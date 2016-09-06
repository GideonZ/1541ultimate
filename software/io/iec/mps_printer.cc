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
    lodepng_state.info_png.color.bitdepth   = 2;
    lodepng_state.info_raw.colortype        = LCT_PALETTE;
    lodepng_state.info_raw.bitdepth         = 2;

    /* Physical page description (A4 240x216 dpi) */
    lodepng_state.info_png.phys_defined     = 1;
    lodepng_state.info_png.phys_x           = 9448;
    lodepng_state.info_png.phys_y           = 8687;
    lodepng_state.info_png.phys_unit        = 1;

    /* I rule, you don't */
    lodepng_state.encoder.auto_convert      = 0;

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

    /* =======  Default configuration */
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
    bzero (bitmap,MPS_PRINTER_BITMAP_SIZE);
    head_x = margin_left;
    head_y = margin_top;
    clean  = true;
    quoted = false;
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
    if (path->get_directory(*infos) != FR_OK)
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

void
MpsPrinter::Print(const char * filename)
{
    uint8_t *buffer;
    size_t outsize;

    DBGMSG("start PNG encoder");
#ifndef NOT_ULTIMATE
    ActivityLedOn();
#endif
    buffer=NULL;
    unsigned error = lodepng_encode(&buffer, &outsize, bitmap, MPS_PRINTER_PAGE_WIDTH, MPS_PRINTER_PAGE_HEIGHT, &lodepng_state);
    DBGMSG("ended PNG encoder, now saving");
#ifndef NOT_ULTIMATE
    ActivityLedOff();

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
    }
#endif

    free(buffer);
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
        case 0:     // Density 0 : 1 single black point (diameter 1 pixel) mostly for debug
            Ink(x,y);
            break;

        case 1:     // Density 1 : 1 black point with gray around (looks like diameter 2)
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

        case 2:     // Density 2 : 4 black points with gray around (looks like diameter 3)
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
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    x : (uint16_t) pixel position from left of printable area of page  *
*    y : (uint16_t) pixel position from top of printable area of page   *
*    c : (uint8_t) grey level                                           *
*        0=black (default), 1=dark grey, 2=light grey                   *
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

    /* -------  Which byte address is it on raster buffer */
    uint32_t byte = ((ty*MPS_PRINTER_PAGE_WIDTH+tx)*MPS_PRINTER_PAGE_DEPTH)>>3;

    /* -------  Whitch bits on byte are coding the pixe (4 pixels per byte) */
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
*               MpsPrinter::Interprepter(buffer, size)          Public  *
*               ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                  *
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

#ifndef NOT_ULTIMATE
void
MpsPrinter::ActivityLedOn(void)
{
    if (activity == 0)
        ioWrite8(ITU_USB_BUSY, 1);

    activity++;
}
#endif

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

#ifndef NOT_ULTIMATE
void
MpsPrinter::ActivityLedOff(void)
{
    if (activity > 0)
    {
        activity--;

        if (activity == 0)
            ioWrite8(ITU_USB_BUSY, 0);
    }
}
#endif

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
