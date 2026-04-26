#ifndef COLOR_TIMINGS_H
#define COLOR_TIMINGS_H

#include "u64.h"

typedef struct {
    uint32_t frac;
    uint8_t  m;
    uint8_t  audio_div;
    uint8_t  phase_inc;
    uint8_t  burst_phase;
    uint8_t  mode_bits;
    int      ppm;
} t_video_color_timing;

extern const t_video_color_timing *color_timings[];

#endif
