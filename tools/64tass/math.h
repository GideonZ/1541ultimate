/*
    $Id: math.h 1408 2017-03-30 15:28:02Z soci $

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
#ifndef MATH_H
#define MATH_H
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef __cplusplus
#if __STDC_VERSION__ >= 199901L && !defined __VBCC__
#elif defined __GNUC__ || _MSC_VER > 1800
#elif defined __VBCC__
extern double cbrt(double);
extern double round(double);
extern double trunc(double);
extern double hypot(double, double);
#else
#include "attributes.h"
static inline double cbrt(double a) {
    return (a > 0.0) ? pow(a, 1.0/3.0) : -pow(-a, 1.0/3.0);
}
static inline double round(double a) {
    return (a > 0.0) ? floor(a + 0.5) : ceil(a - 0.5);
}
static inline double trunc(double a) {
    return (a > 0.0) ? floor(a) : ceil(a);
}
#ifdef _MSC_VER
#define hypot(x,y) _hypot((x),(y))
#elif defined __WATCOMC__
#else
static inline double hypot(double a, double b) {
    return sqrt(a * a + b * b);
}
#endif
#endif
#endif

#endif
