#ifndef VERSIONS_H
#define VERSIONS_H

// 1 = corner lower right
// 2 = horizontal bar
// 3 = corner lower left
// 4 = vertical bar
// 5 = corner upper right
// 6 = corner upper left
// 7 = rounded corner lower right
// 8 = T
// 9 = rounded corner lower left
// A = |-
// B = +
// C = -|
// D = rounded corner upper right
// E = _|_
// F = rounded corner upper left
// 10 = alpha
// 11 = beta
// 12 = test grid
// 13 = diamond

// 18-1F commodore specifics (<-, ^, pound, and their reverse counterparts)

// alpha = \020
// beta  = \021

#define RELEASE_TYPE_NORMAL 0
#define RELEASE_TYPE_BETA   1
#define RELEASE_TYPE_ALPHA  2

#define APPL_VERSION_NUMBER "3.13"
#define APPL_RELEASE_TYPE RELEASE_TYPE_ALPHA
#define BOOT_VERSION "V3.2"
#define FPGA_VERSION "FPGA U2 V116"
#define MINIMUM_FPGA_REV 0x18

#if APPL_RELEASE_TYPE == RELEASE_TYPE_ALPHA
  #define APPL_VERSION         APPL_VERSION_NUMBER "\020"
  #define APPL_VERSION_ASCII   APPL_VERSION_NUMBER " alpha"
#elif APPL_RELEASE_TYPE == RELEASE_TYPE_BETA
  #define APPL_VERSION         APPL_VERSION_NUMBER "\021"
  #define APPL_VERSION_ASCII   APPL_VERSION_NUMBER " beta"
#else
  #define APPL_VERSION         APPL_VERSION_NUMBER
  #define APPL_VERSION_ASCII   APPL_VERSION_NUMBER
#endif

#endif

/* note: 'c' is to avoid confusion with 'b' of beta. */
