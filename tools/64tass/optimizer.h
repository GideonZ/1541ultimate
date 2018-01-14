/*
    $Id: optimizer.h 1382 2017-03-26 17:48:00Z soci $

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
#ifndef OPTIMIZER_H
#define OPTIMIZER_H
#include "inttypes.h"

struct optimizer_s;
struct cpu_s;

extern void cpu_opt(uint8_t, uint32_t, int, linepos_t);
extern void cpu_opt_invalidate(void);
extern void cpu_opt_destroy(struct optimizer_s *);
extern void cpu_opt_long_branch(uint16_t);
extern void cpu_opt_set_cpumode(const struct cpu_s *);

#endif
