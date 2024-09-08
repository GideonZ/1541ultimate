#include "color_timings.h"

#define TABLE_504 0x00
#define TABLE_520 0x80

const t_video_color_timing c_pal_50_283_5   = { 0xEAB1A83C, 18, 77, 0x37 | TABLE_504, 24, VIDEO_FMT_CYCLES_63, 81247 };
const t_video_color_timing c_ntsc_60_227_5  = { 0xA2E8BA2E, 19, 80, 0x07 | TABLE_520, 32, VIDEO_FMT_CYCLES_65 | VIDEO_FMT_NTSC_ENCODING | VIDEO_FMT_NTSC_FREQ | VIDEO_FMT_60_HZ, 84338 };
const t_video_color_timing c_pal_60_free    = { 0xA4F3912B, 19, 80, 0x22 | TABLE_504, 24, VIDEO_FMT_CYCLES_65 |                           VIDEO_FMT_NTSC_FREQ | VIDEO_FMT_60_HZ, 84372 };
const t_video_color_timing c_pal_60_281_5   = { 0xA7EDCCD2, 19, 80, 0x33 | TABLE_520, 24, VIDEO_FMT_CYCLES_65 |                           VIDEO_FMT_NTSC_FREQ | VIDEO_FMT_60_HZ, 84422 };
const t_video_color_timing c_ntsc_50_free   = { 0xEDDAEBE2, 18, 77, 0x18 | TABLE_520, 32, VIDEO_FMT_CYCLES_63 | VIDEO_FMT_NTSC_ENCODING, 81300 };
const t_video_color_timing c_ntsc_50_228_5  = { 0xF2E98AC5, 18, 77, 0x09 | TABLE_504, 32, VIDEO_FMT_CYCLES_63 | VIDEO_FMT_NTSC_ENCODING, 81385 };

//const t_video_color_timing c_pal_60_282_5   = { 0x961DE490, 19, 80, 0x35 | TABLE_520, 24, VIDEO_FMT_CYCLES_65 |                           VIDEO_FMT_NTSC_FREQ | VIDEO_FMT_60_HZ, 84338 };
//const t_video_color_timing c_pal_60_283_5   = { 0x846E277B, 19, 80, 0x37 | TABLE_520, 24, VIDEO_FMT_CYCLES_65 |                           VIDEO_FMT_NTSC_FREQ | VIDEO_FMT_60_HZ, 84338 };
//const t_video_color_timing c_ntsc_50_229_0  = { 0xE8521D78, 18, 77, 0x0A | TABLE_504, 32, VIDEO_FMT_CYCLES_63 | VIDEO_FMT_NTSC_ENCODING, 81429 };
//const t_video_color_timing c_ntsc_50_227_5  = { 0x083C26AB, 19, 77, 0x07 | TABLE_504, 32, VIDEO_FMT_CYCLES_63 | VIDEO_FMT_NTSC_ENCODING, 81429 };
//const t_video_color_timing c_ntsc_50_228_5r = { 0xF2E98AC5, 18, 77, 0x09 | TABLE_504, 32, VIDEO_FMT_CYCLES_63 | VIDEO_FMT_NTSC_ENCODING | VIDEO_FMT_RESET_BURST, 81429 };
//const t_video_color_timing c_ntsc_50_227_5r = { 0x083C26AB, 19, 77, 0x07 | TABLE_504, 32, VIDEO_FMT_CYCLES_63 | VIDEO_FMT_NTSC_ENCODING | VIDEO_FMT_RESET_BURST, 81429 };

const t_video_color_timing *color_timings[] = {
        &c_pal_50_283_5,
        &c_ntsc_60_227_5,
        &c_pal_60_free,
        &c_ntsc_50_free,
        &c_pal_60_281_5,  // best timing match to original C64
        &c_ntsc_50_228_5, //  best timing match to original C64
};
