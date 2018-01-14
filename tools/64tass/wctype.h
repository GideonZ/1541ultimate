/*
    $Id: wctype.h 1095 2016-05-12 14:24:08Z soci $

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

#ifndef WCTYPE_H
#define WCTYPE_H

#ifdef __DJGPP__
#include <wctype.h>
extern int iswprint(wint_t wc);
#elif defined __GNUC__ || _MSC_VER >= 1400 || __WATCOMC__
#include <wctype.h>
#elif __STDC_VERSION__ >= 199901L && !defined __VBCC__
#include <wctype.h>
#else
#include "wchar.h"

extern int iswprint(wint_t wc);
#endif

#endif
