#ifndef ALSA_REC_H
#define ALSA_REC_H

#include <stdint.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AlsaRecorder AlsaRecorder;

typedef void (*alsa_rec_callback_t)(void * user_data);

AlsaRecorder * alsa_recorder_create(const char * device, unsigned int samplerate, unsigned int channels,
                                    snd_pcm_format_t format);

void alsa_recorder_destroy(AlsaRecorder * rec);

int alsa_recorder_start(AlsaRecorder * rec, const char * filename);

int alsa_recorder_stop(AlsaRecorder * rec);

int alsa_recorder_abort(AlsaRecorder * rec);

bool alsa_recorder_is_recording(AlsaRecorder * rec);

unsigned int alsa_recorder_get_duration(AlsaRecorder * rec);

void alsa_recorder_set_finish_callback(AlsaRecorder * rec, alsa_rec_callback_t callback, void * user_data);

#ifdef __cplusplus
}
#endif

#endif