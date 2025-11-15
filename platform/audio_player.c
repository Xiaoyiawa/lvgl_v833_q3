#include "audio_player.h"

#define BUFFER_SIZE 4096
#define MAX_CHANNELS 6

static void * play_thread_func(void * arg);

audio_player_t * player_create(const char * filename)
{
    audio_player_t * player = malloc(sizeof(audio_player_t));
    if(!player) return NULL;

    memset(player, 0, sizeof(audio_player_t));
    player->filename = strdup(filename);

    // 初始化互斥锁
    pthread_mutex_init(&player->mutex, NULL);

    // 初始化状态
    player->state = PLAYER_STOPPED;
    player->seek_request = 0;
    player->current_pts = 0;

    return player;
}

int player_init(audio_player_t * player)
{
    if(!player) return -1;

    pthread_mutex_lock(&player->mutex);

    // 如果已经在播放，直接返回
    if(player->state == PLAYER_PLAYING) {
        pthread_mutex_unlock(&player->mutex);
        return 0;
    }

    int ret = 0;

    // 打开音频文件
    if(avformat_open_input(&player->format_ctx, player->filename, NULL, NULL) < 0) {
        fprintf(stderr, "无法打开音频文件\n");
        ret = -1;
        goto cleanup;
    }

    if(avformat_find_stream_info(player->format_ctx, NULL) < 0) {
        fprintf(stderr, "无法获取流信息\n");
        ret = -1;
        goto cleanup;
    }

    // 查找音频流
    player->audio_stream_index = -1;
    for(int i = 0; i < player->format_ctx->nb_streams; i++) {
        if(player->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            player->audio_stream_index = i;
            break;
        }
    }

    if(player->audio_stream_index == -1) {
        fprintf(stderr, "未找到音频流\n");
        ret = -1;
        goto cleanup;
    }

    // 获取解码器
    AVCodecParameters * codecpar = player->format_ctx->streams[player->audio_stream_index]->codecpar;
    const AVCodec * codec        = avcodec_find_decoder(codecpar->codec_id);
    if(!codec) {
        fprintf(stderr, "未找到对应的解码器\n");
        ret = -1;
        goto cleanup;
    }

    player->codec_ctx = avcodec_alloc_context3(codec);
    if(!player->codec_ctx) {
        fprintf(stderr, "无法分配解码器上下文\n");
        ret = -1;
        goto cleanup;
    }

    if(avcodec_parameters_to_context(player->codec_ctx, codecpar) < 0) {
        fprintf(stderr, "无法复制编解码器参数到解码器上下文\n");
        ret = -1;
        goto cleanup;
    }

    if(avcodec_open2(player->codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "无法打开解码器\n");
        ret = -1;
        goto cleanup;
    }

    // 设置重采样
    player->swr_ctx = swr_alloc();
    if(!player->swr_ctx) {
        fprintf(stderr, "无法分配重采样上下文\n");
        ret = -1;
        goto cleanup;
    }

    // 设置重采样参数
    av_opt_set_int(player->swr_ctx, "in_channel_layout", av_get_default_channel_layout(player->codec_ctx->channels), 0);
    av_opt_set_int(player->swr_ctx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(player->swr_ctx, "in_sample_rate", player->codec_ctx->sample_rate, 0);
    av_opt_set_int(player->swr_ctx, "out_sample_rate", 44100, 0);
    av_opt_set_sample_fmt(player->swr_ctx, "in_sample_fmt", player->codec_ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(player->swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    if(swr_init(player->swr_ctx) < 0) {
        fprintf(stderr, "无法初始化重采样器\n");
        ret = -1;
        goto cleanup;
    }

    player->sample_rate = 44100;
    player->channels    = 2;

    // 打开ALSA设备
    int err;
    if((err = snd_pcm_open(&player->pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "无法打开PCM设备: %s\n", snd_strerror(err));
        ret = -1;
        goto cleanup;
    }

    // 配置PCM参数
    snd_pcm_hw_params_t * hw_params;
    snd_pcm_hw_params_alloca(&hw_params);

    snd_pcm_hw_params_any(player->pcm_handle, hw_params);
    snd_pcm_hw_params_set_access(player->pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(player->pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(player->pcm_handle, hw_params, player->channels);
    snd_pcm_hw_params_set_rate_near(player->pcm_handle, hw_params, &player->sample_rate, 0);

    player->frames = 1024;
    snd_pcm_hw_params_set_period_size_near(player->pcm_handle, hw_params, &player->frames, 0);

    if((err = snd_pcm_hw_params(player->pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "无法设置硬件参数: %s\n", snd_strerror(err));
        ret = -1;
        goto cleanup;
    }

    // 获取持续时间
    AVStream * audio_stream = player->format_ctx->streams[player->audio_stream_index];
    player->time_base       = audio_stream->time_base;
    player->duration        = audio_stream->duration; // 使用流的duration而不是format_ctx的

    // 创建播放线程
    player->state = PLAYER_PAUSED;
    if(pthread_create(&player->play_thread, NULL, play_thread_func, player) != 0) {
        fprintf(stderr, "无法创建播放线程\n");
        player->state = PLAYER_STOPPED;
        ret = -1;
        goto cleanup;
    }

    pthread_mutex_unlock(&player->mutex);
    return 0;

cleanup:
    player_stop(player);
    pthread_mutex_unlock(&player->mutex);
    return ret;
}

static void * play_thread_func(void * arg)
{
    audio_player_t * player = (audio_player_t *)arg;

    AVPacket * packet = av_packet_alloc();
    AVFrame * frame   = av_frame_alloc();
    if(!packet || !frame) {
        fprintf(stderr, "无法分配数据包或帧\n");
        goto cleanup;
    }

    uint8_t * audio_buffer = malloc(BUFFER_SIZE * player->channels * 2); // S16LE
    if(!audio_buffer) {
        fprintf(stderr, "无法分配音频缓冲区\n");
        goto cleanup;
    }

    while(player->state != PLAYER_STOPPED) {

        // 检查跳转请求
        if(player->seek_request) {
            int64_t seek_target = player->seek_pos;
            if(av_seek_frame(player->format_ctx, player->audio_stream_index, seek_target, AVSEEK_FLAG_BACKWARD) < 0) {
                fprintf(stderr, "跳转失败\n");
            } else {
                avcodec_flush_buffers(player->codec_ctx);
                player->current_pts = seek_target;
            }
            player->seek_request = 0;
        }
        
        // 检查暂停状态
        if(player->state == PLAYER_PAUSED) {
            usleep(100000); // 100ms
            continue;
        }

        int ret = av_read_frame(player->format_ctx, packet);
        if(ret < 0) {
            // 文件结束或错误
            //break;
            player->state = PLAYER_PAUSED;
            continue;
        }

        if(packet->stream_index == player->audio_stream_index) {
            ret = avcodec_send_packet(player->codec_ctx, packet);
            if(ret < 0) {
                av_packet_unref(packet);
                continue;
            }

            while(ret >= 0) {
                ret = avcodec_receive_frame(player->codec_ctx, frame);
                if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if(ret < 0) {
                    fprintf(stderr, "解码错误\n");
                    break;
                }

                // 更新当前播放位置
                player->current_pts = frame->pts;

                // 重采样
                uint8_t * out_data[1] = {audio_buffer};
                int out_samples = swr_convert(player->swr_ctx, out_data, BUFFER_SIZE, (const uint8_t **)frame->data,
                                              frame->nb_samples);

                if(out_samples > 0) {
                    int data_size = out_samples * player->channels * 2; // S16LE

                    // 写入ALSA设备
                    snd_pcm_sframes_t frames_written = snd_pcm_writei(player->pcm_handle, audio_buffer, out_samples);
                    if(frames_written < 0) {
                        frames_written = snd_pcm_recover(player->pcm_handle, frames_written, 0);
                    }
                    if(frames_written < 0) {
                        fprintf(stderr, "写入PCM设备错误: %s\n", snd_strerror(frames_written));
                    }
                }

                av_frame_unref(frame);
            }
        }

        av_packet_unref(packet);
    }

cleanup:
    if(packet) av_packet_free(&packet);
    if(frame) av_frame_free(&frame);
    if(audio_buffer) free(audio_buffer);

    return NULL;
}

int player_pause(audio_player_t * player)
{
    if(!player) return -1;

    if(player->state == PLAYER_PLAYING) {
        player->state = PLAYER_PAUSED;
        snd_pcm_pause(player->pcm_handle, 1);
        return 0;
    }
    return -1;
}

int player_resume(audio_player_t * player)
{
    if(!player) return -1;

    if(player->state == PLAYER_PAUSED) {
        player->state = PLAYER_PLAYING;
        snd_pcm_pause(player->pcm_handle, 0);
        return 0;
    }
    return -1;
}

int player_stop(audio_player_t * player)
{
    if(!player) return -1;

    pthread_mutex_lock(&player->mutex);

    player->state = PLAYER_STOPPED;

    // 等待线程结束
    if(player->play_thread) {
        pthread_join(player->play_thread, NULL);
        player->play_thread = 0;
    }

    // 清理资源
    if(player->pcm_handle) {
        snd_pcm_drain(player->pcm_handle);
        snd_pcm_close(player->pcm_handle);
        player->pcm_handle = NULL;
    }

    if(player->swr_ctx) {
        swr_free(&player->swr_ctx);
        player->swr_ctx = NULL;
    }

    if(player->codec_ctx) {
        avcodec_free_context(&player->codec_ctx);
        player->codec_ctx = NULL;
    }

    if(player->format_ctx) {
        avformat_close_input(&player->format_ctx);
        player->format_ctx = NULL;
    }

    pthread_mutex_unlock(&player->mutex);
    return 0;
}

//根据百分比跳转
int player_seek_pct(audio_player_t * player, double percent)
{
    int64_t target_pts = (int64_t)(player->duration * percent / 100.0);
    int64_t now_pts    = player->current_pts;

    printf("now=%lld, duration=%lld\n", now_pts, player->duration);

    if(!player || player->state == PLAYER_STOPPED) return -1;
    if(target_pts < 0) target_pts = 0;
    if(target_pts > player->duration) target_pts = player->duration;

    player->seek_pos = target_pts;
    player->seek_request = 1;
    return 0;

}

//根据毫秒数跳转
int player_seek_ms(audio_player_t * player, int64_t target_ms)
{
    if(player->state != PLAYER_STOPPED) {
        int64_t target_pts = target_ms * (AV_TIME_BASE / 1000);
        int64_t now_pts    = player->current_pts;

        printf("now=%lld, duration=%lld\n", now_pts, player->duration);
        if(!player || target_pts < 0 || target_pts > player->duration || player->state == PLAYER_STOPPED)
            return -1;
        player->seek_pos = target_pts;
        player->seek_request = 1;
        return 0;
    }
    return -1;
}

int64_t player_get_position_ms(audio_player_t * player)
{
    if(!player || player->duration <= 0) return 0;

    int64_t current = player->current_pts;
    return current / (AV_TIME_BASE / 1000);
}

int64_t player_get_duration_ms(audio_player_t * player)
{
    if(!player || player->duration <= 0) return 0;
    return player->duration / (AV_TIME_BASE / 1000);
}

double player_get_position_pct(audio_player_t * player)
{
    if(!player || player->duration <= 0) return 0.0;
    int64_t current = player->current_pts;
    return (double)current / player->duration * 100.0;
}

player_state_t player_get_state(audio_player_t * player)
{
    if(!player) return PLAYER_STOPPED;
    return player->state;
}

void player_destroy(audio_player_t * player)
{
    if(!player) return;

    player_stop(player);

    if(player->filename) {
        free(player->filename);
        player->filename = NULL;
    }

    pthread_mutex_destroy(&player->mutex);
    free(player);
}