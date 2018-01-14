/*
    $Id: attributes.h 1408 2017-03-30 15:28:02Z soci $

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
#ifndef ATTRIBUTES_H
#define ATTRIBUTES_H

#ifdef UNUSED
#elif defined(__GNUC__)
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED(x) /*@unused@*/ x
#else
# define UNUSED(x) x
#endif

#ifdef MUST_CHECK
#elif __GNUC__ >= 3
# define MUST_CHECK __attribute__ ((warn_unused_result))
#else
# define MUST_CHECK
#endif

#ifdef MALLOC
#elif __GNUC__ >= 3
# define MALLOC __attribute__ ((malloc,warn_unused_result))
#else
# define MALLOC
#endif

#ifdef NO_RETURN
#elif defined(__GNUC__)
# define NO_RETURN  __attribute__((noreturn))
#else
# define NO_RETURN
#endif

#ifdef FAST_CALL
#elif defined(__GNUC__) && defined(__i386__)
# define FAST_CALL  __attribute__((regparm(3)))
#else
# define FAST_CALL
#endif

#ifdef NO_INLINE
#elif __GNUC__ >= 3
# define NO_INLINE  __attribute__((noinline))
#else
# define NO_INLINE
#endif

#ifndef __cplusplus
#if __STDC_VERSION__ >= 199901L
#elif defined(__GNUC__)
# define inline __inline
#elif _MSC_VER >= 900
# define inline __inline
#else
# define inline
#endif
#endif

#endif
