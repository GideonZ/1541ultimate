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

/*********************************  Types  ******************************/

enum t_printer_output_type {
    PRINTER_PNG_OUTPUT=0,
    PRINTER_RAW_OUTPUT,
    PRINTER_ASCII_OUTPUT
};

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

        /* Printer buffer ant its pointer */
        uint8_t buffer[IEC_PRINTER_BUFFERSIZE];
        int  buffer_pointer;

        /* Output file descriptor */
        File *f;

        /* Selected output type (RAW, PNG, ASCII) */
        t_printer_output_type output_type;

        /* Flag set to true while in Ultimate init sequence */
        bool init;

    public:
        /*==============================================*/
        /*                 M E T H O D S                */
        /*==============================================*/

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
        }

        /* =======  Destructors */
        virtual ~IecPrinter()
        {
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
