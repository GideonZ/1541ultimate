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
#include "iec.h"
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

typedef struct PrinterEvent {
    t_printer_event_type type;
    uint8_t value;
} PrinterEvent_t;

/*======================================================================*/
/* Class IecPrinter                                                     */
/*======================================================================*/

class IecPrinter
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

    public:
        /* =======  Constructors */
        IecPrinter()
        {
            fm = FileManager::getFileManager();

            output_filename = NULL;
            f = NULL;
            mps = MpsPrinter::getMpsPrinter();
            buffer_pointer = 0;
            output_type = PRINTER_PNG_OUTPUT;
            init = true;

            /* Create the queue */
            queueHandle = xQueueCreate( 1, sizeof(PrinterEvent_t));

            /* Create the task, storing the handle. */
            xTaskCreate((TaskFunction_t) IecPrinter::task, "Virtual Printer",
                        configMINIMAL_STACK_SIZE, this,
                        tskIDLE_PRIORITY, &taskHandle);
        }

        /* =======  Destructors */
        virtual ~IecPrinter()
        {
            vTaskDelete(taskHandle);
            close_file();
        }

        int push_data(uint8_t b);
        int push_command(uint8_t b);
        int flush(void);
        int init_done(void);
        int reset(void);

        /* =======  Setters */
        int set_filename(const char *file);
        int set_emulation(int d);
        int set_ink_density(int d);
        int set_cbm_charset(int d);
        int set_epson_charset(int d);
        int set_ibm_charset(int d);
        int set_output_type(int t);

    private:
        /* =======  Output file management */
        int open_file(void);
        int close_file(void);
};

#endif /* IEC_PRINTER_H */

/****************************  END OF FILE  ****************************/
