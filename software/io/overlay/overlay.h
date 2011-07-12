#ifndef OVERLAY_H
#define OVERLAY_H

#include "integer.h"

#define CHARGEN_LINE_CLOCKS_HI   *(volatile BYTE *)0x40E0000)
#define CHARGEN_LINE_CLOCKS_LO   *(volatile BYTE *)0x40E0001)
#define CHARGEN_CHAR_WIDTH       *(volatile BYTE *)0x40E0002)
#define CHARGEN_CHAR_HEIGHT      *(volatile BYTE *)0x40E0003)
#define CHARGEN_CHARS_PER_LINE   *(volatile BYTE *)0x40E0004)
#define CHARGEN_ACTIVE_LINES     *(volatile BYTE *)0x40E0005)
#define CHARGEN_X_ON_HI          *(volatile BYTE *)0x40E0006)
#define CHARGEN_X_ON_LO          *(volatile BYTE *)0x40E0007)
#define CHARGEN_Y_ON_HI          *(volatile BYTE *)0x40E0008)
#define CHARGEN_Y_ON_LO          *(volatile BYTE *)0x40E0009)
#define CHARGEN_POINTER_HI       *(volatile BYTE *)0x40E000A)
#define CHARGEN_POINTER_LO       *(volatile BYTE *)0x40E000B)
#define CHARGEN_PERFORM_SYNC     *(volatile BYTE *)0x40E000C)

#endif
