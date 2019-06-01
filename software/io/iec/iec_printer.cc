/*
##########################################################################
##                                                                      ##
##   RENE GARCIA                        GNU General Public Licence V3   ##
##                                                                      ##
##########################################################################
##                                                                      ##
##      Project : MPS Emulator                                          ##
##      Class   : IecPrinter                                            ##
##      Piece   : iec_printer.cc                                        ##
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

#include "iec_printer.h"

/************************************************************************
*                                                                       *
*               G L O B A L   M O D U L E   V A R I A B L E S           *
*                                                                       *
************************************************************************/

/* =======  Lookup table for ASCII output (ISO 8859-1 to keep pound sign) */
uint8_t IecPrinter::ascii_lut[256] =
{//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
     0,  0,  0,  0,  0,  0,  0,  0,  0,  9, 10,  0,  0, 13,  0,  0, // 0
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, // 2
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, // 3
    64, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111, // 4
   112,113,114,115,116,117,118,119,120,121,122, 91,163, 93, 94, 95, // 5
    45, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, // 6
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 43, 32,124, 32, 32, // 7
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 8
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 9
    32, 32, 32, 45, 45,124, 32, 32, 32, 32,124, 43, 32, 43, 43, 32, // A
    43, 43, 43, 43,124, 32, 32, 45, 32, 32, 32, 32, 32, 43, 32, 32, // B
    45, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, // C
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 43, 32,124, 32, 32, // D
    32, 32, 32, 45, 45,124, 32, 32, 32, 32,124, 43, 32, 43, 43, 32, // E
    43, 43, 43, 43,124, 32, 32, 45, 32, 32, 32, 32, 32, 43, 32, 32  // F
};

/************************************************************************
*                       IecPrinter::push_data(b)                Pubic   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~                        *
* Function : Interpret one data byte received by data IEC channel       *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    b : (uint8_t) received data byte                                   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK on success or IEC_BYTE_LOST if an error occured             *
*                                                                       *
************************************************************************/

int IecPrinter::push_data(uint8_t b)
{
    if (output_type == PRINTER_ASCII_OUTPUT)
    {
        if ((buffer[buffer_pointer] = ascii_lut[b]))
            buffer_pointer++;
    }
    else
    {
        buffer[buffer_pointer++] = b;
    }

    if(buffer_pointer == IEC_PRINTER_BUFFERSIZE)
    {
        switch (output_type)
        {
            case PRINTER_PNG_OUTPUT:
                mps->Interpreter(buffer,IEC_PRINTER_BUFFERSIZE);
                buffer_pointer = 0;
                break;

            case PRINTER_RAW_OUTPUT:
            case PRINTER_ASCII_OUTPUT:
                if(!f)
                    open_file();

                if (f)
                {
                    uint32_t bytes;
                    f->write(buffer, IEC_PRINTER_BUFFERSIZE, &bytes);
                    buffer_pointer = 0;
                }
                else
                {
                    buffer_pointer--;
                    return IEC_BYTE_LOST;
                }

                break;
        }
    }

    return IEC_OK;
}

/************************************************************************
*                       IecPrinter::push_data(b)                Pubic   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~                        *
* Function : Interpret one data byte received by IEC command channel    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    b : (uint8_t) received command byte                                *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::push_command(uint8_t b)
{
    switch(b) {
        case 0xFE: // Received printer OPEN
        case 0x00: // CURSOR UP (graphics/upper case) mode (default)
            if (output_type == PRINTER_PNG_OUTPUT) {
                if (buffer_pointer)
                {
                    mps->Interpreter(buffer,buffer_pointer);
                    buffer_pointer=0;
                }
                mps->setCharsetVariant(0);
            }

            break;

        case 0x07: // CURSOR DOWN (upper case/lower case) mode
            if (output_type == PRINTER_PNG_OUTPUT) {
                if (buffer_pointer)
                {
                    mps->Interpreter(buffer,buffer_pointer);
                    buffer_pointer=0;
                }
                mps->setCharsetVariant(1);
            }

            break;

        case 0xFF: // Received EOI (Close file)
            if (output_type != PRINTER_PNG_OUTPUT) close_file();
            break;
    }

    return IEC_OK;
}

/************************************************************************
*                           IecPrinter::flush()                 Pubic   *
*                           ~~~~~~~~~~~~~~~~~~~                         *
* Function : Force remaining bytes in printer buffer to be output to    *
*            file in RAW and ASCII output mode                          *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::flush(void)
{
    if (buffer_pointer && output_type != PRINTER_PNG_OUTPUT && !f)
        open_file();

    close_file();

    return IEC_OK;
}

/************************************************************************
*                           IecPrinter::init_done()             Pubic   *
*                           ~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Tell IecPrinter that system init is done                   *
*            printer is disabled while system init                      *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::init_done(void)
{
    init = false;

    return IEC_OK;
}

/************************************************************************
*                           IecPrinter::reset()                 Pubic   *
*                           ~~~~~~~~~~~~~~~~~~~                         *
* Function : Reset printer emulation settings to defaults               *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::reset(void)
{
    buffer_pointer = 0;
    mps->Reset();

    return IEC_OK;
}

/************************************************************************
*                     IecPrinter::set_filename(file)            Pubic   *
*                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Set output filename for printer                            *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    file: (char *) filename (with absolute path)                       *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_filename(const char *file)
{
    output_filename = file;
    mps->setFilename((char *)output_filename);

    return IEC_OK;
}

/************************************************************************
*                       IecPrinter::set_emulation(d)            Pubic   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Set printer emulation (not RAW or ASCII)                   *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) Emulated printer to enable                                *
*         0 = Commodore MPS printer                                     *
*         1 = Epson FX-80 printer                                       *
*         2 = IBM Graphics Printer                                      *
*         3 = IBM Proprinter                                            *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_emulation(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    switch (d)
    {
        case 0: mps->setInterpreter(MPS_PRINTER_INTERPRETER_CBM); break;
        case 1: mps->setInterpreter(MPS_PRINTER_INTERPRETER_EPSONFX80); break;
        case 2: mps->setInterpreter(MPS_PRINTER_INTERPRETER_IBMGP); break;
        case 3: mps->setInterpreter(MPS_PRINTER_INTERPRETER_IBMPP); break;
    }

    return IEC_OK;
}

/************************************************************************
*                      IecPrinter::set_ink_density(d)           Pubic   *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Set ink density for printer emulation                      *
*            flush print buffer if not empty before changing density    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) ink density                                               *
*         0 = low                                                       *
*         1 = medium                                                    *
*         2 = high                                                      *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_ink_density(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    mps->setDotSize(d);

    return IEC_OK;
}

/************************************************************************
*                      IecPrinter::set_cbm_charset(d)           Pubic   *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Change commodore charset for MPS printer emulation         *
*            flush print buffer if not empty before changing charset    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) charset                                                   *
*         0 = USA/UK                  4 = Spain                         *
*         1 = Denmark                 5 = Sweden                        *
*         2 = France/Italy            6 = Switzerland                   *
*         3 = Germany                                                   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_cbm_charset(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    mps->setCBMCharset(d);

    return IEC_OK;
}

/************************************************************************
*                     IecPrinter::set_epson_charset(d)          Pubic   *
*                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                  *
* Function : Change epson charset for FX-80 printer emulation           *
*            flush print buffer if not empty before changing charset    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) charset                                                   *
*         0 = Basic                  6 = Sweden                         *
*         1 = USA                    7 = Italy                          *
*         2 = France                 8 = Spain                          *
*         3 = Germany                9 = Japan                          *
*         4 = UK                    10 = Norway                         *
*         5 = Denmark I             11 = Denmark II                     *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_epson_charset(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    mps->setEpsonCharset(d);

    return IEC_OK;
}

/************************************************************************
*                      IecPrinter::set_ibm_charset(d)           Pubic   *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Change ibm charset for GP and PP printer emulation         *
*            flush print buffer if not empty before changing charset    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) charset                                                   *
*         0 = International 1           3 = Greece                      *
*         1 = International 2           4 = Portugal                    *
*         2 = Israel                    5 = Spain                       *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_ibm_charset(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    mps->setIBMCharset(d);

    return IEC_OK;
}

/************************************************************************
*                      IecPrinter::set_output_type(t)           Pubic   *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Change virtual printer output type                         *
*            flush print buffer if type is different from current one   *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) charset                                                   *
*         0 = RAW                                                       *
*         1 = ASCII                                                     *
*         2 = PNG (greyscale printer emulation)                         *
*         3 = PNG (color printer emulation)                             *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_output_type(int t)
{
    t_printer_output_type new_output_type = output_type;

    switch (t)
    {
        case 0: // RAW output format
            new_output_type = PRINTER_RAW_OUTPUT;
            break;

        case 1: // ASCII output format
            new_output_type = PRINTER_ASCII_OUTPUT;
            break;

        case 2: // PNG grey output format
            new_output_type = PRINTER_PNG_OUTPUT;
            mps->setColorMode(false);
            break;

        case 3: // PNG color output format
            new_output_type = PRINTER_PNG_OUTPUT;
            mps->setColorMode(true);
            break;
    }

    if (!init && new_output_type != output_type)
        close_file();

    output_type = new_output_type;

    return IEC_OK;
}

/************************************************************************
*                           IecPrinter::open_file()           Private   *
*                           ~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Open file to write output data                             *
*            append to existing file or create file if not existing     *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (int) 0 on success, other value on failure                         *
*                                                                       *
************************************************************************/

int IecPrinter::open_file(void)
{
    char filename[40];

    /* Add .txt extension if ASCII output type */
    sprintf(filename,(output_type == PRINTER_ASCII_OUTPUT) ? "%s.txt" : "%s", output_filename);

    FRESULT fres = fm->fopen((const char *) NULL, filename, FA_WRITE|FA_OPEN_ALWAYS, &f);

    if(f)
    {
        printf("Successfully opened printer file %s\n", filename);
        f->seek(f->get_size());
    }
    else
    {
        FRESULT fres = fm->fopen((const char *) filename, FA_WRITE|FA_CREATE_NEW, &f);

        if(f)
        {
            printf("Successfully created printer file %s\n", filename);
        }
        else
        {
            printf("Can't open printer file %s: %s\n", filename, FileSystem :: get_error_string(fres));
            return 1;
        }
    }

    return 0;
}

/************************************************************************
*                           IecPrinter::close_file()          Private   *
*                           ~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Close output file (and flush data in printer buffer before *
*            closing file)                                              *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (int) 0 always                                                     *
*                                                                       *
************************************************************************/

int IecPrinter::close_file(void) // file should be open
{
    switch (output_type)
    {
        case PRINTER_PNG_OUTPUT:
            if (buffer_pointer > 0)
            {
                mps->Interpreter(buffer, buffer_pointer);
                buffer_pointer=0;
            }

            mps->FormFeed();

            break;

        case PRINTER_RAW_OUTPUT:
        case PRINTER_ASCII_OUTPUT:
            if(f)
            {
                if (buffer_pointer > 0)
                {
                    uint32_t bytes;

                    f->write(buffer, buffer_pointer, &bytes);
                    buffer_pointer = 0;
                }

                fm->fclose(f);
                f = NULL;
            }

            break;
    }

    return 0;
}

/****************************  END OF FILE  ****************************/
