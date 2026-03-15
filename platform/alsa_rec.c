#include "alsa_rec.h"
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define ALSA_REC_DEBUG(fmt, ...) printf("[ALSA REC] " fmt "\n", ##__VA_ARGS__)

// WAV 文件头结构（44字节）
typedef struct {
    char     riff[4];            // "RIFF"
    uint32_t file_size;           // 文件总长度 - 8
    char     wave[4];            // "WAVE"
    char     fmt[4];             // "fmt "
    uint32_t fmt_len;             // 格式块长度（16）
    uint16_t audio_fmt;           // 音频格式（1=PCM）
    uint16_t num_channels;        // 声道数
    uint32_t sample_rate;         // 采样率
    uint32_t byte_rate;           // 字节率 = sample_rate * num_channels * bits_per_sample/8
    uint16_t block_align;         // 块对齐 = num_channels * bits_per_sample/8
    uint16_t bits_per_sample;     // 位深
    char     data[4];             // "data"
    uint32_t data_size;           // 数据大小
} wav_header_t;

struct AlsaRecorder {
    char               device[32];
    unsigned int       samplerate;
    unsigned int       channels;
    snd_pcm_format_t   format;
    int                bits_per_sample;

    snd_pcm_t         *pcm_handle;
    pthread_t          thread;
    volatile bool      recording;
    volatile bool      abort_flag;        // 请求终止并删除文件
    volatile bool      finish_called;     // 回调已调用标志

    char               filename[256];
    FILE              *file;
    uint32_t           total_bytes;       // 已写入的数据字节数
    time_t             start_time;        // 开始录音的时间（秒）

    pthread_mutex_t    mutex;

    // 完成回调
    alsa_rec_callback_t finish_cb;
    void               *finish_user;
};

// 内部函数声明
static void *record_thread_func(void *arg);
static int write_wav_header(FILE *fp, const wav_header_t *hdr);
static int update_wav_header(FILE *fp, uint32_t data_size);

AlsaRecorder *alsa_recorder_create(const char *device,
                                    unsigned int samplerate,
                                    unsigned int channels,
                                    snd_pcm_format_t format)
{
    AlsaRecorder *rec = calloc(1, sizeof(AlsaRecorder));
    if (!rec) return NULL;

    strncpy(rec->device, device, sizeof(rec->device) - 1);
    rec->samplerate = samplerate;
    rec->channels = channels;
    rec->format = format;
    rec->bits_per_sample = snd_pcm_format_physical_width(format);
    rec->recording = false;
    rec->abort_flag = false;
    rec->finish_called = false;
    rec->file = NULL;
    rec->total_bytes = 0;
    rec->start_time = 0;
    rec->finish_cb = NULL;
    rec->finish_user = NULL;

    pthread_mutex_init(&rec->mutex, NULL);

    ALSA_REC_DEBUG("Created recorder: dev=%s, rate=%u, ch=%u, bits=%d",
                   device, samplerate, channels, rec->bits_per_sample);

    return rec;
}

void alsa_recorder_destroy(AlsaRecorder *rec)
{
    if (!rec) return;

    // 如果正在录音，先终止并删除文件
    if (rec->recording) {
        alsa_recorder_abort(rec);
        // 等待线程结束
        pthread_join(rec->thread, NULL);
    }

    if (rec->pcm_handle) {
        snd_pcm_close(rec->pcm_handle);
    }

    pthread_mutex_destroy(&rec->mutex);
    free(rec);
    ALSA_REC_DEBUG("Destroyed");
}

int alsa_recorder_start(AlsaRecorder *rec, const char *filename)
{
    if (!rec || !filename) return -1;

    pthread_mutex_lock(&rec->mutex);

    if (rec->recording) {
        ALSA_REC_DEBUG("Already recording");
        pthread_mutex_unlock(&rec->mutex);
        return -1;
    }

    // 打开 PCM 设备
    int err;
    snd_pcm_t *pcm;
    err = snd_pcm_open(&pcm, rec->device, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        ALSA_REC_DEBUG("Failed to open PCM device %s: %s", rec->device, snd_strerror(err));
        pthread_mutex_unlock(&rec->mutex);
        return -1;
    }

    // 设置硬件参数
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    err = snd_pcm_hw_params_any(pcm, hw_params);
    if (err < 0) {
        ALSA_REC_DEBUG("Cannot get hardware parameters: %s", snd_strerror(err));
        goto error;
    }

    err = snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        ALSA_REC_DEBUG("Cannot set access type: %s", snd_strerror(err));
        goto error;
    }

    err = snd_pcm_hw_params_set_format(pcm, hw_params, rec->format);
    if (err < 0) {
        ALSA_REC_DEBUG("Cannot set format: %s", snd_strerror(err));
        goto error;
    }

    err = snd_pcm_hw_params_set_channels(pcm, hw_params, rec->channels);
    if (err < 0) {
        ALSA_REC_DEBUG("Cannot set channels: %s", snd_strerror(err));
        goto error;
    }

    unsigned int rate = rec->samplerate;
    err = snd_pcm_hw_params_set_rate_near(pcm, hw_params, &rate, 0);
    if (err < 0 || rate != rec->samplerate) {
        ALSA_REC_DEBUG("Cannot set rate %u (got %u)", rec->samplerate, rate);
        goto error;
    }

    err = snd_pcm_hw_params(pcm, hw_params);
    if (err < 0) {
        ALSA_REC_DEBUG("Cannot set hardware parameters: %s", snd_strerror(err));
        goto error;
    }

    rec->pcm_handle = pcm;

    // 创建文件
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        ALSA_REC_DEBUG("Cannot create file %s: %s", filename, strerror(errno));
        goto error;
    }

    // 写入占位的 WAV 头（data_size 先写 0）
    wav_header_t hdr;
    memcpy(hdr.riff, "RIFF", 4);
    hdr.file_size = 0; // 暂不计算
    memcpy(hdr.wave, "WAVE", 4);
    memcpy(hdr.fmt, "fmt ", 4);
    hdr.fmt_len = 16;
    hdr.audio_fmt = 1; // PCM
    hdr.num_channels = rec->channels;
    hdr.sample_rate = rec->samplerate;
    hdr.bits_per_sample = rec->bits_per_sample;
    hdr.byte_rate = rec->samplerate * rec->channels * rec->bits_per_sample / 8;
    hdr.block_align = rec->channels * rec->bits_per_sample / 8;
    hdr.data_size = 0;
    memcpy(hdr.data, "data", 4);

    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1) {
        ALSA_REC_DEBUG("Failed to write WAV header");
        fclose(fp);
        unlink(filename);
        goto error;
    }
    fflush(fp);

    rec->file = fp;
    strncpy(rec->filename, filename, sizeof(rec->filename) - 1);
    rec->total_bytes = 0;
    rec->start_time = time(NULL);
    rec->abort_flag = false;
    rec->finish_called = false;
    rec->recording = true;

    // 创建录音线程
    err = pthread_create(&rec->thread, NULL, record_thread_func, rec);
    if (err != 0) {
        ALSA_REC_DEBUG("Failed to create thread");
        rec->recording = false;
        fclose(rec->file);
        unlink(filename);
        rec->file = NULL;
        goto error;
    }

    pthread_mutex_unlock(&rec->mutex);
    ALSA_REC_DEBUG("Recording started: %s", filename);
    return 0;

error:
    snd_pcm_close(pcm);
    pthread_mutex_unlock(&rec->mutex);
    return -1;
}

int alsa_recorder_stop(AlsaRecorder *rec)
{
    if (!rec) return -1;

    pthread_mutex_lock(&rec->mutex);

    if (!rec->recording) {
        pthread_mutex_unlock(&rec->mutex);
        return 0;
    }

    // 设置标志，让线程退出
    rec->recording = false;
    rec->abort_flag = false; // 正常停止，保存文件

    pthread_mutex_unlock(&rec->mutex);

    // 等待线程结束
    pthread_join(rec->thread, NULL);

    // 线程结束后，文件已更新头部并关闭
    ALSA_REC_DEBUG("Recording stopped, file saved: %s", rec->filename);
    return 0;
}

int alsa_recorder_abort(AlsaRecorder *rec)
{
    if (!rec) return -1;

    pthread_mutex_lock(&rec->mutex);

    if (!rec->recording) {
        pthread_mutex_unlock(&rec->mutex);
        return 0;
    }

    // 设置放弃标志
    rec->recording = false;
    rec->abort_flag = true;

    pthread_mutex_unlock(&rec->mutex);

    // 等待线程结束
    pthread_join(rec->thread, NULL);

    ALSA_REC_DEBUG("Recording aborted, file deleted: %s", rec->filename);
    return 0;
}

bool alsa_recorder_is_recording(AlsaRecorder *rec)
{
    if (!rec) return false;
    pthread_mutex_lock(&rec->mutex);
    bool ret = rec->recording;
    pthread_mutex_unlock(&rec->mutex);
    return ret;
}

unsigned int alsa_recorder_get_duration(AlsaRecorder *rec)
{
    if (!rec) return 0;
    pthread_mutex_lock(&rec->mutex);
    unsigned int dur = 0;
    if (rec->recording && rec->start_time > 0) {
        dur = (unsigned int)(time(NULL) - rec->start_time);
    }
    pthread_mutex_unlock(&rec->mutex);
    return dur;
}

void alsa_recorder_set_finish_callback(AlsaRecorder *rec,
                                        alsa_rec_callback_t callback,
                                        void *user_data)
{
    if (!rec) return;
    pthread_mutex_lock(&rec->mutex);
    rec->finish_cb = callback;
    rec->finish_user = user_data;
    pthread_mutex_unlock(&rec->mutex);
}

// ---------- 内部函数 ----------

static void *record_thread_func(void *arg)
{
    AlsaRecorder *rec = (AlsaRecorder *)arg;
    snd_pcm_t *pcm = rec->pcm_handle;
    FILE *fp = rec->file;
    int channels = rec->channels;
    int bits = rec->bits_per_sample;
    int frame_size = channels * bits / 8; // 每帧字节数

    // 分配缓冲区（例如 4096 帧）
    int frames_per_buffer = 4096;
    int buffer_size = frames_per_buffer * frame_size;
    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
        ALSA_REC_DEBUG("Out of memory in record thread");
        goto thread_exit;
    }

    ALSA_REC_DEBUG("Record thread started");

    while (1) {
        // 检查是否应该停止
        pthread_mutex_lock(&rec->mutex);
        bool should_stop = !rec->recording;
        pthread_mutex_unlock(&rec->mutex);
        if (should_stop) break;

        // 读取 PCM 数据
        int frames_read = snd_pcm_readi(pcm, buffer, frames_per_buffer);
        if (frames_read < 0) {
            frames_read = snd_pcm_recover(pcm, frames_read, 0);
            if (frames_read < 0) {
                ALSA_REC_DEBUG("snd_pcm_readi error: %s", snd_strerror(frames_read));
                break;
            }
            continue;
        }
        if (frames_read == 0) {
            // 没有数据？可能设备断开
            break;
        }

        int bytes_read = frames_read * frame_size;

        // 写入文件
        pthread_mutex_lock(&rec->mutex);
        if (fp && !rec->abort_flag) {
            size_t written = fwrite(buffer, 1, bytes_read, fp);
            if (written != bytes_read) {
                ALSA_REC_DEBUG("Write error: %s", strerror(errno));
                // 文件写入失败，终止
                pthread_mutex_unlock(&rec->mutex);
                break;
            }
            rec->total_bytes += written;
        }
        pthread_mutex_unlock(&rec->mutex);
    }

    free(buffer);

thread_exit:
    // 线程即将退出，处理文件
    pthread_mutex_lock(&rec->mutex);
    bool abort = rec->abort_flag;
    FILE *f = rec->file;
    char fname[256];
    strncpy(fname, rec->filename, sizeof(fname) - 1);
    rec->file = NULL; // 防止重复关闭
    pthread_mutex_unlock(&rec->mutex);

    if (f) {
        if (abort) {
            // 放弃录音：关闭文件并删除
            fclose(f);
            unlink(fname);
            ALSA_REC_DEBUG("Deleted incomplete file: %s", fname);
        } else {
            // 正常结束：更新 WAV 头部数据大小
            uint32_t data_size = rec->total_bytes;
            update_wav_header(f, data_size);
            fclose(f);
            ALSA_REC_DEBUG("Finalized WAV file: %s, data size=%u", fname, data_size);
        }
    }

    // 关闭 PCM
    snd_pcm_close(rec->pcm_handle);
    rec->pcm_handle = NULL;

    // 调用完成回调
    pthread_mutex_lock(&rec->mutex);
    if (rec->finish_cb && !rec->finish_called) {
        rec->finish_called = true;
        alsa_rec_callback_t cb = rec->finish_cb;
        void *user = rec->finish_user;
        pthread_mutex_unlock(&rec->mutex);
        cb(user);
    } else {
        pthread_mutex_unlock(&rec->mutex);
    }

    return NULL;
}

static int write_wav_header(FILE *fp, const wav_header_t *hdr)
{
    rewind(fp);
    return (fwrite(hdr, sizeof(wav_header_t), 1, fp) == 1) ? 0 : -1;
}

static int update_wav_header(FILE *fp, uint32_t data_size)
{
    wav_header_t hdr;
    rewind(fp);
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        return -1;
    }
    hdr.data_size = data_size;
    hdr.file_size = data_size + sizeof(hdr) - 8; // file_size = 36 + data_size
    rewind(fp);
    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1) {
        return -1;
    }
    fflush(fp);
    return 0;
}