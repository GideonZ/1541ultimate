/*
    $Id: wctype.c 1095 2016-05-12 14:24:08Z soci $

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
#include "wctype.h"

#ifdef __DJGPP__
#elif defined __GNUC__ || _MSC_VER >= 1400 || __WATCOMC__
#elif __STDC_VERSION__ >= 199901L && !defined __VBCC__
#else
#include <ctype.h>

int iswprint(wint_t wc) {
    if (wc < 0x80) return isprint(wc);
    return (wc >= 0xa0 && wc <= 0xff) ? 1 : 0;
}
#endif
