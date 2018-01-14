/*
    $Id: math.c 1096 2016-05-12 14:40:53Z soci $

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
#include "math.h"

#if defined __VBCC__
double cbrt(double a) {
    return (a > 0.0) ? pow(a, 1.0/3.0) : -pow(-a, 1.0/3.0);
}

double round(double a) {
    return (a > 0.0) ? floor(a + 0.5) : ceil(a - 0.5);
}

double trunc(double a) {
    return (a > 0.0) ? floor(a) : ceil(a);
}

double hypot(double a, double b) {
    return sqrt(a * a + b * b);
}
#endif
