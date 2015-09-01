/*-------------------------------------------*/
/* Integer type definitions for FatFs module */
/*-------------------------------------------*/

#ifndef _FF_INTEGER
#define _FF_INTEGER

#include <stdint.h>

/* This type MUST be 8 bit */
// typedef unsigned char	BYTE;
#define BYTE uint8_t

/* These types MUST be 16 bit */
//typedef short			SHORT;
#define SHORT int16_t
//typedef unsigned short	WORD;
#define WORD uint16_t
// typedef unsigned short	WCHAR;
#define WCHAR uint16_t

/* These types MUST be 16 bit or 32 bit */
// typedef int				INT;
#define INT int
//typedef unsigned int	UINT;
#define UINT uint32_t

/* These types MUST be 32 bit */
//typedef long			LONG;
#define LONG int32_t
//typedef unsigned long	DWORD;
#define DWORD uint32_t

typedef void * DRVREF;

#endif
