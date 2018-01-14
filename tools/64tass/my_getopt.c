/*
 *  my_getopt.c - my re-implementation of getopt.
 *  Copyright 1997, 2000, 2001, 2002, Benjamin Sittler
 *  $Id: my_getopt.c 1500 2017-04-30 01:38:58Z soci $
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

#include "my_getopt.h"
#include "unicode.h"
#include "error.h"
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int my_optind = 1, my_optopt = 0;
bool my_opterr = true;
char *my_optarg = NULL;

/* this is the plain old UNIX getopt, with GNU-style extensions. */
/* if you're porting some piece of UNIX software, this is all you need. */
/* this supports GNU-style permution and optional arguments */

static int my_getopt(int argc, char *argv[], const char *opts)
{
  static int charind = 0;
  const char *s;
  char mode, colon_mode;
  int off = 0, opt = -1;

  if (getenv("POSIXLY_CORRECT") != NULL) colon_mode = mode = '+';
  else {
    if ((colon_mode = *opts) == ':') off ++;
    if (((mode = opts[off]) == '+') || (mode == '-')) {
      off++;
      if ((colon_mode != ':') && ((colon_mode = opts[off]) == ':'))
        off ++;
    }
  }
  my_optarg = NULL;
  if (charind != 0) {
    my_optopt = argv[my_optind][charind];
    for (s = opts + off; *s != 0; s++) if (my_optopt == *s) {
      charind++;
      if ((*(++s) == ':') || ((my_optopt == 'W') && (*s == ';'))) {
        if (argv[my_optind][charind] != 0) {
          my_optarg = &(argv[my_optind++][charind]);
          charind = 0;
        } else if (*(++s) != ':') {
          charind = 0;
          if (++my_optind >= argc) {
            if (my_opterr) {fatal_error("option '-");
                            putc(my_optopt, stderr);
                            fputs("' requires an argument", stderr); fatal_error(NULL); }
            opt = (colon_mode == ':') ? ':' : '?';
            goto my_getopt_ok;
          }
          my_optarg = argv[my_optind++];
        }
      }
      opt = my_optopt;
      goto my_getopt_ok;
    }
    if (my_opterr) {fatal_error("option '-");
                    printable_print2((const uint8_t *)argv[my_optind] + charind, stderr, ((my_optopt & 0x80) != 0) ? utf8len((unsigned int)my_optopt) : 1);
                    fputs("' not recognized", stderr); fatal_error(NULL); }
    opt = '?';
    if (argv[my_optind][++charind] == '\0') {
      my_optind++;
      charind = 0;
    }
  my_getopt_ok:
    if (charind != 0 && argv[my_optind][charind] == 0) {
      my_optind++;
      charind = 0;
    }
  } else if ((my_optind >= argc) ||
             ((argv[my_optind][0] == '-') &&
              (argv[my_optind][1] == '-') &&
              (argv[my_optind][2] == '\0'))) {
    my_optind++;
    opt = -1;
  } else if ((argv[my_optind][0] != '-') ||
             (argv[my_optind][1] == '\0')) {
    char *tmp;
    int i, j, k;

    if (mode == '+') opt = -1;
    else if (mode == '-') {
      my_optarg = argv[my_optind++];
      charind = 0;
      opt = 1;
    } else {
      for (i = j = my_optind; i < argc; i++) if ((argv[i][0] == '-') &&
                                        (argv[i][1] != '\0')) {
        my_optind = i;
        opt = my_getopt(argc, argv, opts);
        while (i > j) {
          tmp = argv[--i];
          for (k = i; k + 1 < my_optind; k++) argv[k] = argv[k + 1];
          argv[--my_optind] = tmp;
        }
        break;
      }
      if (i == argc) opt = -1;
    }
  } else {
    charind++;
    opt = my_getopt(argc, argv, opts);
  }
  if (my_optind > argc) my_optind = argc;
  return opt;
}

/* this is the extended getopt_long{,_only}, with some GNU-like
 * extensions. Implements _getopt_internal in case any programs
 * expecting GNU libc getopt call it.
 */

static int my_getopt_internal(int argc, char *argv[], const char *shortopts,
                     const struct my_option *longopts, int *longind,
                     bool long_only)
{
  char mode, colon_mode;
  int shortoff = 0, opt = -1;

  if (getenv("POSIXLY_CORRECT") != NULL) colon_mode = mode = '+';
  else {
    if ((colon_mode = *shortopts) == ':') shortoff ++;
    if (((mode = shortopts[shortoff]) == '+') || (mode == '-')) {
      shortoff++;
      if ((colon_mode != ':') && ((colon_mode = shortopts[shortoff]) == ':'))
        shortoff ++;
    }
  }
  my_optarg = NULL;
  if ((my_optind >= argc) ||
      ((argv[my_optind][0] == '-') &&
       (argv[my_optind][1] == '-') &&
       (argv[my_optind][2] == '\0'))) {
    my_optind++;
    opt = -1;
  } else if ((argv[my_optind][0] != '-') ||
            (argv[my_optind][1] == '\0')) {
    char *tmp;
    int i, j, k;

    opt = -1;
    if (mode == '+') return -1;
    if (mode == '-') {
      my_optarg = argv[my_optind++];
      return 1;
    }
    for (i = j = my_optind; i < argc; i++) if ((argv[i][0] == '-') &&
                                    (argv[i][1] != '\0')) {
      my_optind = i;
      opt = my_getopt_internal(argc, argv, shortopts,
                               longopts, longind,
                               long_only);
      while (i > j) {
        tmp = argv[--i];
        for (k = i; k + 1 < my_optind; k++)
          argv[k] = argv[k + 1];
        argv[--my_optind] = tmp;
      }
      break;
    }
  } else if ((!long_only) && (argv[my_optind][1] != '-'))
    opt = my_getopt(argc, argv, shortopts);
  else {
    int charind, offset;
    int found = 0, ind, hits = 0;

    if (((my_optopt = argv[my_optind][1]) != '-') && argv[my_optind][2] == 0) {
      int c;

      ind = shortoff;
      while ((c = shortopts[ind++]) != 0) {
        if (((shortopts[ind] == ':') ||
            ((c == 'W') && (shortopts[ind] == ';'))) &&
           (shortopts[++ind] == ':'))
          ind ++;
        if (my_optopt == c) return my_getopt(argc, argv, shortopts);
      }
    }
    offset = (argv[my_optind][1] != '-') ? 1 : 2;
    for (charind = offset;
        (argv[my_optind][charind] != '\0') &&
          (argv[my_optind][charind] != '=');
        charind++);
    for (ind = 0; longopts[ind].name != NULL && hits == 0; ind++)
      if ((strlen(longopts[ind].name) == (size_t) (charind - offset)) &&
         (strncmp(longopts[ind].name,
                  argv[my_optind] + offset, charind - offset) == 0))
        found = ind, hits++;
    if (hits == 0) for (ind = 0; longopts[ind].name != NULL; ind++)
      if (strncmp(longopts[ind].name,
                 argv[my_optind] + offset, charind - offset) == 0)
        found = ind, hits++;
    if (hits == 1) {
      opt = 0;

      if (argv[my_optind][charind] == '=') {
        if (longopts[found].has_arg == 0) {
          opt = '?';
          if (my_opterr) {fatal_error("option '--");
                         printable_print((const uint8_t *)longopts[found].name, stderr);
                         fputs("' doesn't allow an argument", stderr); fatal_error(NULL); }
        } else {
          my_optarg = argv[my_optind] + ++charind;
          /*charind = 0;*/
        }
      } else if (longopts[found].has_arg == 1) {
        if (++my_optind >= argc) {
          opt = (colon_mode == ':') ? ':' : '?';
          if (my_opterr) {fatal_error("option '--");
                         printable_print((const uint8_t *)longopts[found].name, stderr);
                         fputs("' requires an argument", stderr); fatal_error(NULL); }
        } else my_optarg = argv[my_optind];
      }
      if (opt == 0) {
        if (longind != NULL) *longind = found;
        if (longopts[found].flag == NULL) opt = longopts[found].val;
        else *(longopts[found].flag) = longopts[found].val;
      }
      my_optind++;
    } else if (hits == 0) {
      if (offset == 1) opt = my_getopt(argc, argv, shortopts);
      else {
        opt = '?';
        if (my_opterr) {fatal_error("option '");
                       printable_print((const uint8_t *)argv[my_optind++], stderr);
                       fputs("' not recognized", stderr); fatal_error(NULL); }
      }
    } else {
      opt = '?';
      if (my_opterr) {fatal_error("option '");
                     printable_print((const uint8_t *)argv[my_optind++], stderr);
                     fputs("' is ambiguous", stderr); fatal_error(NULL); }
    }
  }
  if (my_optind > argc) my_optind = argc;
  return opt;
}

int my_getopt_long(int argc, char *argv[], const char *shortopts,
                const struct my_option *longopts, int *longind)
{
  return my_getopt_internal(argc, argv, shortopts, longopts, longind, false);
}

/*
int my_getopt_long_only(int argc, char *argv[], const char *shortopts,
                const struct my_option *longopts, int *longind)
{
  return _my_getopt_internal(argc, argv, shortopts, longopts, longind, true);
}
*/
