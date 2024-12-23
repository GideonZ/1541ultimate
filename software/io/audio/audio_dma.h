#ifndef AUDIO_DMA_H
#define AUDIO_DMA_H

#include <stdint.h>

typedef struct {
    void *in_start;
    void *in_end;
    uint8_t in_ctrl;
    uint8_t in_mode;
    uint8_t in_padding[6];
    void *out_start;
    void *out_end;
    uint8_t out_ctrl;
    uint8_t out_mode;
    uint8_t out_padding[5];
    uint8_t misc_io;
} audio_dma_t;

typedef enum {
    e_32bit_stereo = 0,
    e_16bit_stereo = 1,
    e_32bit_mono = 2,
    e_16bit_mono = 3,
} t_sampling_mode;

void record_audio(audio_dma_t *dma, int *in, const uint32_t len, t_sampling_mode mode);
void play_audio(audio_dma_t *dma, int *out, const uint32_t len, const int continuous, t_sampling_mode mode);
void play_audio_speaker(audio_dma_t *dma, int *out, const uint32_t len, const int continuous, t_sampling_mode mode);
void xfer_audio(audio_dma_t *dma, int *in, int *out, const uint32_t len, t_sampling_mode mode);
void stop_audio(audio_dma_t *dma);

#endif
