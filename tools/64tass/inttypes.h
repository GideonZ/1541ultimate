/*
    $Id: inttypes.h 1412 2017-03-30 18:14:47Z soci $

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
#ifndef INTTYPES_H
#define INTTYPES_H

#ifndef _MSC_VER
#include <unistd.h>
#elif _MSC_VER >= 1200
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>

#ifndef PRId32
#define PRId32 "d"
#endif
#ifndef PRIu32
#define PRIu32 "u"
#endif
#ifndef PRIx32
#define PRIx32 "x"
#endif
#ifndef PRIX32
#define PRIX32 "X"
#endif
#ifndef PRIxPTR
#define PRIxPTR "lx"
#endif

#ifndef PRIxSIZE
#if __STDC_VERSION__ >= 199901L && !defined _WIN32 && !defined __VBCC__
#define PRIxSIZE  "zx"
#elif defined USHRT_MAX && SIZE_MAX == USHRT_MAX && !defined __VBCC__ && !defined __DJGPP__
#define PRIxSIZE  "hx"
#elif defined UINT_MAX && SIZE_MAX == UINT_MAX && !defined __VBCC__ && !defined __DJGPP__
#define PRIxSIZE  "x"
#elif defined ULONG_MAX && SIZE_MAX == ULONG_MAX
#define PRIxSIZE  "lx"
#elif defined ULLONG_MAX && SIZE_MAX == ULLONG_MAX
#define PRIxSIZE  "llx"
#else
#define PRIxSIZE  "x"
#endif
#endif

#ifndef PRIuSIZE
#if __STDC_VERSION__ >= 199901L && !defined _WIN32 && !defined __VBCC__
#define PRIuSIZE  "zu"
#elif defined USHRT_MAX && SIZE_MAX == USHRT_MAX && !defined __VBCC__ && !defined __DJGPP__
#define PRIuSIZE  "hu"
#elif defined UINT_MAX && SIZE_MAX == UINT_MAX && !defined __VBCC__ && !defined __DJGPP__
#define PRIuSIZE  "u"
#elif defined ULONG_MAX && SIZE_MAX == ULONG_MAX
#define PRIuSIZE  "lu"
#elif defined ULLONG_MAX && SIZE_MAX == ULLONG_MAX
#define PRIuSIZE  "llu"
#else
#define PRIuSIZE  "u"
#endif
#endif

typedef uint32_t uchar_t;
typedef uint32_t line_t;
#define PRIuline PRIu32
#define PRIxline PRIx32
typedef uint32_t address_t;
typedef struct { address_t address, bank; } address2_t;
#define PRIaddress PRIx32
typedef uint32_t linecpos_t;
struct linepos_s {line_t line;linecpos_t pos;};
#define PRIlinepos PRIu32
typedef const struct linepos_s *linepos_t;
typedef int32_t ival_t;
#define PRIdval PRId32
typedef uint32_t uval_t;
#define PRIxval PRIx32
#define PRIuval PRIu32
#define PRIXval PRIX32

#ifndef SIZE_MAX
# define SIZE_MAX ((size_t)-1)
#endif

#ifndef SSIZE_MAX
# define SSIZE_MAX ((ssize_t)(SIZE_MAX / 2))
#endif

#define SIZE_MSB ((size_t)1 << (sizeof(size_t) * 8 - 1))

#define lenof(a) (sizeof(a) / sizeof (a)[0])

#endif
