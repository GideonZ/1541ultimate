/*
##########################################################################
##                                                                      ##
##   RENE GARCIA                        GNU General Public Licence V3   ##
##                                                                      ##
##########################################################################
##                                                                      ##
##      Project : MPS Emulator                                          ##
##      Class   : MpsPrinter                                            ##
##      Piece   : mps_printer.cc                                        ##
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

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef NOT_ULTIMATE
#include <strings.h>
#endif
#include "lodepng.h"
#include "mps_printer.h"

/************************************************************************
*                                                                       *
*               G L O B A L   M O D U L E   V A R I A B L E S           *
*                                                                       *
************************************************************************/

/* =======  Horizontal pitch for letters */
uint8_t MpsPrinter::spacing_x[7][26] =
{
    {  0, 2, 4, 6, 8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50 },    // Pica              24px/char
    {  0, 2, 3, 5, 7, 8,10,12,13,15,17,18,20,22,23,25,27,28,30,32,33,35,37,38,40,42 },    // Elite             20px/char
    {  0, 1, 3, 4, 5, 7, 8, 9,11,12,13,15,16,17,19,20,21,23,24,25,27,28,29,31,32,33 },    // Micro             16px/char
    {  0, 1, 2, 3, 5, 6, 7, 8, 9,10,12,13,14,15,16,17,19,20,21,22,23,24,26,27,28,29 },    // Compressed        14px/char
    {  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25 },    // Pica Compressed   12px/char
    {  0, 1, 2, 2, 3, 4, 5, 6, 7, 7, 8, 9,10,11,12,12,13,14,15,16,17,17,18,19,20,21 },    // Elite Compressed  10px/char
    {  0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 9, 9,10,11,11,12,13,13,14,15,15,16,17 },    // Micro Compressed  8px/char
};

/* =======  Vertical pitch for sub/super-script */
uint8_t MpsPrinter::spacing_y[6][17] =
{
    {  0, 3, 6, 9,12,15,18,21,24,27,30,33,36,39,42,45,48 },    // Normal Draft & NLQ High
    {  2, 5, 8,11,14,17,20,23,26,28,32,35,38,41,44,47,50 },    // Normal NLQ Low
    {  0, 2, 3, 4, 6, 7, 8,10,12,13,14,16,17,18,20,21,22 },    // Superscript Draft & NLQ High
    {  1, 3, 4, 5, 7, 8, 9,11,13,14,15,17,18,19,21,22,23 },    // Superscript NLQ Low
    { 10,12,13,14,16,17,18,20,22,23,24,26,27,28,30,31,32 },    // Subscript Draft & NLQ High
    { 11,13,14,15,17,18,19,21,23,24,25,27,28,29,31,32,33 },    // Subscript NLQ Low
};

#ifndef TRUE_CMYK

/* =======  Color PNG Palette */

        /*-
         * Palette made by hand with photoshop and saved to .act file
         * CMYK with C,M,Y from values 0% 25% 50% 100%
         * K from values 0% 14% 45% 100%
         * black dot compensation applied for colors where C,M,Y = 0,0,0
         * in this table each value is RED then GREEN then BLUE on 8 bits
         * for 256 colors
        -*/

uint8_t MpsPrinter::rgb_palette[768] =
{
   0xFF, 0xFF, 0xFF, 0xB9, 0xE5, 0xFB, 0x6D, 0xCF, 0xF6, 0x00, 0xAE, 0xEF,
   0xF9, 0xCB, 0xDF, 0xBB, 0xB8, 0xDC, 0x7D, 0xA7, 0xD9, 0x00, 0x8F, 0xD5,
   0xF4, 0x9A, 0xC1, 0xBD, 0x8C, 0xBF, 0x87, 0x81, 0xBD, 0x00, 0x72, 0xBC,
   0xEC, 0x00, 0x8C, 0xBD, 0x1A, 0x8D, 0x92, 0x27, 0x8F, 0x2E, 0x31, 0x92,
   0xFF, 0xFB, 0xCC, 0xC0, 0xE2, 0xCA, 0x7A, 0xCC, 0xC8, 0x00, 0xAB, 0xC5,
   0xFB, 0xC8, 0xB4, 0xC1, 0xB6, 0xB3, 0x86, 0xA6, 0xB2, 0x00, 0x8E, 0xB0,
   0xF5, 0x98, 0x9D, 0xC0, 0x8C, 0x9C, 0x8D, 0x81, 0x9C, 0x00, 0x71, 0x9C,
   0xED, 0x09, 0x73, 0xBF, 0x1E, 0x74, 0x94, 0x29, 0x77, 0x32, 0x32, 0x7B,
   0xFF, 0xF7, 0x99, 0xC4, 0xDF, 0x9B, 0x82, 0xCA, 0x9C, 0x00, 0xA9, 0x9D,
   0xFD, 0xC6, 0x89, 0xC3, 0xB4, 0x8B, 0x8A, 0xA4, 0x8C, 0x00, 0x8C, 0x8D,
   0xF6, 0x96, 0x79, 0xC2, 0x8B, 0x7B, 0x8F, 0x80, 0x7D, 0x00, 0x70, 0x7E,
   0xED, 0x14, 0x5B, 0xBF, 0x24, 0x5E, 0x94, 0x2C, 0x61, 0x34, 0x34, 0x65,
   0xFF, 0xF2, 0x00, 0xCB, 0xDB, 0x2A, 0x8D, 0xC6, 0x3F, 0x00, 0xA6, 0x51,
   0xFF, 0xC2, 0x0E, 0xC8, 0xB1, 0x2F, 0x91, 0xA2, 0x3D, 0x00, 0x8A, 0x4B,
   0xFF, 0xA9, 0x17, 0xC5, 0x89, 0x2F, 0x94, 0x7F, 0x3A, 0x00, 0x6F, 0x45,
   0xED, 0x1C, 0x24, 0xC1, 0x27, 0x2D, 0x96, 0x2F, 0x34, 0x36, 0x36, 0x39,
   0xE0, 0xE0, 0xE0, 0xA3, 0xCA, 0xDD, 0x61, 0xB7, 0xD9, 0x00, 0x9A, 0xD3,
   0xD9, 0xB2, 0xC5, 0xA5, 0xA2, 0xC2, 0x6F, 0x94, 0xC0, 0x00, 0x7F, 0xBC,
   0xD5, 0x87, 0xAB, 0xA6, 0x7C, 0xA9, 0x78, 0x73, 0xA8, 0x00, 0x64, 0xA6,
   0xCF, 0x00, 0x7B, 0xA6, 0x10, 0x7C, 0x81, 0x1C, 0x7E, 0x28, 0x27, 0x81,
   0xE1, 0xDC, 0xB4, 0xA8, 0xC7, 0xB3, 0x6C, 0xB4, 0xB1, 0x00, 0x98, 0xAE,
   0xDC, 0xB0, 0x9F, 0xA9, 0xA1, 0x9E, 0x76, 0x93, 0x9D, 0x00, 0x7E, 0x9C,
   0xD7, 0x86, 0x8B, 0xA9, 0x7B, 0x8A, 0x7C, 0x72, 0x8A, 0x00, 0x64, 0x8A,
   0xCF, 0x00, 0x65, 0xA8, 0x16, 0x67, 0x82, 0x20, 0x69, 0x2C, 0x29, 0x6C,
   0xE3, 0xD9, 0x88, 0xAB, 0xC4, 0x89, 0x72, 0xB2, 0x8A, 0x00, 0x96, 0x8B,
   0xDD, 0xAE, 0x7A, 0xAB, 0x9F, 0x7B, 0x79, 0x91, 0x7C, 0x00, 0x7C, 0x7D,
   0xD7, 0x84, 0x6B, 0xAA, 0x7A, 0x6C, 0x7E, 0x71, 0x6E, 0x00, 0x63, 0x6F,
   0xD0, 0x0D, 0x4F, 0xA8, 0x1B, 0x52, 0x82, 0x23, 0x54, 0x2D, 0x2B, 0x58,
   0xE5, 0xD4, 0x00, 0xB1, 0xC0, 0x25, 0x7B, 0xAF, 0x37, 0x00, 0x93, 0x48,
   0xDE, 0xAA, 0x0E, 0xAF, 0x9C, 0x27, 0x7F, 0x8F, 0x34, 0x00, 0x7A, 0x42,
   0xD8, 0x82, 0x19, 0xAD, 0x78, 0x27, 0x82, 0x70, 0x31, 0x00, 0x62, 0x3C,
   0xD0, 0x18, 0x1F, 0xA9, 0x21, 0x25, 0x84, 0x27, 0x2A, 0x30, 0x2D, 0x30,
   0xA0, 0xA0, 0xA0, 0x73, 0x91, 0xA0, 0x43, 0x84, 0x9D, 0x00, 0x6F, 0x9A,
   0x9B, 0x7F, 0x8E, 0x76, 0x75, 0x8D, 0x4F, 0x6B, 0x8B, 0x00, 0x5A, 0x89,
   0x99, 0x5F, 0x7B, 0x78, 0x58, 0x7A, 0x55, 0x51, 0x7A, 0x00, 0x45, 0x79,
   0x95, 0x00, 0x58, 0x78, 0x00, 0x58, 0x5D, 0x00, 0x5A, 0x18, 0x0F, 0x5E,
   0x9F, 0x9D, 0x82, 0x77, 0x8F, 0x81, 0x4A, 0x82, 0x80, 0x00, 0x6E, 0x7E,
   0x9C, 0x7E, 0x73, 0x78, 0x73, 0x72, 0x52, 0x6A, 0x72, 0x00, 0x5A, 0x71,
   0x99, 0x5E, 0x63, 0x79, 0x57, 0x63, 0x58, 0x51, 0x63, 0x00, 0x46, 0x63,
   0x95, 0x00, 0x46, 0x79, 0x00, 0x48, 0x5D, 0x07, 0x4A, 0x1A, 0x12, 0x4D,
   0xA0, 0x9A, 0x61, 0x79, 0x8C, 0x62, 0x4E, 0x80, 0x63, 0x00, 0x6D, 0x64,
   0x9C, 0x7C, 0x56, 0x79, 0x72, 0x57, 0x54, 0x68, 0x58, 0x00, 0x59, 0x59,
   0x99, 0x5D, 0x4B, 0x79, 0x56, 0x4C, 0x58, 0x50, 0x4D, 0x00, 0x45, 0x4F,
   0x94, 0x00, 0x34, 0x78, 0x05, 0x37, 0x5C, 0x0D, 0x39, 0x1A, 0x15, 0x3D,
   0xA1, 0x97, 0x00, 0x7B, 0x89, 0x16, 0x52, 0x7D, 0x24, 0x00, 0x6C, 0x32,
   0x9D, 0x79, 0x00, 0x7A, 0x6F, 0x16, 0x57, 0x66, 0x20, 0x00, 0x58, 0x2C,
   0x99, 0x5B, 0x05, 0x79, 0x55, 0x14, 0x5A, 0x4F, 0x1D, 0x00, 0x45, 0x26,
   0x94, 0x07, 0x0A, 0x78, 0x0E, 0x0F, 0x5C, 0x13, 0x15, 0x1C, 0x18, 0x1C,
   0x00, 0x00, 0x00, 0x0C, 0x1A, 0x22, 0x00, 0x15, 0x22, 0x00, 0x06, 0x24,
   0x23, 0x0E, 0x15, 0x11, 0x06, 0x18, 0x00, 0x01, 0x19, 0x00, 0x01, 0x21,
   0x23, 0x00, 0x09, 0x16, 0x00, 0x10, 0x0A, 0x00, 0x17, 0x00, 0x00, 0x1E,
   0x29, 0x00, 0x03, 0x21, 0x00, 0x0F, 0x1A, 0x00, 0x15, 0x0E, 0x00, 0x1A,
   0x20, 0x1D, 0x12, 0x09, 0x19, 0x14, 0x00, 0x15, 0x15, 0x00, 0x06, 0x17,
   0x21, 0x0D, 0x05, 0x0F, 0x07, 0x08, 0x00, 0x01, 0x0B, 0x00, 0x01, 0x15,
   0x21, 0x00, 0x00, 0x15, 0x00, 0x02, 0x07, 0x00, 0x0B, 0x00, 0x01, 0x14,
   0x27, 0x00, 0x02, 0x1F, 0x00, 0x04, 0x18, 0x00, 0x0C, 0x0E, 0x00, 0x1A,
   0x1E, 0x1C, 0x00, 0x06, 0x18, 0x02, 0x06, 0x18, 0x02, 0x00, 0x05, 0x08,
   0x1F, 0x0C, 0x00, 0x0B, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
   0x1F, 0x00, 0x00, 0x11, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x09,
   0x25, 0x00, 0x01, 0x1D, 0x00, 0x01, 0x16, 0x00, 0x03, 0x07, 0x00, 0x0B,
   0x18, 0x1A, 0x00, 0x00, 0x18, 0x00, 0x00, 0x14, 0x00, 0x00, 0x05, 0x00,
   0x1A, 0x0C, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x1C, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x23, 0x00, 0x01, 0x1A, 0x00, 0x01, 0x12, 0x00, 0x00, 0x03, 0x00, 0x00
};

#endif /* TRUE_CMYK */

/************************************************************************
*               MpsPrinter::MpsPrinter(filename)          Constructor   *
*               ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                        *
* Function : MpsPrinter object default constructor with no argument     *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    filename : (char *) filename base, an indice and extension will be *
*               added, default is NULL if not provided                  *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

MpsPrinter::MpsPrinter(char * filename)
{
    DBGMSG("creation");
#ifndef NOT_ULTIMATE
    fm = FileManager :: getFileManager();
    path = fm->get_new_path("mps_printer");
    if (filename == NULL) filename = (char *) FS_ROOT "mps";
#else
    if (filename == NULL) filename = (char *) "mps";
#endif
    strcpy(outfile,filename);

    /* =======  BW/Color printer init (default is BW) */
    setColorMode(false, true);

    /* =======  Default configuration */

        /*-
         *
         *  Page num start from 1 but if a file
         *  already exists on the filesystem the
         *  page number will be updated to be
         *  greater than the existing one.
         *
         *  Hoping that user will delete or move
         *  old printed files before reaching 999
         *
        -*/

    page_num = 1;
    dot_size = 1;
    interpreter = MPS_PRINTER_INTERPRETER_CBM;
    epson_charset = cbm_charset = charset = 0;
    ibm_charset = 1;

    /* =======  Reset printer attributes */
    Reset();

#ifndef NOT_ULTIMATE
    activity = 0;
#endif
}

/************************************************************************
*                       MpsPrinter::~MpsPrinter()         Destructor    *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~                       *
* Function : Free all resources used by an MpsPrinter instance          *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

MpsPrinter::~MpsPrinter()
{
#ifndef NOT_ULTIMATE
    fm->release_path(path);
#endif
    lodepng_state_cleanup(&lodepng_state);
    DBGMSG("deletion");
}

/************************************************************************
*                       MpsPrinter::getMpsPrinter()       Singleton     *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Get pointer to the MpsPrinter singleton instance           *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (MpsPrinter *) Pointer to singleton instance of MpsPrinter         *
*                                                                       *
************************************************************************/

MpsPrinter *MpsPrinter::getMpsPrinter()
{
    // The singleton is here, static
    static MpsPrinter m_mpsInstance;
    return &m_mpsInstance;
}

/************************************************************************
*               MpsPrinter::setFilename(filename)               Public  *
*               ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                       *
* Function : Change base filename used for output PNG files. Page       *
*            counter is reset to 1                                      *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    filename : (char *) filename base, an indice and extension will be *
*               added                                                   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::setFilename(char * filename)
{
    DBGMSGV("filename changed to [%s]", filename);
    /* Store new filename */
    if (filename)
        strcpy(outfile,filename);

    /* Reset page counter */
    page_num = 1;
}

/************************************************************************
*                       MpsPrinter::setDotSize(ds)              Public  *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~                      *
* Function : Change ink dot size                                        *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    ds : (uint8_t) New dot size                                        *
*               0 - 1 pixe diameter like                                *
*               1 - 2 pixels diameter like                              *
*               2 - 3 pixels diameter like                              *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::setDotSize(uint8_t ds)
{
    if (ds >2) ds = 2;
    dot_size = ds;
    DBGMSGV("dotsize changed to %d", ds);
}

/************************************************************************
*                       MpsPrinter::setInterpreter(in)          Public  *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                  *
* Function : Change interpreter. Interpreter state is reset but head    *
*            stays at the same place and page is not cleared            *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    in : (mps_printer_interpreter_t) New interpreter                   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::setInterpreter(mps_printer_interpreter_t in)
{
    DBGMSGV("request to change interpreter to %d", in);

    /* Check if a change of interpreter is requested */
    if (in < MPS_PRINTER_INTERPRETERS && interpreter != in)
    {
        DBGMSGV("Changed interpreter to %d", in);
        /* Change interpreter */
        interpreter = in;

        /* Default interpreter state */
        Init();
    }
}

/************************************************************************
*                       MpsPrinter::Clear()               Private       *
*                       ~~~~~~~~~~~~~~~~~~~                             *
* Function : Clear page and set printer head on top left position       *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::Clear(void)
{
    DBGMSG("clear page bitmap");

    if (color_mode)
    {
        bzero (bitmap,MPS_PRINTER_BITMAP_SIZE_COLOR);
    }
    else
    {
        bzero (bitmap,MPS_PRINTER_BITMAP_SIZE_BW);
    }

    head_x = margin_left;
    head_y = margin_top;
    clean  = true;
    quoted = false;

#ifdef DEBUG
    /* DEBUG : Palette display on top of page */
    int x, y, i;

    for ( x=0; x<256; x++)
        for ( y=0; y<256; y++)
        {
            int c = (x >> 4) | (y & 0xF0);
            InkTest(x,y,c);
        }
#endif /* DEBUG */
}

/************************************************************************
*                       MpsPrinter::Reset()                 Public      *
*                       ~~~~~~~~~~~~~~~~~~~                             *
* Function : Set printer to initial state                               *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::Reset(void)
{
    DBGMSG("printer reset requested");

    /* =======  Set default interpreter settings */
    Init();

    /* -------  Clear the bitmap (all white) */
    Clear();
}

/************************************************************************
*                    MpsPrinter::setColorMode(mode,init)                *
*                    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                *
* Function : Set printer to color or black and white mode               *
*            the current page is lost                                   *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    mode: (bool) true for color, false for greyscale                   *
*    init: (bool) true if called from the printer constructor           *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::setColorMode(bool mode, bool init)
{
    DBGMSGV("interpreter set color mode to %d", mode);

    /* =======  Default printer attributes */

    /* Except on first call, don't do anything if config has not changed */
    if (!init && color_mode == mode)
        return;

    color_mode = mode;

    /* Except on first call, release resources of previous configuration */
    if (!init)
    {
        lodepng_state_cleanup(&lodepng_state);
    }

    /* Initialise PNG convertor attributes */
    lodepng_state_init(&lodepng_state);

    /* PNG compression settings */
    lodepng_state.encoder.zlibsettings.btype        = 2;
    lodepng_state.encoder.zlibsettings.use_lz77     = true;
    lodepng_state.encoder.zlibsettings.windowsize   = 1024;
    lodepng_state.encoder.zlibsettings.minmatch     = 3;
    lodepng_state.encoder.zlibsettings.nicematch    = 128;

    /* Initialise color palette for memory bitmap and file output */
    lodepng_palette_clear(&lodepng_state.info_png.color);
    lodepng_palette_clear(&lodepng_state.info_raw);

    if (color_mode)
    {
        /* =======  Color printer */
        uint16_t i;

            /*-
             *  Each color is coded on 2 bits (3 shades + white)
             *  bits 7,6 : black
             *  bits 4,5 : yellow
             *  bits 2,3 : magenta
             *  bits 0,1 : cyan
            -*/

#ifdef TRUE_CMYK
        /* Gamma curve */
        static double grey[4] = { 0.0, 1.0-224.0/255.0, 1.0-160.0/255.0, 1.0 };

        for (i=0; i<256; i++)
        {
            /* Convert cmyk to rgb */

            float c = grey[(i & 0x03)];
            float m = grey[((i>>2) & 0x03)];
            float y = grey[((i>>4) & 0x03)];
            float k = grey[((i>>6) & 0x03)];

            c = c * (1.0 - k) + k;
            m = m * (1.0 - k) + k;
            y = y * (1.0 - k) + k;

            uint8_t r = (uint8_t) ((1.0 - c) * 255.0);
            uint8_t g = (uint8_t) ((1.0 - m) * 255.0);
            uint8_t b = (uint8_t) ((1.0 - y) * 255.0);

            /* printf("%03d: #%02X%02X%02X - (%3d %3d %3d) %4.2f %4.2f %4.2f %4.2f - %d %d %d %d\n",
                i,
                r, g, b,
                r, g, b,
                c, m, y, k,
                i & 0x03,
                (i>>2) & 0x03,
                (i>>4) & 0x03,
                (i>>6) & 0x03);*/

#else
        int x=0;        /* index in RGB component palette */

        for (int i=0; i<256; i++)
        {
            uint8_t r = rgb_palette[x++];
            uint8_t g = rgb_palette[x++];
            uint8_t b = rgb_palette[x++];

#endif /* TRUE_CMYK */
            lodepng_palette_add(&lodepng_state.info_png.color, r, g, b, 255);
            lodepng_palette_add(&lodepng_state.info_raw, r, g, b, 255);
        }

        /* Bitmap uses 6 bit depth and a palette */
        lodepng_state.info_png.color.colortype  = LCT_PALETTE;
        lodepng_state.info_png.color.bitdepth   = MPS_PRINTER_PAGE_DEPTH_COLOR;
        lodepng_state.info_raw.colortype        = LCT_PALETTE;
        lodepng_state.info_raw.bitdepth         = MPS_PRINTER_PAGE_DEPTH_COLOR;
    }
    else
    {
        /* =======  Greyscale printer */

        /* White */
        lodepng_palette_add(&lodepng_state.info_png.color, 255, 255, 255, 255);
        lodepng_palette_add(&lodepng_state.info_raw, 255, 255, 255, 255);

        /* Light grey */
        lodepng_palette_add(&lodepng_state.info_png.color, 224, 224, 224, 255);
        lodepng_palette_add(&lodepng_state.info_raw, 224, 224, 224, 255);

        /* Dark grey */
        lodepng_palette_add(&lodepng_state.info_png.color, 160, 160, 160, 255);
        lodepng_palette_add(&lodepng_state.info_raw, 160, 160, 160, 255);

        /* Black */
        lodepng_palette_add(&lodepng_state.info_png.color, 0, 0, 0, 255);
        lodepng_palette_add(&lodepng_state.info_raw, 0, 0, 0, 255);

        /* Bitmap uses 2 bit depth and a palette */
        lodepng_state.info_png.color.colortype  = LCT_PALETTE;
        lodepng_state.info_png.color.bitdepth   = MPS_PRINTER_PAGE_DEPTH;
        lodepng_state.info_raw.colortype        = LCT_PALETTE;
        lodepng_state.info_raw.bitdepth         = MPS_PRINTER_PAGE_DEPTH;
    }

    /* Physical page description (A4 240x216 dpi) */
    lodepng_state.info_png.phys_defined     = 1;
    lodepng_state.info_png.phys_x           = 9448;
    lodepng_state.info_png.phys_y           = 8687;
    lodepng_state.info_png.phys_unit        = 1;

    /* I rule, you don't */
    lodepng_state.encoder.auto_convert      = 0;

    Clear();
}

/************************************************************************
*                       MpsPrinter::Init()                  Private     *
*                       ~~~~~~~~~~~~~~~~~~                              *
* Function : Set printer interpreter to default state but does not      *
*            clear the page                                             *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::Init(void)
{
    DBGMSG("interpreter init requested");

    /* =======  Default tabulation stops */
    for (int i=0; i<MPS_PRINTER_MAX_HTABULATIONS; i++)
        htab[i] = 168+i*24*8;

    for (int j=0; j<MPS_PRINTER_MAX_VTABSTORES; j++)
        for (int i=0; i<MPS_PRINTER_MAX_VTABULATIONS; i++)
            vtab_store[j][i] = 0;

    vtab = vtab_store[0];

    /* =======  Default printer attributes */
    step            = 0;
    script          = MPS_PRINTER_SCRIPT_NORMAL;
    interline       = 36;
    next_interline  = interline;
    charset_variant = 0;
    bim_density     = 0;
    color           = MPS_PRINTER_COLOR_BLACK;
    italic          = false;
    underline       = false;
    overline        = false;
    double_width    = false;
    bold            = false;
    nlq             = false;
    double_strike   = false;
    auto_lf         = false;
    bim_mode        = false;
    state           = MPS_PRINTER_STATE_INITIAL;
    margin_left     = 0;
    margin_top      = 0;
    margin_right    = MPS_PRINTER_PAGE_PRINTABLE_WIDTH;
    margin_bottom   = MPS_PRINTER_PAGE_PRINTABLE_HEIGHT - MPS_PRINTER_HEAD_HEIGHT;
    bim_K_density   = 0;    /* EPSON specific 60 dpi */
    bim_L_density   = 1;    /* EPSON specific 120 dpi */
    bim_Y_density   = 2;    /* EPSON specific 120 dpi high speed */
    bim_Z_density   = 3;    /* EPSON specific 240 dpi */

    /* =======  Default charsets (user defined) */

    epson_charset_extended = false;

    switch (interpreter)
    {
        case MPS_PRINTER_INTERPRETER_EPSONFX80:
            charset = epson_charset;
            break;

        case MPS_PRINTER_INTERPRETER_IBMPP:
        case MPS_PRINTER_INTERPRETER_IBMGP:
            charset = 0;
            break;

        case MPS_PRINTER_INTERPRETER_CBM:
            charset = cbm_charset;
    }
}

/************************************************************************
*                       MpsPrinter::calcPageNum()               Public  *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~                       *
* Function : Find existing print files in directory and get max index   *
*            to set file num for next print                             *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

#ifndef NOT_ULTIMATE
void
MpsPrinter::calcPageNum(void)
{
    char dirname[40];
    char *last_slash = NULL;
    char *basename = NULL;

    /* CHANGE THIS, it's ugly but it's fast */

    DBGMSGV("calculate page_num. I think it's %d", page_num);
    strcpy(dirname, outfile);
    for (int i=0; dirname[i]; i++)
        if (dirname[i] == '/') last_slash = &dirname[i];

    if (dirname)
    {
        *last_slash = '\0';
        basename = last_slash + 1;
    }
    else
    {
        /* No '/', set dirname to current '.' dir (legal in ultimate ?) */
        dirname[0] = '.';
        dirname[0] = '\0';
        basename = outfile;
    }

    path->cd(dirname);
    IndexedList<FileInfo *> *infos = new IndexedList<FileInfo *>(8, NULL);
    if (fm->get_directory(path, *infos, NULL) != FR_OK)
    {
        delete infos;
    }
    else
    {
        int baselength = strlen(basename);

        for(int i=0;i<infos->get_elements();i++)
        {
            FileInfo *inf = (*infos)[i];

            if (!strncmp(basename, inf->lfname, baselength))
            {
                /* Basename matches, then look if rest of filename is -XXX.png */
                if (inf->lfname[baselength] != '-') continue;
                if (inf->lfname[baselength+1] < '0') continue;
                if (inf->lfname[baselength+1] > '9') continue;
                if (inf->lfname[baselength+2] < '0') continue;
                if (inf->lfname[baselength+2] > '9') continue;
                if (inf->lfname[baselength+3] < '0') continue;
                if (inf->lfname[baselength+3] > '9') continue;
                if (inf->lfname[baselength+4] != '.') continue;
                if (inf->lfname[baselength+5] != 'p') continue;
                if (inf->lfname[baselength+6] != 'n') continue;
                if (inf->lfname[baselength+7] != 'g') continue;
                if (inf->lfname[baselength+8] != '\0') continue;

                /* If we are here, it maches, get the number */
                int number = (inf->lfname[baselength+1] - '0') * 100 +
                             (inf->lfname[baselength+2] - '0') * 10 +
                             (inf->lfname[baselength+3] - '0');

                if (number >= page_num) page_num = number+1;
            }

            delete inf; // FileInfo not needed anymore
        }

        delete infos;   // deletes the indexed list, FileInfos already deleted
    }
    DBGMSGV("calculate page_num. Finally it's %d", page_num);
}

#endif

/************************************************************************
*                       MpsPrinter::FormFeed()            Public        *
*                       ~~~~~~~~~~~~~~~~~~~~~~                          *
* Function : Save current page to PNG file and clear page               *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::FormFeed(void)
{
    char filename[40];

    if (!clean)
    {
#ifndef NOT_ULTIMATE
        calcPageNum();
#endif
        sprintf(filename,"%s-%03d.png", outfile, page_num);
        page_num++;
#ifdef NOT_ULTIMATE
        printf("printing to file %s\n", filename);
#else
        DBGMSGV("printing to file %s", filename);
#endif
        Print(filename);
    }

    Clear();
}

/************************************************************************
*                       MpsPrinter::Print(filename)       Private       *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Save current page to PNG finename provided                 *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    filename : (char *) filename to save PNG file to                   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

int
MpsPrinter::Print(const char * filename)
{
    uint8_t *buffer;
    size_t outsize;

    DBGMSG("start PNG encoder");
    ActivityLedOn();
    buffer=NULL;
    unsigned error = lodepng_encode(&buffer, &outsize, bitmap, MPS_PRINTER_PAGE_WIDTH, MPS_PRINTER_PAGE_HEIGHT, &lodepng_state);
    DBGMSG("ended PNG encoder, now saving");
    ActivityLedOff();

    if (error) {
        free(buffer);
        return -1;
    }

    int retval = 0;
#ifndef NOT_ULTIMATE
    File *f;
    FRESULT fres = fm->fopen((const char *) filename, FA_WRITE|FA_CREATE_NEW, &f);
    if (f) {
        uint32_t bytes;
        f->write(buffer, outsize, &bytes);
        fm->fclose(f);
        DBGMSG("PNG saved");
    }
    else
    {
        DBGMSG("Saving PNG failed\n");
        retval = -2;
    }
#else
    int fhd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
    if (fhd) {
        write(fhd, buffer, outsize);
        close(fhd);
    }
    else
    {
        printf("Saving file failed\n");
        retval = -2;
    }
#endif

    free(buffer);
    return retval;
}

/************************************************************************
*                             MpsPrinter::Dot(x,y,b)          Private   *
*                             ~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Prints a single dot on page, dot size depends on dentity   *
*            setting. If position is out of printable area, no dot is   *
*            printed                                                    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    x : (uint16_t) pixel position from left of printable area of page  *
*    y : (uint16_t) pixel position from top of printable area of page   *
*    b : (bool) true if DOT is part of BIM. No double-strike or bold    *
*        treatment is applied if true                                   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::Dot(uint16_t x, uint16_t y, bool b)
{
    /* =======  Check if position is out of range */

    if ( x > MPS_PRINTER_PAGE_PRINTABLE_WIDTH  ||
         y > MPS_PRINTER_PAGE_PRINTABLE_HEIGHT ) return;

    /* =======  Draw dot */

    switch (dot_size)
    {
        case 0:     // Density 0 : 1 single full color point (diameter 1 pixel) mostly for debug
            Ink(x,y);
            break;

        case 1:     // Density 1 : 1 full color point with shade around (looks like diameter 2)
            Ink(x,y);
            Ink(x-1,y-1,1);
            Ink(x+1,y+1,1);
            Ink(x-1,y+1,1);
            Ink(x+1,y-1,1);
            Ink(x,y-1,2);
            Ink(x,y+1,2);
            Ink(x-1,y,2);
            Ink(x+1,y,2);
            break;

        case 2:     // Density 2 : 4 full color points with shade around (looks like diameter 3)
        default:
            Ink(x,y);
            Ink(x,y+1);
            Ink(x+1,y);
            Ink(x+1,y+1);

            Ink(x-1,y-1,1);
            Ink(x+2,y-1,1);
            Ink(x-1,y+2,1);
            Ink(x+2,y+2,1);

            Ink(x,y-1,2);
            Ink(x+1,y-1,2);
            Ink(x,y+1,2);
            Ink(x-1,y,2);
            Ink(x-1,y+1,2);
            Ink(x+2,y,2);
            Ink(x+2,y+1,2);
            Ink(x,y+2,2);
            Ink(x+1,y+2,2);
            break;
    }


    if (!b)   /* This is not BIM related, we can double strike and bold */
    {
        /* -------  If double strike is ON, draw a second dot just to the right of the first one */
        if (bold) Dot(x+2,y,true);
        if (double_strike)
        {
            Dot(x,y+1,true);
            if (bold) Dot(x+2,y+1,true);
        }
    }

    /* -------  Now we know that the page is not blank */
    clean  = false;
}

/************************************************************************
*                             MpsPrinter::Ink(x,y,c)          Private   *
*                             ~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Add ink on a single pixel position. If ink as already been *
*            added on this position it will add more ink to be darker   *
*            On color printer, use the current color ribbon for ink     *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    x : (uint16_t) pixel position from left of printable area of page  *
*    y : (uint16_t) pixel position from top of printable area of page   *
*    c : (uint8_t) shade level                                          *
*        0=full (default), 1=dark shade, 2=light shade                  *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::Ink(uint16_t x, uint16_t y, uint8_t c)
{
    /* =======  Calculate true x and y position on page */
    uint16_t tx=x+MPS_PRINTER_PAGE_OFFSET_LEFT;
    uint16_t ty=y+MPS_PRINTER_PAGE_OFFSET_TOP;
    uint8_t current;

    if (color_mode)
    {
        /* =======  Color printer mode, each pixel is coded with 8 bits */
        /* -------  Which byte address is it on raster buffer */
        uint32_t byte = ((ty*MPS_PRINTER_PAGE_WIDTH+tx)*MPS_PRINTER_PAGE_DEPTH_COLOR)>>3;

        /* -------  Which bits on byte are coding the color (1 pixel per byte) */
        switch (color)
        {
            case MPS_PRINTER_COLOR_BLACK:
                current = (bitmap[byte] >> 6) & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0x3F;
                bitmap[byte] |= c << 6;
                break;

            case MPS_PRINTER_COLOR_YELLOW:
                current = (bitmap[byte] >> 4) & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xCF;
                bitmap[byte] |= c << 4;
                break;

            case MPS_PRINTER_COLOR_MAGENTA:
                current = (bitmap[byte] >> 2) & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xF3;
                bitmap[byte] |= c << 2;
                break;

            case MPS_PRINTER_COLOR_CYAN:
                current = bitmap[byte] & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xFC;
                bitmap[byte] |= c;
                break;

            case MPS_PRINTER_COLOR_VIOLET:      // CYAN + MAGENTA
                // CYAN
                current = bitmap[byte] & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xFC;
                bitmap[byte] |= c;
                // MAGENTA
                current = (bitmap[byte] >> 2) & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xF3;
                bitmap[byte] |= c << 2;
                break;

            case MPS_PRINTER_COLOR_ORANGE:      // MANGENTA + YELLOW
                // MAGENTA
                current = (bitmap[byte] >> 2) & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xF3;
                bitmap[byte] |= c << 2;
                // YELLOW
                current = (bitmap[byte] >> 4) & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xCF;
                bitmap[byte] |= c << 4;
                break;

            case MPS_PRINTER_COLOR_GREEN:       // CYAN + YELLOW
                // CYAN
                current = bitmap[byte] & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xFC;
                bitmap[byte] |= c;
                // YELLOW
                current = (bitmap[byte] >> 4) & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xCF;
                bitmap[byte] |= c << 4;
                break;
        }
    }
    else
    {
        /* =======  Greyscale printer mode, each pixel is coded with 2 bits */
        /* -------  Which byte address is it on raster buffer */
        uint32_t byte = ((ty*MPS_PRINTER_PAGE_WIDTH+tx)*MPS_PRINTER_PAGE_DEPTH)>>3;

        /* -------  Whitch bits on byte are coding the pixel (4 pixels per byte) */
        uint8_t sub = tx & 0x3;
        c &= 0x3;

        /* =======  Add ink to the pixel */
        switch (sub)
        {
            case 0:
                current = (bitmap[byte] >> 6) & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0x3F;
                bitmap[byte] |= c << 6;
                break;

            case 1:
                current = (bitmap[byte] >> 4) & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xCF;
                bitmap[byte] |= c << 4;
                break;

            case 2:
                current = (bitmap[byte] >> 2) & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xF3;
                bitmap[byte] |= c << 2;
                break;

            case 3:
                current = bitmap[byte] & 0x03;
                c = Combine(c, current);
                bitmap[byte] &= 0xFC;
                bitmap[byte] |= c;
                break;
        }
    }
}

/* ONLY FOR COLOR DEBUG */
#ifdef DEBUG
void
MpsPrinter::InkTest(uint16_t x, uint16_t y, uint8_t c)
{
    uint16_t tx=x+MPS_PRINTER_PAGE_OFFSET_LEFT;
    uint16_t ty=y+MPS_PRINTER_PAGE_OFFSET_TOP;

    if (color_mode)
    {
        uint32_t byte = ((ty*MPS_PRINTER_PAGE_WIDTH+tx)*MPS_PRINTER_PAGE_DEPTH_COLOR)>>3;
        bitmap[byte]=c;
    }
}
#endif /* DEBUG */

/************************************************************************
*                       MpsPrinter::Combine(c1,c2)            Private   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~                      *
* Function : Combine two grey levels and give the resulting grey        *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    c1 : (int) first grey level (0 is white, 3 is black)               *
*    c2 : (int) second grey level (0 is white, 3 is black)              *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (uint8_t) resulting grey level (in range range 0-3)                *
*                                                                       *
************************************************************************/

uint8_t
MpsPrinter::Combine(uint8_t c1, uint8_t c2)
{
        /*-
         *
         *   Very easy to understand :
         *
         *   white      + white         = white
         *   white      + light grey    = light grey
         *   white      + dark grey     = dark grey
         *   light grey + light grey    = dark grey
         *   light grey + dark grey     = black
         *   dark grey  + dark grey     = black
         *   black      + *whatever*    = black
         *
        -*/

    uint8_t result = c1 + c2;
    if (result > 3) result = 3;
    return result;
}

/************************************************************************
*                       MpsPrinter::CharItalic(c,x,y)         Private   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Print a single italic draft quality character              *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    c : (uint16_t) char from italic chargen table                      *
*    x : (uint16_t) first pixel position from left of printable area    *
*    y : (uint16_t) first pixel position from top of printable area     *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (uint16_t) printed char width (in pixels)                          *
*                                                                       *
************************************************************************/

uint16_t
MpsPrinter::CharItalic(uint16_t c, uint16_t x, uint16_t y)
{
    /* =======  Check is chargen is in italic chargen range */
    if (c > 128) return 0;

    uint8_t lst_head = 0;         // Last printed pattern to calculate reverse
    uint8_t shift = chargen_italic[c][11] & 1;    // 8 down pins from 9 ?

    /* =======  For each value of the pattern */
    for (int i=0; i<(double_width?24:12); i++)
    {
        uint8_t cur_head;

        if (double_width)
        {
            if (i&1)
                cur_head = 0;
            else
            {
                cur_head = (i>>1 == 11) ? 0 : chargen_italic[c][i>>1];
                if (i>1) cur_head |= chargen_italic[c][(i>>1)-1];
            }
        }
        else
        {
            cur_head = (i == 11) ? 0 : chargen_italic[c][i];
        }

        /* -------  Reverse is negative printing */
        if (reverse)
        {
            uint8_t saved_head = cur_head;
            cur_head = cur_head | lst_head;
            cur_head ^= 0xFF;
            lst_head = saved_head;
        }

        /* Calculate x position according to width and stepping */
        int dx = x + spacing_x[step][i];

        /* -------  Each dot to print (LSB is up) */
        for (int j=0; j<8; j++)
        {
            /* pin 9 is used for underline, can't be used on shifted chars */
            if (underline && shift && script == MPS_PRINTER_SCRIPT_NORMAL && j==7) continue;

            /* Need to print a dot ?*/
            if (cur_head & 0x01)
            {
                /* vertical position according to stepping for normal, subscript or superscript */
                int dy = y+spacing_y[script][j+shift];

                /* The dot itself */
                Dot(dx, dy);
            }

            cur_head >>= 1;
        }

        /* -------  Need to underline ? */

        /* Underline is one dot every 2 pixels */
        if (!(i&1) && underline)
        {
            int dy = y+spacing_y[MPS_PRINTER_SCRIPT_NORMAL][8];
            Dot(dx, dy);
        }
    }

    /* =======  This is the width of the printed char */
    return spacing_x[step][12]<<(double_width?1:0);
}

/************************************************************************
*                       MpsPrinter::CharDraft(c,x,y)          Private   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Print a single regular draft quality character             *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    c : (uint16_t) char from draft chargen table                       *
*    x : (uint16_t) first pixel position from left of printable area    *
*    y : (uint16_t) first pixel position from top of printable area     *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (uint16_t) printed char width (in pixels)                          *
*                                                                       *
************************************************************************/

uint16_t
MpsPrinter::CharDraft(uint16_t c, uint16_t x, uint16_t y)
{
    /* =======  Check is chargen is in draft chargen range */
    if (c > 403) return 0;

    uint8_t lst_head = 0; // Last printed pattern to calculate reverse
    uint8_t shift = chargen_draft[c][11] & 1;     // 8 down pins from 9 ?

    /* =======  For each value of the pattern */
    for (int i=0; i<(double_width?24:12); i++)
    {
        uint8_t cur_head;

        if (double_width)
        {
            if (i&1)
                cur_head = 0;
            else
            {
                cur_head = (i>>1 == 11) ? 0 : chargen_draft[c][i>>1];
                if (i>1) cur_head |= chargen_draft[c][(i>>1)-1];
            }
        }
        else
        {
            cur_head = (i == 11) ? 0 : chargen_draft[c][i];
        }

        /* -------  Reverse is negative printing */
        if (reverse)
        {
            uint8_t saved_head = cur_head;
            cur_head = cur_head | lst_head;
            cur_head ^= 0xFF;
            lst_head = saved_head;
        }

        /* Calculate x position according to width and stepping */
        int dx = x + spacing_x[step][i];

        /* -------  Each dot to print (LSB is up) */
        for (int j=0; j<8; j++)
        {
            /* pin 9 is used for underline, can't be used on shifted chars */
            if (underline && shift && script == MPS_PRINTER_SCRIPT_NORMAL && j==7) { cur_head >>= 1; continue; }
            if (overline && j==(shift?3:4)) { cur_head >>= 1; continue; }

            /* Need to print a dot ?*/
            if (cur_head & 0x01)
            {
                /* vertical position according to stepping for normal, subscript or superscript */
                int dy = y+spacing_y[script][j+shift];

                /* The dot itself */
                Dot(dx, dy);
            }

            cur_head >>= 1;
        }

        /* -------  Need to underline ? */

        /* Overline is one dot every 2 pixels */
        if (!(i&1) && overline)
        {
            int dy = y+spacing_y[script][4];
            Dot(dx, dy);
        }

        /* Underline is one dot every 2 pixels */
        if (!(i&1) && underline)
        {
            int dy = y+spacing_y[MPS_PRINTER_SCRIPT_NORMAL][8];
            Dot(dx, dy);
        }
    }

    /* =======  If the char is completed by a second chargen below, go print it */
    if (chargen_draft[c][11] & 0x80)
    {
        CharDraft((chargen_draft[c][11] & 0x70) >> 4, x, y+spacing_y[script][shift+8]);
    }

    /* =======  This is the width of the printed char */
    return spacing_x[step][12]<<(double_width?1:0);
}

/************************************************************************
*                       MpsPrinter::CharNLQ(c,x,y)            Private   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~                      *
* Function : Print a single regular NLQ character                       *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    c : (uint16_t) char from draft nlq (high and low) table            *
*    x : (uint16_t) first pixel position from left of printable area    *
*    y : (uint16_t) first pixel position from top of printable area     *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (uint16_t) printed char width (in pixels)                          *
*                                                                       *
************************************************************************/

uint16_t
MpsPrinter::CharNLQ(uint16_t c, uint16_t x, uint16_t y)
{
    /* =======  Check is chargen is in NLQ chargen range */
    if (c > 403) return 0;

    uint8_t lst_head_low = 0;     // Last low printed pattern to calculate reverse
    uint8_t lst_head_high = 0;    // Last high printed pattern to calculate reverse
    uint8_t shift = chargen_nlq_high[c][11] & 1;  // 8 down pins from 9 ?

    /* =======  For each value of the pattern */
    for (int i=0; i<(double_width?24:12); i++)
    {
        uint8_t cur_head_high;
        uint8_t cur_head_low;

        /* -------  Calculate last column */
        if (double_width)
        {
            cur_head_high = (i>>1 == 11) ? 0 : chargen_nlq_high[c][i>>1];
            if (i>1) cur_head_high |= chargen_nlq_high[c][(i>>1)-1];

            cur_head_low = (i>>1 == 11) ? 0 : chargen_nlq_low[c][i>>1];
            if (i>1) cur_head_low |= chargen_nlq_low[c][(i>>1)-1];
        }
        else
        {
            if (i==11)
            {
                if (chargen_nlq_high[c][11] & 0x04)
                {
                    /* Repeat last colums */
                    cur_head_high = chargen_nlq_high[c][10];
                }
                else
                {
                    /* Blank column */
                    cur_head_high = 0;
                }

                if (chargen_nlq_low[c][11] & 0x04)
                {
                    /* Repeat last colums */
                    cur_head_high = chargen_nlq_low[c][10];
                }
                else
                {
                    /* Blank column */
                    cur_head_low = 0;
                }
            }
            else
            {
                /* Not on last column, get data from chargen table */
                cur_head_high = chargen_nlq_high[c][i];
                cur_head_low = chargen_nlq_low[c][i];
            }
        }

        /* -------  Reverse is negative printing */
        if (reverse)
        {
            uint8_t saved_head_high = cur_head_high;
            uint8_t saved_head_low = cur_head_low;

            cur_head_high = cur_head_high | lst_head_high;
            cur_head_high ^= 0xFF;
            lst_head_high = saved_head_high;

            cur_head_low = cur_head_low | lst_head_low;
            cur_head_low ^= 0xFF;
            lst_head_low = saved_head_low;
        }

        /* -------  First we start with the high pattern */
        uint8_t head = cur_head_high;

        /* Calculate x position according to width and stepping */
        int dx = x + spacing_x[step][i];

        /* -------  Each dot to print (LSB is up) */
        for (int j=0; j<8; j++)
        {
            /* Need to print a dot ?*/
            if (head & 0x01)
            {
                /* vertical position according to stepping for normal, subscript or superscript */
                int dy = y+spacing_y[script][j+shift];

                /* The dot itself */
                Dot(dx, dy);
            }

            head >>= 1;
        }

        /* -------  Now we print the low pattern */
        head = cur_head_low;

        /* -------  Each dot to print (LSB is up) */
        for (int j=0; j<8; j++)
        {
            /* pin 9 on low pattern is used for underline, can't be used on shifted chars */
            if (underline && shift && script == MPS_PRINTER_SCRIPT_NORMAL && j==7) continue;

            /* Need to print a dot ?*/
            if (head & 0x01)
            {
                /* vertical position according to stepping for normal, subscript or superscript */
                int dy = y+spacing_y[script+1][j+shift];

                /* The dot itself */
                Dot(dx, dy);
            }

            head >>= 1;
        }

        /* -------  Need to underline ? */
        /* Underline is one dot every pixel on NLQ quality */
        if (underline)
        {
            int dy = y+spacing_y[MPS_PRINTER_SCRIPT_NORMAL+1][8];
            Dot(dx, dy);
        }
    }

    /* =======  If the char is completed by a second chargen below, go print it */
    if (chargen_nlq_high[c][11] & 0x80)
    {
        CharNLQ((chargen_nlq_high[c][11] & 0x70) >> 4, x, y+spacing_y[script][shift+8]);
    }

    /* =======  This is the width of the printed char */
    return spacing_x[step][12]<<(double_width?1:0);
}

/************************************************************************
*                MpsPrinter::Interpreter(buffer, size)          Public  *
*                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                  *
* Function : Interpret a set of data as sent by the computer            *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    buffer : (uint8_t *) Buffer to interpret                           *
*    size   : (uint32_t) bytes in buffer to interpret                   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::Interpreter(const uint8_t * input, uint32_t size)
{
    DBGMSGV("Interpreter called with %d bytes", size);
#ifndef NOT_ULTIMATE
    ActivityLedOn();
#endif
    /* =======  Call the right Interpreter method on each byte */
    while(size-- > 0)
    {
        switch (interpreter)
        {
            case MPS_PRINTER_INTERPRETER_EPSONFX80:
                Epson_Interpreter(*input);
                break;

            case MPS_PRINTER_INTERPRETER_IBMPP:
                IBMpp_Interpreter(*input);
                break;

            case MPS_PRINTER_INTERPRETER_IBMGP:
                IBMgp_Interpreter(*input);
                break;

            case MPS_PRINTER_INTERPRETER_CBM:
                CBM_Interpreter(*input);
                break;

            default:
                DBGMSGV("Unknown interpreter %d called", interpreter);
        }

        input++;
    }
#ifndef NOT_ULTIMATE
    ActivityLedOff();
#endif
}

/************************************************************************
*                       MpsPrinter::IsPrintable(c)              Private *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~                      *
* Function : Tell if an char code is printable or not                   *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    c : (uint8_t) Byte code to test                                    *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (bool) true if printable                                           *
*                                                                       *
************************************************************************/

bool
MpsPrinter::IsPrintable(uint8_t input)
{
    bool result = false;

    /* In charset Tables, non printables are coded 500 */

    switch (interpreter)
    {
        case MPS_PRINTER_INTERPRETER_EPSONFX80:
            if (epson_charset_extended)
            {
                result = (charset_epson_extended[input&0x7F] == 500) ? false : true;
            }
            if (!result) result = (charset_epson[charset][input&0x7F] == 500) ? false : true;
            break;

        case MPS_PRINTER_INTERPRETER_IBMPP:
        case MPS_PRINTER_INTERPRETER_IBMGP:
            result = (charset_ibm[charset][input] == 500) ? false : true;
            break;

        case MPS_PRINTER_INTERPRETER_CBM:
        default:
            result = (charset_cbm[charset][charset_variant][input] == 500) ? false : true;
    }

    return result;
}

/************************************************************************
*                       MpsPrinter::Charset2Chargen(c)          Private *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                  *
* Function : Give the chargen code of a charset character               *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    c : (uint8_t) Byte code to convert                                 *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (uint16_t) chargen code.                                           *
*  will be 500 if non printable                                         *
*  will be >=1000 if italic (sub 1000 to get italic chargen code)       *
*                                                                       *
************************************************************************/

uint16_t
MpsPrinter::Charset2Chargen(uint8_t input)
{
    uint16_t chargen_id = 500;

    /* =======  Read char entry in charset */
    switch (interpreter)
    {
        case MPS_PRINTER_INTERPRETER_EPSONFX80:
            if (epson_charset_extended)
            {
                chargen_id = charset_epson_extended[input&0x7F];
            }
            /* Get chargen entry for this char in Epson charset */
            if (chargen_id == 500) chargen_id = charset_epson[charset][input&0x7F];
            break;

        case MPS_PRINTER_INTERPRETER_IBMPP:
        case MPS_PRINTER_INTERPRETER_IBMGP:
            /* Get chargen entry for this char in IBM charset */
            chargen_id = charset_ibm[charset][input];
            break;

        case MPS_PRINTER_INTERPRETER_CBM:
        default:
            /* Get chargen entry for this char in Commodore charset */
            chargen_id = charset_cbm[charset][charset_variant][input];
    }

    /* =======  Italic is enabled, get italic code if not 500 */
    /* In EPSON, ASCII codes from 128-255 are the same as 0-127 but with italic enabled */
    if ((italic || (interpreter == MPS_PRINTER_INTERPRETER_EPSONFX80 && input & 0x80))
        && convert_italic[chargen_id] != 500)
    {
        /* Add 1000 to result to tell the drawing routine that this is from italic chargen */
        chargen_id = convert_italic[chargen_id] + 1000;
    }

    return chargen_id;
}

/************************************************************************
*                       MpsPrinter::Char(c)                     Private *
*                       ~~~~~~~~~~~~~~~~~~~                             *
* Function : Print a char on a page it will call Italic, Draft or NLQ   *
*            char print method depending on current configuration       *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    c : (uint16_t) Chargen code to print (+1000 if italic)             *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (uint16_t) printed char width                                      *
*                                                                       *
************************************************************************/

uint16_t
MpsPrinter::Char(uint16_t c)
{
    /* 1000 is italic offset */
    if (c >= 1000)
    {
        /* call Italic print method */
        return CharItalic(c-1000, head_x, head_y);
    }
    else
    {
        if (nlq)
        {
            /* call NLQ print method */
            return CharNLQ(c, head_x, head_y);
        }
        else
        {
            /* call NLQ print method */
            return CharDraft(c, head_x, head_y);
        }
    }
}

/************************************************************************
*                   MpsPrinter::ActivityLedOn(void)             Private *
*                   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Turn on the activity LED to show that printer is working   *
*            (calls can be nested)                                      *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::ActivityLedOn(void)
{
#ifndef NOT_ULTIMATE
    if (activity == 0)
        ioWrite8(ITU_USB_BUSY, 1);

    activity++;
#endif
}

/************************************************************************
*                   MpsPrinter::ActivityLedOff(void)            Private *
*                   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Turn off the activity LED to show that printer is not      *
*            working anymore (calls can be nested)                      *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::ActivityLedOff(void)
{
#ifndef NOT_ULTIMATE
    if (activity > 0)
    {
        activity--;

        if (activity == 0)
            ioWrite8(ITU_USB_BUSY, 0);
    }
#endif
}

/*
void
MpsPrinter::PrintString(const char *s, uint16_t x, uint16_t y)
{
    uint8_t *c = (uint8_t*) s;

    while (*c)
    {
        MpsPrinter::CharDraft(charset_epson[0][*c], x, y);
        x+=spacing_x[step][12];
        c++;
    }
}

void
MpsPrinter::PrintStringNlq(const char *s, uint16_t x, uint16_t y)
{
    uint8_t *c = (uint8_t*) s;

    while (*c)
    {
        MpsPrinter::CharNLQ(charset_epson[0][*c], x, y);
        x+=spacing_x[step][12];
        c++;
    }
}
*/

/****************************  END OF FILE  ****************************/
