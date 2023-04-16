/*
##########################################################################
##                                                                      ##
##   RENE GARCIA                        GNU General Public Licence V3   ##
##                                                                      ##
##########################################################################
##                                                                      ##
##      Project : MPS Emulator                                          ##
##      Class   : IecPrinter                                            ##
##      Piece   : iec_printer.h                                         ##
##      Language: C ANSI                                                ##
##      Author  : Rene GARCIA                                           ##
##                                                                      ##
##########################################################################
*/

        /*-
         *
         *  $Id:$
         *
         *  $URL:$
         *
        -*/

/******************************  Inclusions  ****************************/

#ifndef IEC_PRINTER_H
#define IEC_PRINTER_H

#include "integer.h"
#include "menu.h"
#include "config.h"
#include "userinterface.h"
#include "iec.h"
#include "subsys.h"
#include "dump_hex.h"
#include "iec_channel.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "filemanager.h"
#include "mystring.h"
#include "mps_printer.h"

/*******************************  Constants  ****************************/

#define IEC_PRINTER_BUFFERSIZE  256
#define IEC_PRINTER_EVENT_CMD	0
#define IEC_PRINTER_EVENT
/*********************************  Types  ******************************/

enum t_printer_output_type {
    PRINTER_PNG_OUTPUT=0,
    PRINTER_RAW_OUTPUT,
    PRINTER_ASCII_OUTPUT
};

enum t_printer_event_type {
    PRINTER_EVENT_CMD=0,
    PRINTER_EVENT_DATA,
    PRINTER_EVENT_USER
};

enum t_printer_usercmd_type {
    PRINTER_USERCMD_RESET=0,
    PRINTER_USERCMD_FLUSH
};

typedef struct PrinterEvent {
    t_printer_event_type type;
    uint8_t value;
} PrinterEvent_t;

/*======================================================================*/
/* Class IecPrinter                                                     */
/*======================================================================*/

class IecPrinter : public SubSystem, ObjectWithMenu, ConfigurableObject
{
    private:
        /* PETASCII to ASCII lookup table */
        static uint8_t ascii_lut[256];

        /* Ultimate IEC interface */
        IecInterface *interface;

        /* Ultimate filesystem interface */
        FileManager *fm;

        /* Output base filename */
        const char *output_filename;

        /* Printer IEC address */
        int last_printer_addr;

        /* Printer enabled or not */
        uint8_t printer_enable;

        /* Printer emulation interface */
        MpsPrinter *mps;

        /* Printer buffer and its pointer */
        uint8_t buffer[IEC_PRINTER_BUFFERSIZE];
        int  buffer_pointer;

        /* Output file descriptor */
        File *f;

        /* Selected output type (RAW, PNG, ASCII) */
        t_printer_output_type output_type;

        /* Flag set to true while in Ultimate init sequence */
        bool init;

        /* User interface descriptor */
        UserInterface *cmd_ui;

        /* The printer task itself */
        TaskHandle_t taskHandle;

        /* Queue to send IEC data to printer */
	QueueHandle_t queueHandle;

        /*==============================================*/
        /*                 M E T H O D S                */
        /*==============================================*/

        /* =======  Printter task related methods */
        static void task(IecPrinter *iecPrinter);
        int _push_data(uint8_t b);
        int _push_command(uint8_t b);

        struct {
            Action *turn_on;
            Action *turn_off;
            Action *eject;
            Action *reset;
        } myActions;

    public:
        IecPrinter(void);
        ~IecPrinter(void);

        int push_data(uint8_t b);
        int push_command(uint8_t b);
        int flush(void);
        int init_done(void);
        int reset(void);

        /* =======  Interface menu */
        void create_task_items();
        void update_task_items(bool writablePath, Path *path);
        void effectuate_settings(void); // from ConfigurableObject
        int executeCommand(SubsysCommand *cmd); // from SubSystem
        const char *identify(void) { return "Virtual Printer"; }

        /* =======  Setters */
        int set_filename(const char *file);
        int set_emulation(int d);
        int set_ink_density(int d);
        int set_cbm_charset(int d);
        int set_epson_charset(int d);
        int set_ibm_charset(int d);
        int set_output_type(int t);

        /* =======  Getters */
        int get_current_printer_address(void) { return last_printer_addr; }

    private:
        /* =======  Output file management */
        int open_file(void);
        int close_file(void);
};

extern IecPrinter iec_printer;

#endif /* IEC_PRINTER_H */

/****************************  END OF FILE  ****************************/
