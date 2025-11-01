#include "u64.h"
#include "hdmi_scan.h"
#include "color_timings.h"

extern "C" void SetScanModeRegisters(volatile t_video_timing_regs *regs, const TVideoMode *mode)
{
    regs->VID_HSYNCPOL     = mode->hsyncpol;
    regs->VID_HSYNCTIME    = mode->hsync >> 1;
    regs->VID_HBACKPORCH   = mode->hbackporch >> 2;
    regs->VID_HACTIVE      = mode->hactive >> 3;
    regs->VID_HFRONTPORCH  = mode->hfrontporch >> 2;
    regs->VID_HREPETITION  = 0;
    regs->VID_VSYNCPOL     = mode->vsyncpol;
    regs->VID_VSYNCTIME    = mode->vsync;
    regs->VID_VBACKPORCH   = mode->vbackporch;
    regs->VID_VACTIVE      = mode->vactive >> 3;
    regs->VID_VFRONTPORCH  = mode->vfrontporch;
    regs->VID_VIC          = mode->vic;
    regs->VID_REPCN        = mode->pixel_repetition - 1;
    regs->VID_Y            = mode->color_mode;
}

extern "C" void SetVideoMode(t_video_mode mode)
{
    volatile t_video_timing_regs *regs = (volatile t_video_timing_regs *)VID_IO_BASE;

    const TVideoMode pal  =  { 17, 27023962,  720, 12,  64,  68, 0,   576,  4, 5, 39, 0, 1, 0 };  // VIC 17/18 720x576 @ 50.12Hz (624 lines) (!) // detuned + one line less
    const TVideoMode ntsc =  { 03, 27000000,  720, 16,  62,  60, 0,   480,  9, 6, 31, 0, 1, 0 };  // VIC 2/3 720x480 @ 60Hz

    const t_video_color_timing *ct = color_timings[(int)mode];
    if (ct->audio_div == 77) {
        SetScanModeRegisters(regs, &pal);
    } else {
        SetScanModeRegisters(regs, &ntsc);
    }
}

typedef struct {
    uint8_t offset_x;
    uint8_t offset_y;
    uint8_t size_x;
    uint8_t size_y;
} t_vic_crop_regs;

extern "C" void SetVicCrop(int x, int y, int x_size, int y_size)
{
    volatile t_vic_crop_regs *crop = (volatile t_vic_crop_regs *)U64II_CROPPER_BASE;

    crop->offset_x = x;
    crop->offset_y = y;
    crop->size_x   = x_size >> 1;
    crop->size_y   = y_size >> 1;
}

extern "C" void SetVideoMode1080p(t_video_mode mode)
{
    volatile t_video_timing_regs *regs = (volatile t_video_timing_regs *)U64II_HDMI_REGS;
    
    const t_video_color_timing *ct = color_timings[(int)mode];
    bool pal = (ct->audio_div == 77);

    const TVideoMode sm_1080p50 =  { 31, 148869856, 1920, 528, 44, 148, 1,  1080,  4, 5, 36, 1, 1, 0 }; // 16:9
    const TVideoMode sm_1080p60 =  { 16, 148069608, 1920, 88,  44, 148, 1,  1080,  4, 5, 36, 1, 1, 0 }; // 16:9
    const TVideoMode sm_576p50  =  { 17,  27067270,  720, 12,  64,  68, 0,   576,  5, 5, 39, 0, 1, 0 }; // 4:3
    const TVideoMode sm_480p60  =  {  1,  25126964,  640, 16,  96,  48, 0,   480, 10, 2, 33, 0, 1, 0 }; // 4:3
    const TVideoMode sm_720p50  =  { 19,  74434928, 1280, 440, 40, 220, 1,   720,  5, 5, 20, 1, 1, 0 }; // 16:9
    const TVideoMode sm_720p60  =  {  4,  74034804, 1280, 108, 42, 220, 1,   720,  5, 5, 20, 1, 1, 0 }; // 16:9 (again, not possible to encode 110 is not divisable by 4)

    const TVideoMode sm_800x600x50  =  {  0,  34944000,  800, 40, 112,  88, 1,   600, 30, 5, 37, 1, 1, 0 }; // 4:3  (1040 x 672)
    const TVideoMode sm_800x600x60  =  {  0,  41028000,  800, 40, 124,  88, 1,   600, 20, 5, 25, 1, 1, 0 }; // 4:3  (1052 x 650)

    const TVideoMode sm_1024x768x50 =  {  0,  54163200,  1024, 24, 136, 160, 0, 768,  3, 6, 29, 0, 1, 0 }; // 4:3  (1344 x 806)
    const TVideoMode sm_1024x768x60 =  {  0,  68140800,  1024, 24, 136, 160, 0, 768, 42, 6, 29, 0, 1, 0 }; // 4:3  (1344 x 845) frontporch 3 -> 42 lines

    const TVideoMode sm_1280x1024x50 = {  0,  92164800,  1280, 48, 112, 248, 1, 1024, 27, 3, 38, 1, 1, 0 }; // 5:4 Frontporch 1 → 27 lines
    const TVideoMode sm_1280x1024x60 = {  0, 106546560,  1280, 48, 112, 248, 1, 1024,  1, 3, 24, 1, 1, 0 }; // 5:4 Backporch 38 → 24 lines

    // h_scaling   <= unsigned(io_req.data(2 downto 0));
    // vstretch    <= io_req.data(3);
    // v_scaling   <= unsigned(io_req.data(7 downto 4));

    // signal hdmi_horiz_mode : unsigned(2 downto 0); -- 000 = x5/4, 001 = x4/3,  010 = x5/3,  011 = x15/8, 100 = x5/2, 101 = x8/3,  110 = x10/3, 111 = x15/4
    //                                                -- 000 = 1.25, 001 = 1.333, 010 = 1.666, 011 = 1.875, 100 = 2.50, 101 = 2.666, 110 = 3.333, 111 = 3.75
    //                                 --   384 input -- 000 = 480,  001 = 512,   010 = 640,   011 = 720,   100 = 960,  101 = 1024,  110 = 1280,  111 = 1440
    // signal hdmi_vert_mode  : unsigned(3 downto 0); -- bit 3 is x4 regime, three remaining bits are:
    //                                                --                      240   270   240  270
    //                                                -- OFF: x1.000          480   540   960  1080
    //                                                -- 000: x1.111 (10/9)   533   600  1067  1200
    //                                                -- 001: x1.125 (9/8)    540   608  1080  1215
    //                                                -- 010: x1.250 (5/4)    600   675  1200  1350
    //                                                -- 011: x1.333 (4/3)    640   720  1280  1440
    //                                                -- 100: x1.429 (10/7)   686   771  1371  1543
    //                                                -- 101: x1.500 (3/2)    720   810  1440  1620
    //                                                -- 110: x1.600 (8/5)    768   864  1536  1728
    //                                                -- 111: x1.800 (9/5)    864   972  1728  1944

    // NTSC Input:
    // 00:480x480	    01:512x480	    02:640x480	    03:720x480	    04:960x480	    05:1024x480	    06:1280x480	    07:1440x480
    // 08:480x533	    09:512x533	    0A:640x533	    0B:720x533	    0C:960x533	    0D:1024x533	    0E:1280x533	    0F:1440x533
    // 18:480x540	    19:512x540	    1A:640x540	    1B:720x540	    1C:960x540	    1D:1024x540	    1E:1280x540	    1F:1440x540
    // 28:480x600	    29:512x600	    2A:640x600	    2B:720x600	    2C:960x600	    2D:1024x600	    2E:1280x600	    2F:1440x600
    // 38:480x640	    39:512x640	    3A:640x640	    3B:720x640	    3C:960x640	    3D:1024x640	    3E:1280x640	    3F:1440x640
    // 48:480x686	    49:512x686	    4A:640x686	    4B:720x686	    4C:960x686	    4D:1024x686	    4E:1280x686	    4F:1440x686
    // 58:480x720	    59:512x720	    5A:640x720	    5B:720x720	    5C:960x720	    5D:1024x720	    5E:1280x720	    5F:1440x720
    // 68:480x768	    69:512x768	    6A:640x768	    6B:720x768	    6C:960x768	    6D:1024x768	    6E:1280x768	    6F:1440x768
    // 78:480x864	    79:512x864	    7A:640x864	    7B:720x864	    7C:960x864	    7D:1024x864	    7E:1280x864	    7F:1440x864
    // 80:480x960	    81:512x960	    82:640x960	    83:720x960	    84:960x960	    85:1024x960	    86:1280x960	    87:1440x960
    // 88:480x1067	    89:512x1067	    8A:640x1067	    8B:720x1067	    8C:960x1067	    8D:1024x1067	8E:1280x1067	8F:1440x1067
    // 98:480x1080	    99:512x1080	    9A:640x1080	    9B:720x1080	    9C:960x1080	    9D:1024x1080	9E:1280x1080	9F:1440x1080
    // A8:480x1200	    A9:512x1200	    AA:640x1200	    AB:720x1200	    AC:960x1200	    AD:1024x1200	AE:1280x1200	AF:1440x1200
    // B8:480x1280	    B9:512x1280	    BA:640x1280	    BB:720x1280	    BC:960x1280	    BD:1024x1280	BE:1280x1280	BF:1440x1280
    // C8:480x1371	    C9:512x1371	    CA:640x1371	    CB:720x1371	    CC:960x1371	    CD:1024x1371	CE:1280x1371	CF:1440x1371
    // D8:480x1440	    D9:512x1440	    DA:640x1440	    DB:720x1440	    DC:960x1440	    DD:1024x1440	DE:1280x1440	DF:1440x1440
    // E8:480x1536	    E9:512x1536	    EA:640x1536	    EB:720x1536	    EC:960x1536	    ED:1024x1536	EE:1280x1536	EF:1440x1536
    // F8:480x1728	    F9:512x1728	    FA:640x1728	    FB:720x1728	    FC:960x1728	    FD:1024x1728	FE:1280x1728	FF:1440x1728
    //
    // PAL:
    // 00:480x540	    01:512x540	    02:640x540	    03:720x540	    04:960x540	    05:1024x540	    06:1280x540	    07:1440x540
    // 08:480x600	    09:512x600	    0A:640x600	    0B:720x600	    0C:960x600	    0D:1024x600	    0E:1280x600	    0F:1440x600
    // 18:480x608	    19:512x608	    1A:640x608	    1B:720x608	    1C:960x608	    1D:1024x608	    1E:1280x608	    1F:1440x608
    // 28:480x675	    29:512x675	    2A:640x675	    2B:720x675	    2C:960x675	    2D:1024x675	    2E:1280x675	    2F:1440x675
    // 38:480x720	    39:512x720	    3A:640x720	    3B:720x720	    3C:960x720	    3D:1024x720	    3E:1280x720	    3F:1440x720
    // 48:480x771	    49:512x771	    4A:640x771	    4B:720x771	    4C:960x771	    4D:1024x771	    4E:1280x771	    4F:1440x771
    // 58:480x810	    59:512x810	    5A:640x810	    5B:720x810	    5C:960x810	    5D:1024x810	    5E:1280x810	    5F:1440x810
    // 68:480x864	    69:512x864	    6A:640x864	    6B:720x864	    6C:960x864	    6D:1024x864	    6E:1280x864	    6F:1440x864
    // 78:480x972	    79:512x972	    7A:640x972	    7B:720x972	    7C:960x972	    7D:1024x972	    7E:1280x972	    7F:1440x972
    // 80:480x1080	    81:512x1080	    82:640x1080	    83:720x1080	    84:960x1080	    85:1024x1080	86:1280x1080	87:1440x1080
    // 88:480x1200	    89:512x1200	    8A:640x1200	    8B:720x1200	    8C:960x1200	    8D:1024x1200	8E:1280x1200	8F:1440x1200
    // 98:480x1215	    99:512x1215	    9A:640x1215	    9B:720x1215	    9C:960x1215	    9D:1024x1215	9E:1280x1215	9F:1440x1215
    // A8:480x1350	    A9:512x1350	    AA:640x1350	    AB:720x1350	    AC:960x1350	    AD:1024x1350	AE:1280x1350	AF:1440x1350
    // B8:480x1440	    B9:512x1440	    BA:640x1440	    BB:720x1440	    BC:960x1440	    BD:1024x1440	BE:1280x1440	BF:1440x1440
    // C8:480x1543	    C9:512x1543	    CA:640x1543	    CB:720x1543	    CC:960x1543	    CD:1024x1543	CE:1280x1543	CF:1440x1543
    // D8:480x1620	    D9:512x1620	    DA:640x1620	    DB:720x1620	    DC:960x1620	    DD:1024x1620	DE:1280x1620	DF:1440x1620
    // E8:480x1728	    E9:512x1728	    EA:640x1728	    EB:720x1728	    EC:960x1728	    ED:1024x1728	EE:1280x1728	EF:1440x1728
    // F8:480x1944	    F9:512x1944	    FA:640x1944	    FB:720x1944	    FC:960x1944	    FD:1024x1944	FE:1280x1944	FF:1440x1944


#if 1
    if (pal) {
        regs->gearbox = 0;
        SetScanModeRegisters(regs, &sm_1280x1024x50);
        SetVicCrop(9, 4, 382, 280);
        regs->scaler = 0x7E;
        regs->x_offset = 0;
        regs->resync = 2;
    } else {
        regs->gearbox = 0;
        SetScanModeRegisters(regs, &sm_1280x1024x60);
        SetVicCrop(9, 0, 382, 240);
        regs->scaler = 0x86;
        regs->x_offset = 0;
        regs->resync = 2;
    }
#endif

#if 0
    if (pal) {
        regs->gearbox = 2;
        SetScanModeRegisters(regs, &sm_1024x768x50);
        SetVicCrop(9, 10, 382, 268);
        regs->scaler = 0x4D;
        regs->x_offset = 0;
        regs->resync = 2;
    } else {
        regs->gearbox = 0;
        SetScanModeRegisters(regs, &sm_1024x768x60);
        SetVicCrop(9, 0, 382, 240);
        regs->scaler = 0x6D;
        regs->x_offset = 0;
        regs->resync = 2;
    }
#endif

#if 0
    if (pal) {
        regs->gearbox = 2;
        SetScanModeRegisters(regs, &sm_800x600x50);
        SetVicCrop(0, 9, 400, 270);
        regs->scaler = 0x0B; // wrong horizontal
        regs->x_offset = 25;
        regs->resync = 2;
    } else {
        regs->gearbox = 2;
        SetScanModeRegisters(regs, &sm_800x600x60);
        SetVicCrop(0, 0, 400, 240);
        regs->scaler = 0x2B; // wrong horizontal
        regs->x_offset = 25;
        regs->resync = 2;
    }
#endif

#if 0
    if (pal) {
        regs->gearbox = 2;
        SetScanModeRegisters(regs, &sm_720p50);
        SetVicCrop(8, 9, 384, 270);
        regs->scaler = 0x3C;
        regs->x_offset = 160;
        regs->resync = 2;
    } else {
        regs->gearbox = 2;
        SetScanModeRegisters(regs, &sm_720p60);
        SetVicCrop(8, 0, 384, 240);
        regs->scaler = 0x5C;
        regs->x_offset = 160;
        regs->resync = 2;
    }
#endif

#if 0
    if (pal) {
        regs->gearbox = 0;
        SetScanModeRegisters(regs, &sm_1080p50);
        SetVicCrop(8, 9, 384, 270);
        regs->scaler = 0x87;
        regs->x_offset = 240;
        regs->resync = 2;
    } else {
        regs->gearbox = 0;
        SetScanModeRegisters(regs, &sm_1080p60);
        SetVicCrop(8, 0, 384, 240);
        regs->scaler = 0x9F;
        regs->x_offset = 240;
        regs->resync = 2;
    }
#endif

#if 0
    if (pal) {
        regs->gearbox = 1;
        SetScanModeRegisters(regs, &sm_480p60);
        regs->VID_A = 0;
        regs->VID_R = 9; // 4:3
        regs->VID_M = 1; // 4:3
        SetVicCrop(0, 0, 400, 288);
        regs->scaler = 0x02;
        regs->x_offset = 26;
        regs->resync = 2;
    } else {
        regs->gearbox = 1;
        SetScanModeRegisters(regs, &vga);
        SetVicCrop(8, 0, 383, 240);
        regs->VID_A = 0;
        regs->VID_R = 9; // 4:3
        regs->VID_M = 1; // 4:3
        regs->scaler = 0x02;
        regs->x_offset = 0;
        regs->resync = 2;
    }
#endif
}
