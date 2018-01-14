/*
 *  my_getopt.h - interface to my re-implementation of getopt.
 *  Copyright 1997, 2000, 2001, 2002, Benjamin Sittler
 *  $Id: my_getopt.h 1165 2016-06-12 18:23:19Z soci $
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

#ifndef MY_GETOPT_H
#define MY_GETOPT_H

#include "stdbool.h"

/* UNIX-style short-argument parser */
/*extern int my_getopt(int argc, char *argv[], const char *opts);*/

extern int my_optind, my_optopt;
extern bool my_opterr;
extern char *my_optarg;

struct my_option {
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

/* human-readable values for has_arg */
#define my_no_argument 0
#define my_required_argument 1
#define my_optional_argument 2

/* GNU-style long-argument parsers */
extern int my_getopt_long(int argc, char *argv[], const char *shortopts,
                       const struct my_option *longopts, int *longind);

/*
extern int my_getopt_long_only(int argc, char *argv[], const char *shortopts,
                            const struct my_option *longopts, int *longind);

*/
#endif
