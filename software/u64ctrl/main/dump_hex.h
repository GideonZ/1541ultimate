/*
 *   1541 Ultimate - The Storage Solution for your Commodore computer.
 *   Copyright (C) 2009  Gideon Zweijtzer - Gideon's Logic Architectures
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Abstract:
 *	 Simple routine to dump some hex values on the console.
 *
 */
#ifndef DUMPHEX_H
#define DUMPHEX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dump_hex(void *pp, int len);
void dump_hex_relative(void *pp, int len);
void dump_hex_actual(void *pp, int len, int relative);
void dump_hex_dirty(void *p, int len, uint8_t ptrn);
void dump_hex_verify(void *pp1, void *pp2, int len);

#ifdef __cplusplus
}
#endif

#endif

