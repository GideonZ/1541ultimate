// Stand-in for the tape player: it takes over the File it is given, as the real one does
// (set_file: "the tape controller is now owner of the File object").
#ifndef TAPE_TESTS_TAPE_CONTROLLER_H
#define TAPE_TESTS_TAPE_CONTROLLER_H
#include "filemanager.h"
class TapeController
{
public:
    File *file = NULL;
    int set_file_calls = 0, started = 0, stops = 0, closes = 0, start_mode = 0;
    uint32_t length = 0;
    void stop() { stops++; }
    void close();
    void start(int m) { started++; start_mode = m; }
    void set_file(File *f, uint32_t len, int, int) { file = f; length = len; set_file_calls++; }
};
extern TapeController *tape_controller;
#endif
