/*
##########################################################################
##                                                                      ##
##   RENE GARCIA                        GNU General Public Licence V3   ##
##                                                                      ##
##########################################################################
##                                                                      ##
##      Project : MPS Emulator                                          ##
##      Class   : MpsPrinter                                            ##
##      Piece   : mps_printer_epson.cc                                  ##
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
#include "mps_printer.h"

/************************************************************************
*                                                                       *
*               G L O B A L   M O D U L E   V A R I A B L E S           *
*                                                                       *
************************************************************************/


/************************************************************************
*                      MpsPrinter::setEpsonCharset(cs)          Public  *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                  *
* Function : Change international charset for Epson emulation           *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    ds : (uint8_t) New charset                                         *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::setEpsonCharset(uint8_t cs)
{
    /* Check charset range */
    if (cs > 11)
        return;

    DBGMSGV("request to change EPSON charset to %d", cs);

    /* If charset change and emulation is now for this printer */
    if (cs != epson_charset && interpreter == MPS_PRINTER_INTERPRETER_EPSONFX80)
    {
        charset = cs;
        DBGMSGV("current charset set to %d (EPSON Emulation)", charset);
    }

    /* record charset */
    epson_charset = cs;
}

/************************************************************************
*                       MpsPrinter::EpsonBim(c,x,y)             Private *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Print a single bitmap image record (epson standard)        *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    c : (uint8_t) record to print                                      *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (uint16_t) printed width (in pixels)                               *
*                                                                       *
************************************************************************/

uint16_t
MpsPrinter::EpsonBim(uint8_t head)
{
    /* =======  Horizontal steps to simulate the 7 pitches */
    static uint8_t density_tab[7][3] =
    {
        { 4, 4, 4 },    /* 60 dpi */
        { 2, 2, 2 },    /* 120 dpi */
        { 2, 2, 2 },    /* 120 dpi high speed */
        { 1, 1, 1 },    /* 240 dpi */
        { 3, 3, 3 },    /* 80 dpi */
        { 3, 4, 3 },    /* 72 dpi */
        { 3, 2, 3 }     /* 90 dpi */
    };

    /* -------  Each dot to print (LSB is up) */
    for (int j=0; j<8; j++)
    {
        /* Need to print a dot ?*/
        if (head & 0x80)
        {
            Dot(head_x, head_y+spacing_y[MPS_PRINTER_SCRIPT_NORMAL][j], true);
        }

        head <<= 1;
    }

    /* Return horizontal spacing */
    return density_tab[bim_density][bim_position++%3];
}

/************************************************************************
*                       MpsPrinter::EpsonBim9(c,x,y)            Private *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Print a single bitmap image record (epson standard)        *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    ch : (uint8_t) record to print 8 up needles                        *
*    cl : (uint8_t) record to print last down needle                    *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (uint16_t) printed width (in pixels)                               *
*                                                                       *
************************************************************************/

uint16_t
MpsPrinter::EpsonBim9(uint8_t head, uint8_t low)
{
    /* =======  Horizontal steps to simulate the 7 pitches */
    static uint8_t density_tab[2] = { 4, 2 };    /* 60, 120 dpi */

    /* -------  Each dot to print (LSB is up) */
    for (int j=0; j<8; j++)
    {
        /* Need to print a dot ?*/
        if (head & 0x80)
        {
            Dot(head_x, head_y+spacing_y[MPS_PRINTER_SCRIPT_NORMAL][j], true);
        }

        head <<= 1;
    }

    if (low)
    {
        Dot(head_x, head_y+spacing_y[MPS_PRINTER_SCRIPT_NORMAL][8], true);
    }

    /* Return horizontal spacing */
    return density_tab[bim_density];
}

/************************************************************************
*                  MpsPrinter::Epson_Interpreter(data)         Private  *
*                  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                  *
* Function : Epson FX-80 single data interpreter automata               *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    data : (uint8_t *) Single data to interpret                        *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::Epson_Interpreter(uint8_t input)
{
    switch(state)
    {
        case MPS_PRINTER_STATE_INITIAL:

            /* =======  Select action if command char received */
            cbm_command = input;
            param_count = 0;
            switch(input)
            {
                case 0x08:   // Backspace
                    {
                        uint16_t cwidth = Char(Charset2Chargen(' '));
                        if (cwidth <= head_x)
                            head_x -= cwidth;
                        else
                            head_x = 0;
                    }
                    break;

                case 0x09:   // TAB: horizontal tabulation
                    {
                        int i = 0;
                        while (i < MPS_PRINTER_MAX_HTABULATIONS && htab[i] < MPS_PRINTER_PAGE_PRINTABLE_WIDTH)
                        {
                            if (htab[i] > head_x)
                            {
                                head_x = htab[i];
                                break;
                            }

                            i++;
                        }
                    }
                    break;

                case 0x0A:   // LF: line feed (LF+CR)
                    head_y += interline;
                    head_x  = margin_left;
                    if (head_y > margin_bottom)
                        FormFeed();
                    break;

                case 0x0B:   // VT: vertical tabulation
                    if (vtab[0] == 0)
                    {
                        /* If vertical tab stops are not defined, VT does only LF */
                        head_y += interline;
                        if (head_y > margin_bottom)
                            FormFeed();
                    }
                    else
                    {
                        int i = 0;
                        while (i < MPS_PRINTER_MAX_VTABULATIONS && vtab[i] < MPS_PRINTER_PAGE_PRINTABLE_HEIGHT)
                        {
                            if (vtab[i] > head_y)
                            {
                                head_y = vtab[i];
                                break;
                            }

                            i++;
                        }
                    }
                    break;

                case 0x0C:   // FF: form feed
                    FormFeed();
                    break;

                case 0x0D:   // CR: carriage return (CR only, no LF)
                    head_x  = margin_left;
                    break;

                case 0x0E:   // SO: Double width printing ON
                    double_width = true;
                    break;

                case 0x0F:   // SI: 17.1 chars/inch on
                    step = MPS_PRINTER_STEP_CONDENSED;
                    break;

                //case 0x11:   // DC1: Printer select
                    // ignore
                  //  break;

                case 0x12:   // DC2: 17.1 chars/inch off
                    step = MPS_PRINTER_STEP_PICA;
                    break;

                //case 0x13:   // DC3: Printer suspend
                    // ignore
                    //break;

                case 0x14:   // DC4: Double width printing off
                    double_width = false;
                    break;

                //case 0x18:   // CAN: Clear print buffer
                    // ignored
                    //break;

                case 0x1B:   // ESC: ASCII code for escape
                    state = MPS_PRINTER_STATE_ESC;
                    break;

                case 0x7F:   // DEL: Clear last printable character
                    // ignore
                    break;

                default:    // maybe a printable character
                    if (IsPrintable(input))
                    {
                        head_x += Char(Charset2Chargen(input));
                        if (head_x > margin_right)
                        {
                            head_y += interline;
                            head_x  = margin_left;

                            if (head_y > margin_bottom)
                                FormFeed();
                        }
                    }
                    break;
            }
            break;

        // =======  Escape sequences
        case MPS_PRINTER_STATE_ESC:
            esc_command = input;
            param_count = 0;
            switch (input)
            {
                case 0x0E:   // ESC SO: Double width printing on
                    double_width = true;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x0F:   // ESC SI: 17.1 chars/inch on
                    step = MPS_PRINTER_STEP_CONDENSED;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x21:  // ESC ! : Select graphics layout types
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x23:  // ESC # : Clear bit 7 forcing (MSB)
                    // ignored
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x25:  // ESC % : Select RAM (special characters) and ROM (standard characters)
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x26:  // ESC & : Define spacial characters in RAM
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x2A:  // ESC * : Set graphics layout in diferent density
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x2D:  // ESC - : Underline on/off
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x2F:  // ESC / : Vertical TAB stops program
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x30:  // ESC 0 : Spacing = 1/8"
                    interline = 27;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x31:  // ESC 1 : Spacing = 7/72"
                    interline = 21;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x32:  // ESC 2 : Spacing = 1/6"
                    interline = 36;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x33:  // ESC 3 : Spacing = n/216"
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x34:  // ESC 4 : Italic ON
                    italic = true;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x35:  // ESC 5 : Italic OFF
                    italic = false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x36:  // ESC 6 : Extend printable character set
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignore
                    break;

                case 0x37:  // ESC 7 : Select basic national characters table
                    charset = 0;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x38:  // ESC 8 : Out of paper detection disabled
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignored
                    break;

                case 0x39:  // ESC 9 : Out of paper detection enabled
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignored
                    break;

                case 0x3A:  // ESC : :  Copy standard character generator (ROM) into RAM
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x3C:  // ESC < : Set left to right printing for one line
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignore
                    break;

                case 0x3D:  // ESC = : Force bit 7 (MSB) to "0"
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignore
                    break;

                case 0x3E:  // ESC > : Force bit 7 (MSB) to "1"
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignore
                    break;

                case 0x3F:  // ESC ? : Change BIM density selected by graphics commands
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x40:  // ESC @ : Initialise printer (main reset)
                    Init();
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x41:  // ESC A : Spacing = n/72"
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x42:  // ESC B : Vertical TAB stops program
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x43:  // ESC C : Set form length
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x44:  // ESC D : Horizontal TAB stops program
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x45:  // ESC E : Emphasized printing ON
                    bold = true;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x46:  // ESC F : Emphasized printing OFF
                    bold = false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x47:  // ESC G : NLQ Printing ON
                    double_strike = true;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x48:  // ESC H : NLQ Printing OFF
                    double_strike = false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x49:  // ESC I : Extend printable characters set
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x4A:  // ESC J : Skip n/216" of paper
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x4B:  // ESC K : Set normal density graphics
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x4C:  // ESC L : Set double density graphics
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x4D:  // ESC M : Print pitch ELITE ON
                    step = MPS_PRINTER_STEP_ELITE;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x4E:  // ESC N : Defines bottom of from (BOF) in lines
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x4F:  // ESC O : Clear bottom of from (BOF)
                    // Ignored in this version, usefull only for continuous paper feed
                    break;

                case 0x50:  // ESC P : Print pitch ELITE OFF
                    step = MPS_PRINTER_STEP_PICA;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x51:  // ESC Q : Define right margin
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x52:  // ESC R : Select national character set
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x53:  // ESC S : Superscript/subscript printing
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x54:  // ESC T : Clear superscript/subscript printing
                    state = MPS_PRINTER_STATE_INITIAL;
                    script = MPS_PRINTER_SCRIPT_NORMAL;
                    break;

                case 0x55:  // ESC U : Mono/Bidirectional printing
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x57:  // ESC W : Double width characters ON/OFF
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x59:  // ESC Y : Double dentity BIM selection, normal speed
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x5A:  // ESC Z : Four times density BIM selection
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x5E:  // ESC ^ : 9-dot high strips BIM printing
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x62:  // ESC b : Select up to 8 vertical tab stops programs
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x69:  // ESC i : Immediate character printing ON/OFF
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x6A:  // ESC j : Reverse paper feed n/216"
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x6C:  // ESC l : Define left margin
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x70:  // ESC p : Proportional spacing ON/OFF
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x73:  // ESC s : Half speed printing ON/OFF
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x78:  // ESC x : DRAFT/NLQ print mode selection
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x7E:  // ESC ~ : MPS-1230 extension
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                default:
                    DBGMSGV("undefined Epson printer escape sequence %d", input);
                    state = MPS_PRINTER_STATE_INITIAL;
            }

            break;

        // =======  Escape sequence parameters
        case MPS_PRINTER_STATE_ESC_PARAM:
            param_count++;
            switch(esc_command)
            {
                case 0x21:  // ESC ! : Select graphics layout types
                    step = MPS_PRINTER_STEP_PICA;
                    if (input & 4) step = MPS_PRINTER_STEP_CONDENSED;
                    if (input & 1) step = MPS_PRINTER_STEP_ELITE;
                    underline = (input & 0x80) ? true : false;
                    italic = (input & 0x40) ? true : false;
                    double_width = (input & 0x20) ? true : false;
                    double_strike = (input & 0x10) ? true : false;
                    bold = (input & 0x08) ? true : false;
                    //porportional = (input & 0x02) ? true : false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x25:  // ESC % : Select RAM (special characters) and ROM (standard characters)
                    // ignore
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x26:  // ESC & : Define spacial characters in RAM
                    /* Fisrt parameter has to be '0' */
                    if (param_count == 1 && input != '0') state = MPS_PRINTER_STATE_INITIAL;
                    /* Second parameter is ascii code of first redefined character */
                    if (param_count == 2) param_build = input;
                    /* Thirs parameter is ascii code of last redefined character */
                    if (param_count == 3)
                    {
                        /* If firest is greater than last, error */
                        if (param_build > input)
                            state = MPS_PRINTER_STATE_INITIAL;
                        else
                            /* Otherwise calculate amout of data to be uploaded */
                            param_build = (input - param_build + 1) * 12 + 3;
                    }
                    /* ignore, skip uploaded data */
                    if (param_count >= param_build) state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x2A:  // ESC * : Set graphics layout in diferent density
                    /* First parameter is density */
                    if (param_count == 1)
                    {
                        bim_density  = (input<7) ? input : 0;
                        bim_position = 0;
                    }
                    /* Second parameter is data length LSB */
                    if (param_count == 2) param_build = input;
                    /* Third parameter is data length MSB */
                    if (param_count == 3) param_build |= input<<8;
                    /* Follows the BIM data */
                    if (param_count>3)
                    {
                        printf("BIM 0x%02X %d\n",input,param_count);
                        head_x += EpsonBim(input);
                        if (param_count - 3 >= param_build)
                            state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                case 0x2D:  // ESC - : Underline on/off
                    underline = input & 0x01 ? true : false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x2F:  // ESC / : Vertical TAB stops program
                    if (input < MPS_PRINTER_MAX_VTABSTORES)
                    {
                        vtab = vtab_store[input];
                    }
                    break;

                case 0x33:  // ESC 3 : Spacing = n/216"
                    interline = input;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x3A:  // ESC : :  Copy standard character generator (ROM) into RAM
                    if (param_count == 3)
                        state = MPS_PRINTER_STATE_INITIAL;
                    // ignored
                    break;

                case 0x3F:  // ESC ? : Change BIM density selected by graphics commands
                    /* BIM mode to change */
                    if (param_count == 1) param_build = input;
                    /* New density for selected mode */
                    if (param_count == 2)
                    {
                        input &= 0x07;

                        switch (param_build)
                        {
                            case 'K': bim_K_density = input; break;
                            case 'L': bim_L_density = input; break;
                            case 'Y': bim_Y_density = input; break;
                            case 'Z': bim_Z_density = input; break;
                        }

                        state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                case 0x41:  // ESC A : Spacing = n/72"
                    interline = input * 3;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x42:  // ESC B : Vertical TAB stops program
                    if (input == 0)
                        state = MPS_PRINTER_STATE_INITIAL;
                    else
                    {
                        if ((param_count > 1 && input < param_build) || param_count > MPS_PRINTER_MAX_VTABULATIONS)
                            state = MPS_PRINTER_STATE_INITIAL;
                        else
                        {
                            param_build = input;
                            vtab[param_count-1] = input * interline;
                        }
                    }
                    break;

                case 0x43:  // ESC C : Set form length
                    if (param_count == 1 && input != 0)
                    {
                        margin_bottom = input * interline;
                        state = MPS_PRINTER_STATE_INITIAL;
                    }
                    else if (param_count > 1)
                    {
                        if (input > 0 && input < 23)
                            margin_bottom = input * 216;

                        state = MPS_PRINTER_STATE_INITIAL;
                    }

                    if (margin_bottom > MPS_PRINTER_PAGE_PRINTABLE_HEIGHT - MPS_PRINTER_HEAD_HEIGHT)
                        margin_bottom = MPS_PRINTER_PAGE_PRINTABLE_HEIGHT - MPS_PRINTER_HEAD_HEIGHT;

                    break;

                case 0x44:  // ESC D : Horizontal TAB stops program
                    if (input == 0)
                        state = MPS_PRINTER_STATE_INITIAL;
                    else
                    {
                        if ((param_count > 1 && input < param_build) || param_count > MPS_PRINTER_MAX_HTABULATIONS)
                            state = MPS_PRINTER_STATE_INITIAL;
                        else
                        {
                            param_build = input;
                            htab[param_count-1] = input * spacing_x[step][12];
                        }
                    }
                    break;

                case 0x49:  // ESC I : Extend printable characters set
                    epson_charset_extended = (input & 1) ? true : false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x4A:  // ESC J : Skip n/216" of paper
                    head_y += input;
                    if (head_y > margin_bottom)
                        FormFeed();
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x4B:  // ESC K : Set normal density graphics
                    /* First parameter is data length LSB */
                    if (param_count == 1)
                    {
                        param_build = input;
                        bim_density  = bim_K_density;
                        bim_position = 0;
                    }
                    /* Second parameter is data length MSB */
                    if (param_count == 2) param_build |= input<<8;
                    /* Follows the BIM data */
                    if (param_count>2)
                    {
                        head_x += EpsonBim(input);
                        if (param_count - 2 >= param_build)
                            state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                case 0x4C:  // ESC L : Set double density graphics
                    /* First parameter is data length LSB */
                    if (param_count == 1)
                    {
                        param_build = input;
                        bim_density  = bim_L_density;
                        bim_position = 0;
                    }
                    /* Second parameter is data length MSB */
                    if (param_count == 2) param_build |= input<<8;
                    /* Follows the BIM data */
                    if (param_count>2)
                    {
                        head_x += EpsonBim(input);
                        if (param_count - 2 >= param_build)
                            state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                case 0x4E:  // ESC N : Defines bottom of from (BOF)
                    // Ignored in this version, usefull only for continuous paper feed
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x51:  // ESC Q : Define right margin
                    margin_right = input * spacing_x[step][12];
                    if (margin_right <= margin_left || margin_right > MPS_PRINTER_PAGE_PRINTABLE_WIDTH)
                        margin_right = MPS_PRINTER_PAGE_PRINTABLE_WIDTH;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x52:  // ESC R : Select national character set
                    if (input == '0') input = 0;
                    if (input < 11) charset = input+1;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x53:  // ESC S : Superscript/subscript printing
                    script = input & 0x01 ? MPS_PRINTER_SCRIPT_SUB : MPS_PRINTER_SCRIPT_SUPER;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x55:  // ESC U : Mono/Bidirectional printing
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignore
                    break;

                case 0x57:  // ESC W : Double width characters ON/OFF
                    double_width = (input & 1) ? true : false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x59:  // ESC Y : Double dentity BIM selection, normal speed
                    /* First parameter is data length LSB */
                    if (param_count == 1)
                    {
                        param_build = input;
                        bim_density  = bim_Y_density;
                        bim_position = 0;
                    }
                    /* Second parameter is data length MSB */
                    if (param_count == 2) param_build |= input<<8;
                    /* Follows the BIM data */
                    if (param_count>2)
                    {
                        head_x += EpsonBim(input);
                        if (param_count - 2 >= param_build)
                            state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                case 0x5A:  // ESC Z : Four times density BIM selection
                    /* First parameter is data length LSB */
                    if (param_count == 1)
                    {
                        param_build = input;
                        bim_density  = bim_Z_density;
                        bim_position = 0;
                    }
                    /* Second parameter is data length MSB */
                    if (param_count == 2) param_build |= input<<8;
                    /* Follows the BIM data */
                    if (param_count>2)
                    {
                        head_x += EpsonBim(input);
                        if (param_count - 2 >= param_build)
                            state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                case 0x5E:  // ESC ^ : 9-dot high strips BIM printing
                    /* First parameter is density */
                    if (param_count == 1)
                    {
                        /* Only density 0 & 1 are allowed */
                        bim_density  = input & 0x01;
                        bim_position = 0;
                    }
                    /* Second parameter is data length LSB */
                    if (param_count == 2) param_build = input;
                    /* Third parameter is data length MSB */
                    if (param_count == 3) param_build |= input<<8;
                    /* Follows the BIM data */
                    if (param_count > 3)
                    {
                        static uint8_t keep;

                        if (param_count & 0x01)
                        {
                            head_x += EpsonBim9(keep, input);
                        }
                        else
                        {
                            keep = input;
                        }

                        if (param_count >= param_build +3)
                            state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                case 0x62:  // ESC b : Select up to 8 vertical tab stops programs
                    if (param_count == 1) param_build = input;
                    else
                    {
                        if (input == 0)
                            state = MPS_PRINTER_STATE_INITIAL;
                        else
                        {
                            if ((param_count > 2 && (input * interline) < vtab_store[param_build][param_count-3]) || param_count-1 > MPS_PRINTER_MAX_VTABULATIONS)
                                state = MPS_PRINTER_STATE_INITIAL;
                            else
                            {
                                vtab_store[param_build][param_count-2] = input * interline;
                            }
                        }
                    }
                    break;

                case 0x69:  // ESC i : Immediate character printing ON/OFF
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignored
                    break;

                case 0x6A:  // ESC j : Reverse paper feed n/216"
                    if (head_y >= input) head_y -= input;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x6C:  // ESC l : Define left margin
                    margin_left = input * spacing_x[step][12];
                    if (margin_left >= margin_right)
                        margin_left = 0;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x70:  // ESC p : Proportional spacing ON/OFF
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignore
                    break;

                case 0x73:  // ESC s : Half speed printing ON/OFF
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignore
                    break;

                case 0x78:  // ESC x : DRAFT/NLQ print mode selection
                    nlq = input & 0x01 ? true : false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x7E:  // ESC ~ : MPS-1230 extension
                    if (param_count == 1) param_build = input;  /* Command number */
                    if (param_count == 2)
                    {
                        switch (param_build)
                        {
                            case 2:         // ESX ~ 2 n : reverse ON/OFF
                            case '2':
                                reverse = (input & 1) ? true : false;
                                break;

                            case 3:         // ESX ~ 3 n : select pitch
                            case '3':
                                {
                                    uint8_t new_step = input & 0x0F;
                                    if (new_step < 7)
                                        step = new_step;
                                }
                                break;

                            case 4:         // ESX ~ 4 n : slashed zero ON/OFF
                            case '4':
                                // ignored
                                break;

                            case 5:         // ESX ~ 5 n : switch EPSON, Commodore, Proprinter, Graphics Printer
                            case '5':
                                switch (input)
                                {
                                    case 0:
                                    case '0':
                                        setInterpreter(MPS_PRINTER_INTERPRETER_EPSONFX80);
                                        break;

                                    case 1:
                                    case '1':
                                        setInterpreter(MPS_PRINTER_INTERPRETER_CBM);
                                        break;

                                    case 2:
                                    case '2':
                                        setInterpreter(MPS_PRINTER_INTERPRETER_IBMPP);
                                        break;

                                    case 3:
                                    case '3':
                                        setInterpreter(MPS_PRINTER_INTERPRETER_IBMGP);
                                        break;
                                }
                                break;
                        }

                        state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                default:
                    DBGMSGV("undefined Epson printer escape sequence 0x%02X parameter %d", esc_command, input);
                    state = MPS_PRINTER_STATE_INITIAL;
            }
            break;

        default:
            DBGMSGV("undefined printer state %d", state);
            state = MPS_PRINTER_STATE_INITIAL;
    }
}

/****************************  END OF FILE  ****************************/
