/*
 * c64_subsys.h
 *
 *  Created on: Jul 16, 2015
 *      Author: Gideon
 */

#ifndef IO_C64_C64_SUBSYS_H_
#define IO_C64_C64_SUBSYS_H_

#include "subsys.h"
#include "menu.h"
#include "filemanager.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

class C64_Subsys : public SubSystem, ObjectWithMenu
{
    TaskHandle_t taskHandle;
    FileManager *fm;
    C64 *c64;
    TaskCategory *taskCategory;
    struct {
        Action *reset;
        Action *reboot;
        Action *clearmem;
        Action *powercyc;
        Action *off;
        Action *pause;
        Action *resume;
        Action *savereu;
        Action *savemem;
        Action *savemod;
        Action *save_crt;
        Action *savemp3a;
        Action *savemp3b;
        Action *savemp3c;
        Action *savemp3d;
        Action *measure;
    } myActions;

    static void poll(void *a);

    /* Subsystem */
	const char *identify(void) { return "C64 Machine"; }
	SubsysResultCode_e executeCommand(SubsysCommand *cmd);

    /* Object With Menu */
    void create_task_items(void);
    void update_task_items(bool writablePath);

    /* Others */
    int  dma_load(File *f, const uint8_t *buffer, const int bufferSize,
    		const char *name, uint8_t run_mode, uint8_t drv, uint16_t reloc=0);
    int  dma_load_buffer(uint8_t prg_buffer, uint8_t run_mode, uint16_t reloc=0);
    int  dma_load_raw(File *f);
    int  dma_load_raw_buffer(uint16_t offset, uint8_t *buffer, int length, int rw);

    int  load_file_dma(File *f, uint16_t reloc);
    int  load_buffer_dma(const uint8_t *buffer, const int bufferSize, uint16_t reloc);
    bool write_vic_state(File *f);
    void restoreCart(void);
    void measure_timing(const char *path);
public:
	C64_Subsys(C64 *machine);
	virtual ~C64_Subsys();

    friend class FileTypeSID; // sid load does some tricks
};

extern C64_Subsys *c64_subsys;

#endif /* IO_C64_C64_SUBSYS_H_ */
