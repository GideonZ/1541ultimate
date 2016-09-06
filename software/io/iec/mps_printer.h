/*
##########################################################################
##                                                                      ##
##   RENE GARCIA                        GNU General Public Licence V3   ##
##                                                                      ##
##########################################################################
##                                                                      ##
##      Project : MPS Emulator                                          ##
##      Class   : MpsPrinter                                            ##
##      Piece   : mps_printer.h                                         ##
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

#ifndef _MPS_PRINTER_H_
#define _MPS_PRINTER_H_

#ifndef NOT_ULTIMATE
#include "filemanager.h"
#include "task.h"
#endif

#include <stdint.h>
#include "lodepng.h"

/*******************************  Constants  ****************************/

#define MPS_PRINTER_PAGE_WIDTH              1984
#define MPS_PRINTER_PAGE_HEIGHT             2580
#define MPS_PRINTER_PAGE_DEPTH              2
#define MPS_PRINTER_PAGE_OFFSET_LEFT        32
#define MPS_PRINTER_PAGE_OFFSET_TOP         210
#define MPS_PRINTER_PAGE_PRINTABLE_WIDTH    1920
#define MPS_PRINTER_PAGE_PRINTABLE_HEIGHT   2160
#define MPS_PRINTER_HEAD_HEIGHT             27

#define MPS_PRINTER_MAX_HTABULATIONS        32
#define MPS_PRINTER_MAX_VTABULATIONS        32
#define MPS_PRINTER_MAX_VTABSTORES          8

#define MPS_PRINTER_BITMAP_SIZE             ((MPS_PRINTER_PAGE_WIDTH*MPS_PRINTER_PAGE_HEIGHT*MPS_PRINTER_PAGE_DEPTH+7)>>3)

#define MPS_PRINTER_MAX_BIM_SUB             256
#define MPS_PRINTER_MAX_SPECIAL             46

#define MPS_PRINTER_SCRIPT_NORMAL           0
#define MPS_PRINTER_SCRIPT_SUPER            2
#define MPS_PRINTER_SCRIPT_SUB              4

#ifdef NIOS
#define FS_ROOT "/USB/"
#else
#define FS_ROOT "/SD/"
#endif

/******************************  Debug macros  **************************/

/* Uncomment to enable debug messages to serial port */
//#define DEBUG

#ifdef DEBUG
#define DBGMSG(x) printf(__FILE__ " %d: " x "\n", __LINE__)
#define DBGMSGV(x, ...) printf(__FILE__ " %d: " x "\n", __LINE__, __VA_ARGS__)
#else
#define DBGMSG(x)
#define DBGMSGV(x, ...)
#endif

/*********************************  Types  ******************************/

typedef enum mps_printer_states {
    MPS_PRINTER_STATE_INITIAL,
    MPS_PRINTER_STATE_PARAM,
    MPS_PRINTER_STATE_ESC,
    MPS_PRINTER_STATE_ESC_PARAM,
    MPS_PRINTER_STATES
} mps_printer_state_t;

typedef enum mps_printer_interpreter {
    MPS_PRINTER_INTERPRETER_CBM,
    MPS_PRINTER_INTERPRETER_EPSONFX80,
    MPS_PRINTER_INTERPRETER_IBMPP,
    MPS_PRINTER_INTERPRETER_IBMGP,
    MPS_PRINTER_INTERPRETERS
} mps_printer_interpreter_t;

typedef enum mps_printer_step {
    MPS_PRINTER_STEP_PICA,
    MPS_PRINTER_STEP_ELITE,
    MPS_PRINTER_STEP_MICRO,
    MPS_PRINTER_STEP_CONDENSED,
    MPS_PRINTER_STEP_PICA_COMPRESSED,
    MPS_PRINTER_STEP_ELITE_COMPRESSED,
    MPS_PRINTER_STEP_MICRO_COMPRESSED,
    MPS_PRINTER_STEPS
} mps_printer_step_t;

/*======================================================================*/
/* Class MpsPrinter                                                     */
/*======================================================================*/

class MpsPrinter
{
    private:
        /* =======  Embeded data */
        /* Char Generators (bitmap definition of each character) */
        static uint8_t chargen_italic[129][12];
        static uint8_t chargen_nlq_low[404][12];
        static uint8_t chargen_nlq_high[404][12];
        static uint8_t chargen_draft[404][12];

        /* Italic chargen lookup table */
        static uint16_t convert_italic[404];

        /* Charsets (CBM and other ASCII) */
        static uint16_t charset_cbm[7][2][256];
        static uint16_t charset_epson[12][128];
        static uint16_t charset_ibm[7][256];
        static uint16_t charset_epson_extended[128];

        /* Dot spacing on X axis depending on character width (pical, elite, compressed,...) */
        static uint8_t spacing_x[7][26];

        /* Dot spacing on Y axis depending on character style (normal, superscript, subscript) */
        static uint8_t spacing_y[6][17];

        /* CBM character specia for quote mode */
        static uint8_t cbm_special[MPS_PRINTER_MAX_SPECIAL];

        /* =======  Configuration */
        /* PNG file basename */
        char outfile[32];

        /* PNG palette */
        LodePNGState lodepng_state;

        /* tabulation stops */
        uint16_t htab[MPS_PRINTER_MAX_HTABULATIONS];
        uint16_t vtab_store[MPS_PRINTER_MAX_VTABSTORES][MPS_PRINTER_MAX_VTABULATIONS];
        uint16_t *vtab;

        /* Page bitmap */
        uint8_t bitmap[MPS_PRINTER_BITMAP_SIZE];

        /* How many pages printed since start */
        int page_num;

        /* Current interpreter to use */
        mps_printer_interpreter_t interpreter;

        /* Current international charset */
        uint8_t charset;

        /* Saved international charsets */
        uint8_t cbm_charset;
        uint8_t epson_charset;
        uint8_t ibm_charset;
        bool epson_charset_extended;

        /* =======  Print head configuration */
        /* Ink density */
        uint8_t dot_size;

        /* Printer head position */
        uint16_t head_x;
        uint16_t head_y;

        /* Current interline value */
        uint16_t interline;
        uint16_t next_interline;

        /* Margins */
        uint16_t margin_right;
        uint16_t margin_left;
        uint16_t margin_top;
        uint16_t margin_bottom;

        /* BIM */
        uint8_t bim_density;
        uint8_t bim_K_density;  /* EPSON specific */
        uint8_t bim_L_density;  /* EPSON specific */
        uint8_t bim_Y_density;  /* EPSON specific */
        uint8_t bim_Z_density;  /* EPSON specific */
        uint8_t bim_position;
        bool bim_mode;

        /* =======  Current print attributes */
        bool reverse;           /* Negative characters */
        bool double_width;      /* Double width characters */
        bool nlq;               /* Near Letter Quality */
        bool clean;             /* Page is blank */
        bool quoted;            /* Commodore quoted listing */
        bool double_strike;     /* Print twice at the same place */
        bool underline;         /* Underline is on */
        bool overline;          /* Overline is on (not implemented yet) */
        bool bold;              /* Bold is on */
        bool italic;            /* Italic is on */
        bool auto_lf;           /* Automatic LF on CR (IBM Proprinter only) */

        /* =======  Current CBM charset variant (Uppercases/graphics or Lowercases/Uppercases) */
        uint8_t charset_variant;

        /* =======  Interpreter state */
        mps_printer_state_t state;
        uint16_t param_count;
        uint32_t param_build;
        uint8_t bim_sub[MPS_PRINTER_MAX_BIM_SUB];
        uint16_t bim_count;

        /* Current CMB command waiting for a parameter */
        uint8_t cbm_command;

        /* Current ESC command waiting for a parameter */
        uint8_t esc_command;

#ifndef NOT_ULTIMATE
        /* =======  1541 Ultimate FileManager */
        FileManager *fm;
        Path *path;
        uint8_t activity;
#endif
        /* =======  Current spacing configuration */
        uint8_t step;     /* X spacing */
        uint8_t script;   /* Y spacing */


        /*==============================================*/
        /*                 M E T H O D S                */
        /*==============================================*/
    private:
        /* =======  Constructors */
        MpsPrinter(char * filename = NULL);

        /* =======  Destructors */
        ~MpsPrinter(void);

    public:
        /* =======  End of current page */
        void FormFeed(void);

        /* =======  Singleton get */
        static MpsPrinter* getMpsPrinter();

        /* =======  Object customization */
        void setFilename(char * filename);
        void setCharsetVariant(uint8_t cs);
        void setDotSize(uint8_t ds);
        void setInterpreter(mps_printer_interpreter_t it);
        void setCBMCharset(uint8_t in);
        void setEpsonCharset(uint8_t in);
        void setIBMCharset(uint8_t in);

        /* =======  Feed interpreter */
        void Interpreter(const uint8_t * input, uint32_t size);

        /* =======  Reset printer */
        void Reset(void);

    private:
        uint8_t Combine(uint8_t c1, uint8_t c2);
        void Clear(void);
        void Init(void);
#ifndef NOT_ULTIMATE
        void calcPageNum(void);
#endif
        void Print(const char* filename);
        void Ink(uint16_t x, uint16_t y, uint8_t c=3);
        void Dot(uint16_t x, uint16_t y, bool b=false);
        uint16_t Charset2Chargen(uint8_t input);
        uint16_t Char(uint16_t c);
        uint16_t CharItalic(uint16_t c, uint16_t x, uint16_t y);
        uint16_t CharDraft(uint16_t c, uint16_t x, uint16_t y);
        uint16_t CharNLQ(uint16_t c, uint16_t x, uint16_t y);
        void PrintString(const char *s, uint16_t x, uint16_t y);
        void PrintStringNlq(const char *s, uint16_t x, uint16_t y);
        bool IsPrintable(uint8_t input);
        void ActivityLedOn(void);
        void ActivityLedOff(void);

        /* CBM related interpreter */
        void CBM_Interpreter(uint8_t input);
        uint16_t CBMBim(uint8_t head);
        bool IsCBMSpecial(uint8_t input);

        /* Epson FX80 related interpreter */
        void Epson_Interpreter(uint8_t input);
        uint16_t EpsonBim(uint8_t head);
        uint16_t EpsonBim9(uint8_t h,uint8_t l);

        /* IBM Proprinter related interpreter */
        void IBMpp_Interpreter(uint8_t input);
        uint16_t IBMBim(uint8_t head);

        /* IBM Gaphics Printer related interpreter */
        void IBMgp_Interpreter(uint8_t input);
};

#endif /* _MPS_PRINTER_H_ */

/****************************  END OF FILE  ****************************/
