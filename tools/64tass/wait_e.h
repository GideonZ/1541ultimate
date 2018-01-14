/*
    $Id: wait_e.h 1390 2017-03-27 02:28:33Z soci $

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
#ifndef WAIT_E_H
#define WAIT_E_H
typedef enum Wait_types {
    W_NONE, W_ENDM, W_ENDM2, W_BEND, W_BEND2, W_HERE, W_HERE2, W_ENDU, W_ENDU2,
    W_ENDS, W_ENDS2, W_ENDC, W_ENDP, W_ENDP2, W_NEXT, W_NEXT2, W_SEND, W_SEND2,
    W_PEND, W_FI, W_FI2, W_ENDF, W_ENDF2, W_SWITCH, W_SWITCH2, W_WEAK, W_WEAK2
} Wait_types;
#endif
