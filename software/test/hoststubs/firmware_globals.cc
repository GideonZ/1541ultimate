// Host-test doubles for the firmware globals that dos.cc links against.
//
// These live in translation units that talk to hardware (rtc.cc, c64.cc,
// c1541.cc, subsys.cc, itu.c) and cannot be built on the host. Only the symbols
// the command interface actually reaches are provided here, with deterministic
// behaviour so tests can rely on them.

#include "rtc.h"
#include "c64.h"
#include "c1541.h"
#include "subsys.h"
#include "itu.h"

// --- Drives ---------------------------------------------------------------
// No drives are attached by default. Tests that need one assign these.
C1541 *c1541_A = 0;
C1541 *c1541_B = 0;

// --- Real time clock ------------------------------------------------------
// A fixed, valid timestamp: 2026-08-02 12:34:56. The year is stored as an
// offset from 1980, matching the firmware convention.
Rtc rtc;

Rtc::Rtc() : capable(true), cfg(0)
{
    for (int i = 0; i < 16; i++) {
        rtc_regs[i] = 0;
    }
}

Rtc::~Rtc()
{
}

int Rtc::get_correction(void)
{
    return 0;
}

void Rtc::get_time(int &y, int &M, int &D, int &wd, int &h, int &m, int &s)
{
    y = 46; // 1980 + 46 = 2026
    M = 8;
    D = 2;
    wd = 0;
    h = 12;
    m = 34;
    s = 56;
}

void Rtc::set_time(int y, int M, int D, int wd, int h, int m, int s)
{
    (void)y; (void)M; (void)D; (void)wd; (void)h; (void)m; (void)s;
}

void Rtc::set_time_in_chip(int corr, int y, int M, int D, int wd, int h, int m, int s)
{
    (void)corr; (void)y; (void)M; (void)D; (void)wd; (void)h; (void)m; (void)s;
}

// --- C64 ------------------------------------------------------------------
// The MP3 ram drives are a U64 feature and are reported as absent.
int C64::isMP3RamDrive(int dev)
{
    (void)dev;
    return 0;
}

int C64::getSizeOfMP3NativeRamdrive(int dev)
{
    (void)dev;
    return 0;
}

// --- Subsystem dispatch ---------------------------------------------------
// Commands are recorded rather than executed, so tests can assert on what the
// command interface asked the rest of the firmware to do.
IndexedList<SubsysCommand *>& executed_subsys_commands()
{
    static IndexedList<SubsysCommand *> commands(8, 0);
    return commands;
}

SubsysResultCode_t SubsysCommand::execute(void)
{
    executed_subsys_commands().append(this);
    SubsysResultCode_t result;
    result.status = SSRET_OK;
    return result;
}

// --- FPGA -----------------------------------------------------------------
uint32_t getFpgaCapabilities(void)
{
    return 0xFFFFFFFF; // report every capability as present
}
