#include "audio_dma.h"
#include "itu.h"

void record_audio(audio_dma_t *dma, int *in, const uint32_t len, t_sampling_mode mode)
{
    dma->in_ctrl = 0;
    wait_ms(1);
    dma->in_mode = (uint8_t)mode;
    dma->in_start = in;
    dma->in_end = &(in[len]);
    dma->in_ctrl = 1; // enable once
}

void play_audio(audio_dma_t *dma, int *out, const uint32_t len, const int continuous, t_sampling_mode mode)
{
    dma->out_ctrl = 0;
    dma->misc_io = 0;
    wait_ms(1);
    dma->out_mode = (uint8_t)mode;
    dma->out_start = out;
    dma->out_end = &(out[len]);
    dma->out_ctrl = continuous ? 3 : 1;
}

void play_audio_speaker(audio_dma_t *dma, int *out, const uint32_t len, const int continuous, t_sampling_mode mode)
{
    dma->out_ctrl = 0;
    dma->misc_io = 1;
    wait_ms(1);
    dma->out_mode = (uint8_t)mode;
    dma->out_start = out;
    dma->out_end = &(out[len]);
    dma->out_ctrl = continuous ? 3 : 1;
}

void xfer_audio(audio_dma_t *dma, int *in, int *out, const uint32_t len, t_sampling_mode mode)
{
    dma->in_ctrl = 0;
    dma->out_ctrl = 0;
    wait_ms(1);
    dma->in_mode = (uint8_t)mode;
    dma->out_mode = (uint8_t)mode;
    dma->in_start = in;
    dma->in_end = &(in[len]);
    dma->out_start = out;
    dma->out_end = &(out[len]);
    dma->out_ctrl = 1;
    dma->in_ctrl = 1;
}

void stop_audio(audio_dma_t *dma)
{
    dma->in_ctrl = 0;
    dma->out_ctrl = 0;
    wait_ms(1);
}
