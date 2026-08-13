/*
 * c64.h - host test stub
 *
 * configio.cc includes c64.h for exactly two constants, both used by the
 * "clear config flash" menu handler, which these tests do not exercise.
 * Pulling in the real header would drag the cartridge and machine layer into
 * a test about parsing text files.
 */

#ifndef CONFIGIO_TEST_STUB_C64_H
#define CONFIGIO_TEST_STUB_C64_H

#include "subsys.h"

#ifndef MENU_C64_POWEROFF
#define MENU_C64_POWEROFF 0x640B
#endif

#endif /* CONFIGIO_TEST_STUB_C64_H */
