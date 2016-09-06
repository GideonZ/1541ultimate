/*
##########################################################################
##                                                                      ##
##   RENE GARCIA                        GNU General Public Licence V3   ##
##                                                                      ##
##########################################################################
##                                                                      ##
##      Project : MPS Emulator                                          ##
##      Class   : MpsPrinter                                            ##
##      Piece   : mps_printer_cbm.cc                                    ##
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

/* =======  Printable control chars in quotted mode */
uint8_t MpsPrinter::cbm_special[MPS_PRINTER_MAX_SPECIAL] =
{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x81,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
};

/************************************************************************
*                       MpsPrinter::setCBMCharset(cs)           Public  *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Change international charset for Commodore emulation       *
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
MpsPrinter::setCBMCharset(uint8_t cs)
{
    /* Check charset range */
    if (cs > 6)
        return;

    DBGMSGV("request to change CBM charset to %d", cs);

    /* If charset change and emulation is now for this printer */
    if (cs != cbm_charset && interpreter == MPS_PRINTER_INTERPRETER_CBM)
    {
        charset = cs;
        DBGMSGV("current charset set to %d (CBM Emulation)", charset);
    }

    /* record charset */
    cbm_charset = cs;
}


/************************************************************************
*                       MpsPrinter::setCharsetVariant(cs)       Public  *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~               *
* Function : Change Commodore PETSCII charset                           *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    cs : (uint8_t) New Commodore PETSCII Charset                       *
*               0 - Uppercases/Graphics                                 *
*               1 - Lowercases/Uppercases                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void
MpsPrinter::setCharsetVariant(uint8_t cs)
{
    if (cs !=0 && cs != 1)
        return;

    charset_variant = cs;
}

/************************************************************************
*                       MpsPrinter::CBMBim(c,x,y)               Private *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~                       *
* Function : Print a single bitmap image record (commodore standard)    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    c : (uint16_t) record to print                                     *
*    x : (uint16_t) first pixel position from left of printable area    *
*    y : (uint16_t) first pixel position from top of printable area     *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (uint16_t) printed width (in pixels)                               *
*                                                                       *
************************************************************************/

uint16_t
MpsPrinter::CBMBim(uint8_t head)
{
    /* -------  Each dot to print (LSB is up) */
    for (int j=0; j<8; j++)
    {
        /* Need to print a dot ?*/
        if (head & 0x01)
        {
            /* In MPS, BIM uses double width, dots are printed twice */
            Dot(head_x, head_y+spacing_y[MPS_PRINTER_SCRIPT_NORMAL][j], true);
            Dot(head_x+spacing_x[0][1], head_y+spacing_y[MPS_PRINTER_SCRIPT_NORMAL][j], true);
        }

        head >>= 1;
    }

    /* Return spacing */
    return spacing_x[0][2];
}

/************************************************************************
*                       MpsPrinter::IsCBMSpecial(c)             Private *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Tell if an char code is special commodore control or not   *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    c : (uint8_t) Byte code to test                                    *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (bool) true if special                                             *
*                                                                       *
************************************************************************/

bool
MpsPrinter::IsCBMSpecial(uint8_t input)
{
    /* Not very efficient search but table is small */
    for (int i=0; i< MPS_PRINTER_MAX_SPECIAL; i++)
        if (input == cbm_special[i])
            return true;

    return false;
}

/************************************************************************
*                   MpsPrinter::CBM_Interpreter(data)          Private  *
*                   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Commodore MPS single data interpreter automata             *
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
MpsPrinter::CBM_Interpreter(uint8_t input)
{
    uint8_t cmd;

    switch(state)
    {
        case MPS_PRINTER_STATE_INITIAL:
            /* =======  Special Commodore special quotted chars */
            if (quoted)
            {
                if (!IsPrintable(input))
                {
                    if (IsCBMSpecial(input))
                    {
                        bool saved_reverse = reverse;
                        reverse = true;
                        head_x += Char(Charset2Chargen(input | 0x40));
                        reverse = saved_reverse;

                        if (head_x > margin_right)
                        {
                            head_y += interline;
                            head_x  = margin_left;

                            if (head_y > margin_bottom)
                                FormFeed();
                        }

                        return;
                    }
                    else
                    {
                        quoted = false;
                    }
                }
            }

            /* =======  Select action if command char received */
            cbm_command = input;
            param_count = 0;

            if (bim_mode && (input & 0x80))
                cmd = 0;
            else
                cmd = input;

            switch(cmd)
            {
                case 0x08:   // BIT IMG: bitmap image
                    next_interline = spacing_y[MPS_PRINTER_SCRIPT_NORMAL][7];
                    //state = MPS_PRINTER_STATE_PARAM;
                    bim_mode = true;
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

                case 0x0C:   // FF: form feed
                    FormFeed();
                    break;

                case 0x0A:   // LF: line feed (LF+CR)
                case 0x0D:   // CR: carriage return (CR+LF)
                    head_y += next_interline;
                    head_x  = 0;
                    quoted = false;
                    if (head_y > margin_bottom)
                        FormFeed();
                    break;

                case 0x0E:   // EN ON: Double width printing on
                    double_width = true;
                    //bim_mode = false;
                    break;

                case 0x0F:   // EN OFF: Double width printing off
                    double_width = false;
                    bim_mode = false;
                    break;

                case 0x10:   // POS: Jump to horizontal position in number of chars
                    state = MPS_PRINTER_STATE_PARAM;
                    break;

                case 0x11:   // CRSR DWN: Upper/lower case printing
                    charset_variant = 1;
                    break;

                case 0x12:   // RVS ON: Reverse printing on
                    reverse = true;
                    break;

                case 0x1A:   // BIT IMG SUB, one byte
                    if (bim_mode)
                    {
                        state = MPS_PRINTER_STATE_PARAM;
                    }
                    break;

                case 0x1B:   // ESC: ASCII code for escape
                    state = MPS_PRINTER_STATE_ESC;
                    break;

                case 0x1F:   // NLQ ON: Near letter quality on
                    nlq = true;
                    break;

                case 0x8D:   // CS: Cariage return with no line feed
                    head_x  = 0;
                    break;

                case 0x91:   // CRSR UP: Upper case printing
                    charset_variant = 0;
                    break;

                case 0x92:   // RVS OFF: Reverse printing off
                    reverse = false;
                    break;

                case 0x9F:   // NLQ OFF: Near letter quality off
                    nlq = false;
                    break;

                default:    // maybe a printable character
                    if (bim_mode && (input & 0x80))
                    {
                        head_x += CBMBim(input & 0x7F);
                    }
                    else
                    {
                        if (IsPrintable(input))
                        {
                            bim_mode = false;
                            next_interline = interline;

                            if (input == 0x22)
                            quoted = quoted ? false : true;

                            head_x += Char(Charset2Chargen(input));
                            if (head_x > margin_right)
                            {
                                head_y += interline;
                                head_x  = 0;

                                if (head_y > margin_bottom)
                                    FormFeed();
                            }
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
                case 0x10:  //ESC POS : Jump to horizontal position in number of dots
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x2D:  // ESC - : Underline on/off
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

                case 0x38:  // ESC 8 : Out of paper detection disabled
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignored
                    break;

                case 0x39:  // ESC 9 : Out of paper detection enabled
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignored
                    break;

                case 0x3D:  // ESC = : Down Line Loading of user characters
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x43:  // ESC c : Set form length
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x45:  // ESC e : Emphasized printing ON
                    bold = true;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x46:  // ESC f : Emphasized printing OFF
                    bold = false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x47:  // ESC g : Double strike printing ON
                    double_strike = true;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x48:  // ESC h : Double strike printing OFF
                    double_strike = false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x49:  // ESC i : Select print definition
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x4E:  // ESC n : Defines bottom of from (BOF)
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x4F:  // ESC o : Clear bottom of from (BOF)
                    // Ignored in this version, usefull only for continuous paper feed
                    break;

                case 0x53:  // ESC s : Superscript/subscript printing
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x54:  // ESC t : Clear superscript/subscript printing
                    state = MPS_PRINTER_STATE_INITIAL;
                    script = MPS_PRINTER_SCRIPT_NORMAL;
                    break;

                case 0x78:  // ESC X : DRAFT/NLQ print mode selection
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x5B:  // ESC [ : Print style selection
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x7E:  // ESC ~ : MPS-1230 extension
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                default:
                    DBGMSGV("undefined CBM printer escape sequence %d", input);
                    state = MPS_PRINTER_STATE_INITIAL;
            }

            break;

        // =======  Escape sequence parameters
        case MPS_PRINTER_STATE_ESC_PARAM:
            param_count++;
            switch(esc_command)
            {
                case 0x10:  // ESC POS : Jump to horizontal position in number of dots
                    if (param_count == 1) param_build = input << 8;
                    if (param_count == 2)
                    {
                        param_build |= input;
                        param_build <<= 2;

                        if ((param_build < MPS_PRINTER_PAGE_PRINTABLE_WIDTH) && param_build > head_x)
                        {
                            head_x = param_build;
                        }

                        state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                case 0x3D:  // ESC = : Down Line Loading of user characters (parse but ignore)
                    if (param_count == 1) param_build = input;
                    if (param_count == 2) param_build |= input<<8;
                    if ((param_count > 2) && (param_count == param_build+2))
                        state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x43:  // ESC c : Set form length
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

                case 0x49:  // ESC i : Select print definition
                    switch (input)
                    {
                        case 0x00:  // Draft
                        case 0x30:
                            nlq = false;
                            break;

                        case 0x02:  // NLQ
                        case 0x32:
                            nlq = true;

                            break;
                        case 0x04:  // Draft + DLL enabled (not implemented)
                        case 0x34:
                            nlq = false;
                            break;

                        case 0x06:  // NLQ + DLL enabled (not implemented)
                        case 0x36:
                            nlq = true;
                            break;
                    }
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x4E:  // ESC n : Defines bottom of from (BOF)
                    // Ignored in this version, usefull only for continuous paper feed
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x53:  // ESC S : Superscript/subscript printing
                    script = input & 0x01 ? MPS_PRINTER_SCRIPT_SUB : MPS_PRINTER_SCRIPT_SUPER;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x2D:  // ESC - : Underline on/off
                    underline = input & 0x01 ? true : false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x5B:  // ESC [ : Print style selection (pica, elite, ...)
                    {
                        uint8_t new_step = input & 0x0F;
                        if (new_step < 7)
                            step = new_step;
                        state = MPS_PRINTER_STATE_INITIAL;
                    }
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
                    DBGMSGV("undefined CBM printer escape sequence 0x%02X parameter %d", esc_command, input);
                    state = MPS_PRINTER_STATE_INITIAL;
            }
            break;

        // =======  Escape sequence parameters
        case MPS_PRINTER_STATE_PARAM:
            param_count++;
            switch(cbm_command)
            {
                case 0x1A:   // BIT IMG SUB: bitmap image repeated one byte
                    if (param_count == 1)
                    {
                        // Get number of repeats
                        param_build = (input==0) ? 256 : input;
                    }
                    if (param_count == 2)
                    {
                        for (int i=0; i<param_build; i++)
                            head_x += CBMBim(input & 0x7F);

                        state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                case 0x10:  // POS : Jump to horizontal position in number of chars
                    if (param_count == 1) param_build = input & 0x0F;
                    if (param_count == 2)
                    {
                        param_build = (param_build * 10) + (input & 0x0F);
                        if (param_build < 80 && (param_build * 24 ) > head_x)
                        {
                            head_x = 24 * param_build;
                        }

                        state = MPS_PRINTER_STATE_INITIAL;
                    }
                    break;

                default:
                    DBGMSGV("undefined CBM printer command 0x%02X parameter %d", cbm_command, input);
                    state = MPS_PRINTER_STATE_INITIAL;
            }
            break;

        default:
            DBGMSGV("undefined printer state %d", state);
            state = MPS_PRINTER_STATE_INITIAL;
    }
}

/****************************  END OF FILE  ****************************/
