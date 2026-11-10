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

/******************************  Includes  ****************************/

#ifndef IEC_PRINTER_H
#define IEC_PRINTER_H

#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "menu.h"
#include "config.h"
#include "userinterface.h"
#include "iec_interface.h"
#include "subsys.h"
#include "dump_hex.h"
#include "filemanager.h"
#include "mystring.h"
#include "mps_printer.h"
#include "printer_spool.h"

/*******************************  Constants  ****************************/

#define IEC_PRINTER_BUFFERSIZE  256
#define IEC_PRINTER_EVENT_CMD   0

/* How long the Flush/Eject menu item waits for the printer task */
#define IEC_PRINTER_FLUSH_WAIT_MS 20000

/* Max text lines in a page */
#define IEC_PRINTER_PAGE_LINES	70

/* Height of a line of text in pixels */
#define IEC_PRINTER_PIXLINE     36

/*********************************  Types  ******************************/

enum t_printer_output_type {
    PRINTER_UNSET_OUTPUT=0,
    PRINTER_PNG_OUTPUT,
    PRINTER_RAW_OUTPUT,
    PRINTER_ASCII_OUTPUT
};

/* Type of an event in the spool (bits 8-9; the value is in bits 0-7) */
enum t_printer_event_type {
    PRINTER_EVENT_CMD=0,
    PRINTER_EVENT_DATA
};


/*======================================================================*/
/* Class IecPrinter                                                     */
/*======================================================================*/

class IecPrinter : public IecSlave, SubSystem, ObjectWithMenu, ConfigurableObject
{
    private:
        /* Slot ID at registration with the interface */
        int slot_id;

        /* PETASCII to ASCII lookup table */
        static uint8_t ascii_lut[256];

        /* Ultimate IEC interface */
        IecInterface *interface;

        /* Ultimate filesystem interface */
        FileManager *fm;

        /* Output base filename (the printer task's own copy) */
        char output_filename_buf[64];
        const char *output_filename;

        /* Printer IEC address */
        int printer_iec_addr;

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

        /* Printer is color (only used for progress bar) */
        bool is_color;

        /* User interface descriptor */
        UserInterface *cmd_ui;

        /* The printer task itself */
        TaskHandle_t taskHandle;

        /* Spool that carries the IEC data, and the menu and settings markers, to the printer task */
        PrinterSpool spool;

        /* Binary semaphore to hold the action menu caller Task, given when a Flush/Eject is done */
        SemaphoreHandle_t xCallerSemaphore;

        /*==============================================*/
        /*                 M E T H O D S                */
        /*==============================================*/

        /* =======  Printter task related methods */
        static void task(IecPrinter *iecPrinter);
        t_channel_retval _push_data(uint8_t b);
        t_channel_retval _push_command(uint8_t b);
        int _reset(void);
        void apply_settings(const PrinterSettings &s);

        /* -------  User action menu */
        struct {
            Action *turn_on;
            Action *turn_off;
            Action *eject;
            Action *reset;
        } myActions;

    public:
        IecPrinter(void);
        ~IecPrinter(void);

        /* ====== IecSlave functions */
        // void reset(void); // FIXME
        t_channel_retval push_data(uint8_t b);
        t_channel_retval push_ctrl(uint16_t b);
        bool is_enabled(void) { return printer_enable; }
        uint8_t get_address(void) { return (uint8_t) printer_iec_addr; }
        uint8_t get_type(void) { return 0x50; }
        const char *iec_identify(void) { return "Printer Emulation"; }
        void info(JSON_Object *);
        void info(StreamTextLog&);

        int flush(void);
        /* =======  Interface menu */
        void create_task_items();
        void update_task_items(bool writablePath);
        void effectuate_settings(void); // from ConfigurableObject
        SubsysResultCode_e executeCommand(SubsysCommand *cmd); // from SubSystem
        const char *identify(void) { return "Virtual Printer"; }
        void updateFlushProgressBar(void);

        /* =======  Setters */
        int set_filename(const char *file);
        int set_emulation(int d);
        int set_ink_density(int d);
        int set_cbm_charset(int d);
        int set_epson_charset(int d);
        int set_ibm_charset(int d);
        int set_output_type(int t);
        int set_page_top(int d);
        int set_page_height(int d);

    private:
        /* =======  Output file management */
        int open_file(void);
        int close_file(void);
};

extern IecPrinter *iec_printer;

#endif /* IEC_PRINTER_H */

/****************************  END OF FILE  ****************************/
