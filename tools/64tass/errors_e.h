/*
    $Id: errors_e.h 1493 2017-04-28 08:38:45Z soci $

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/
#ifndef ERRORS_E_H
#define ERRORS_E_H

/* ---------------------------------------------------------------------------
   $00-$3f warning
   $40-$7f error
   $80-$bf fatal
*/

typedef enum Error_types {
    ERROR____OLD_MODULO = 0x00,
    ERROR_______OLD_NEQ,
    ERROR______OLD_GOTO,
    ERROR____OLD_STRING,
    ERROR_______OLD_ENC,
    ERROR__CONST_RESULT,
    ERROR____IND_RESULT,
#ifdef _WIN32
    ERROR___INSENSITIVE,
#endif
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
    ERROR_____BACKSLASH,
#else
    ERROR__RESERVED_CHR,
#endif
    ERROR_ABSOLUTE_PATH,
    ERROR_WUSER_DEFINED,
    ERROR___OPTIMIZABLE,
    ERROR______SIMPLIFY,
    ERROR_____REDUNDANT,
    ERROR_____OLD_EQUAL,
    ERROR_DUPLICATECASE,
    ERROR_NONIMMEDCONST,
    ERROR_FLOAT_COMPARE,
    ERROR_LEADING_ZEROS,
    ERROR_DIRECTIVE_IGN,
    ERROR___LONG_BRANCH,

    ERROR__DOUBLE_RANGE = 0x40,
    ERROR_DOUBLE_ESCAPE,
    ERROR_EXTRA_CHAR_OL,
    ERROR_NOT_TWO_CHARS,
    ERROR_NUMERIC_OVERF,
    ERROR_ADDRESS_LARGE,
    ERROR_GENERL_SYNTAX,
    ERROR_EXPRES_SYNTAX,
    ERROR_LABEL_REQUIRE,
    ERROR_MISSING_ARGUM,
    ERROR_DIVISION_BY_Z,
    ERROR_NO_ZERO_VALUE,
    ERROR___NO_HIGH_BIT,
    ERROR__BYTES_NEEDED,
    ERROR___NO_LAST_GAP,
    ERROR_CANT_CROSS_BA,
    ERROR_OUTOF_SECTION,
    ERROR_NEGFRAC_POWER,
    ERROR_SQUARE_ROOT_N,
    ERROR_LOG_NON_POSIT,
    ERROR___MATH_DOMAIN,
    ERROR___EMPTY_RANGE,
    ERROR__EMPTY_STRING,
    ERROR__NOT_ONE_CHAR,
    ERROR____NO_FORWARD,
    ERROR_REQUIREMENTS_,
    ERROR______CONFLICT,
    ERROR___INDEX_RANGE,
    ERROR_____KEY_ERROR,
    ERROR__OFFSET_RANGE,
    ERROR__NOT_HASHABLE,
    ERROR__NOT_KEYVALUE,
    ERROR_____CANT_IVAL,
    ERROR_____CANT_UVAL,
    ERROR______NOT_UVAL,
    ERROR_CANT_BROADCAS,
    ERROR_____CANT_SIGN,
    ERROR______CANT_ABS,
    ERROR______CANT_INT,
    ERROR______CANT_LEN,
    ERROR_____CANT_SIZE,
    ERROR_____CANT_BOOL,
    ERROR______NOT_ITER,
    ERROR__NO_BYTE_ADDR,
    ERROR__NO_WORD_ADDR,
    ERROR__NO_LONG_ADDR,
    ERROR____NOT_DIRECT,
    ERROR__NOT_DATABANK,
    ERROR_____NOT_BANK0,
    ERROR_OUT_OF_MEMORY,
    ERROR__ADDR_COMPLEX,
    ERROR__EMPTY_ENCODI,
    ERROR__USER_DEFINED,
    ERROR_NO_ADDRESSING,
    ERROR___NO_REGISTER,
    ERROR___NO_LOT_OPER,
    ERROR____PAGE_ERROR,
    ERROR__BRANCH_CROSS,
    ERROR_BRANCH_TOOFAR,
    ERROR____PTEXT_LONG,
    ERROR______EXPECTED,
    ERROR___NOT_ALLOWED,
    ERROR_RESERVED_LABL,
    ERROR___UNKNOWN_CPU,

    ERROR___NOT_DEFINED,
    ERROR__INVALID_OPER,
    ERROR____STILL_NONE,

    ERROR_CANT_FINDFILE = 0xc0,
    ERROR__READING_FILE,
    ERROR_CANT_WRTE_OBJ,
    ERROR_CANT_WRTE_LST,
    ERROR_CANT_WRTE_LBL,
    ERROR_CANT_WRTE_MAK,
    ERROR_CANT_WRTE_ERR,
    ERROR_FILERECURSION,
    ERROR__MACRECURSION,
    ERROR__FUNRECURSION,
    ERROR_TOO_MANY_PASS,
    ERROR_UNKNOWN_OPTIO,
    ERROR____LABEL_ROOT
} Error_types;
#endif
