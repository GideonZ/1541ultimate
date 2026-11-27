// Stand-in for c64.h: the unfreeze/run commands the tape code issues, and the capability word.
#ifndef TAPE_TESTS_C64_H
#define TAPE_TESTS_C64_H
#include "subsys.h"
#define C64_UNFREEZE 0x0001
#define C64_DRIVE_LOAD 0x0002
#define RUNCODE_TAPE_LOAD_RUN 1
#define RUNCODE_TAPE_RECORD 2
#define CAPAB_C2N_STREAMER 0x00000080
#define CAPAB_C2N_RECORDER 0x00000100
#endif
