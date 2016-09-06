/*
##########################################################################
##                                                                      ##
##   RENE GARCIA                        GNU General Public Licence V3   ##
##                                                                      ##
##########################################################################
##                                                                      ##
##      Project : MPS Emulator                                          ##
##      Class   : MpsPrinter                                            ##
##      Piece   : mps_printer_ibmgp.cc                                  ##
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
*                      MpsPrinter::setIBMCharset(cs)            Public  *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Change international charset for IBM emulation             *
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
MpsPrinter::setIBMCharset(uint8_t cs)
{
    /* Check charset range */
    if (cs > 5)
        return;

    DBGMSGV("request to change IBM charset to %d", cs);

    /* If charset change and emulation is now for this printer */
    if (cs != ibm_charset && (interpreter == MPS_PRINTER_INTERPRETER_IBMPP
                             || interpreter == MPS_PRINTER_INTERPRETER_IBMGP))
    {
        if (charset) charset = cs + 1;
        DBGMSGV("current charset set to %d (IBM Emulation)", charset);
    }

    /* record charset */
    ibm_charset = cs + 1;
}

/************************************************************************
*                  MpsPrinter::IBMgp_Interpreter(data)         Private  *
*                  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                  *
* Function : IBM Graphics Printer single data interpreter automata      *
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
MpsPrinter::IBMgp_Interpreter(uint8_t input)
{
    switch(state)
    {
        case MPS_PRINTER_STATE_INITIAL:

            /* =======  Select action if command char received */
            cbm_command = input;
            param_count = 0;
            switch(input)
            {
                case 0x07:   // BELL
                    // No bell here in Ultimate, just ignore
                    break;

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

                case 0x0A:   // LF: line feed (no CR)
                case 0x0B:   // VT: vertical tabulation
                    head_y += interline;
                    if (head_y > margin_bottom)
                        FormFeed();
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

                case 0x12:   // DC2: 17.1 chars/inch off
                    step = MPS_PRINTER_STEP_PICA;
                    break;

                case 0x14:   // DC4: Double width printing off
                    double_width = false;
                    break;

                case 0x18:   // CAN: Clear print buffer
                    // ignored
                    break;

                case 0x1B:   // ESC: ASCII code for escape
                    state = MPS_PRINTER_STATE_ESC;
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

                case 0x21:  // ESC ! : Select graphics layout types
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x2D:  // ESC - : Underline on/off
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

                case 0x36:  // ESC 6 : IBM Table 2 selection
                    charset = ibm_charset;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x37:  // ESC 7 : IBM Table 1 selection
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

                case 0x3C:  // ESC < : Set left to right printing for one line
                    state = MPS_PRINTER_STATE_INITIAL;
                    // ignore
                    break;

                case 0x3D:  // ESC = : Down Line Loading of user characters
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x40:  // ESC @ : Initialise printer (main reset)
                    Init();
                    state = MPS_PRINTER_STATE_INITIAL;
                    // te be done
                    break;

                case 0x41:  // ESC A : Spacing = n/72"
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

                case 0x47:  // ESC G : Double Strike Printing ON
                    double_strike = true;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x48:  // ESC H : Double Strike Printing OFF
                    double_strike = false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x49:  // ESC I : Select print definition
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

                case 0x53:  // ESC S : Superscript/subscript printing
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x54:  // ESC T : Clear superscript/subscript printing
                    state = MPS_PRINTER_STATE_INITIAL;
                    script = MPS_PRINTER_SCRIPT_NORMAL;
                    break;

                case 0x55:  // ESC U : Mono/Bidirectional printing
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    // to be done
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

                case 0x5B:  // ESC [ : Set horizontal spacing
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x78:  // ESC x : DRAFT/NLQ print mode selection
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                case 0x7E:  // ESC ~ : MPS-1230 extension
                    state = MPS_PRINTER_STATE_ESC_PARAM;
                    break;

                default:
                    DBGMSGV("undefined IBMgp printer escape sequence %d", input);
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

                case 0x2D:  // ESC - : Underline on/off
                    underline = input & 0x01 ? true : false;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x33:  // ESC 3 : Spacing = n/216"
                    interline = input;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x3D:  // ESC = : Down Line Loading of user characters (parse but ignore)
                    if (param_count == 1) param_build = input;
                    if (param_count == 2) param_build |= input<<8;
                    if ((param_count > 2) && (param_count == param_build+2))
                        state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x41:  // ESC A : Spacing = n/72"
                    interline = input * 3;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x43:  // ESC C : Set form length
                    // Ignored in this version
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

                case 0x49:  // ESC I : Select print definition
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

                case 0x53:  // ESC S : Superscript/subscript printing
                    script = input & 0x01 ? MPS_PRINTER_SCRIPT_SUB : MPS_PRINTER_SCRIPT_SUPER;
                    state = MPS_PRINTER_STATE_INITIAL;
                    break;

                case 0x55:  // ESC U : Mono/Bidirectional printing
                    state = MPS_PRINTER_STATE_INITIAL;
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

                case 0x5B:  // ESC [ : Set horizontal spacing
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
                    DBGMSGV("undefined IBMgp printer escape sequence 0x%02X parameter %d", esc_command, input);
                    state = MPS_PRINTER_STATE_INITIAL;
            }
            break;

        default:
            DBGMSGV("undefined printer state %d", state);
            state = MPS_PRINTER_STATE_INITIAL;
    }
}

/****************************  END OF FILE  ****************************/
