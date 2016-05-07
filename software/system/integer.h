/*-------------------------------------------*/
/* Integer type definitions for FatFs module */
/*-------------------------------------------*/

#ifndef _INTEGER
#define _INTEGER

#include <stdint.h>

/* These types must be 16-bit integer */
typedef unsigned short	WCHAR;

/* Boolean type */
typedef enum { FALSE = 0, TRUE } BOOL;

/* NULL */
#ifndef NULL
#define NULL 0
#endif

/*--------------------------------*/
/* Multi-byte word access macros  */

#if _WORD_ACCESS == 1   /* Enable word access to the FAT structure */
#define LD_WORD(ptr)        (uint16_t)(*(uint16_t*)(uint8_t*)(ptr))
#define LD_DWORD(ptr)       (uint32_t)(*(uint32_t*)(uint8_t*)(ptr))
#define ST_WORD(ptr,val)    *(uint16_t*)(uint8_t*)(ptr)=(uint16_t)(val)
#define ST_DWORD(ptr,val)   *(uint32_t*)(uint8_t*)(ptr)=(uint32_t)(val)
#else                   /* Use byte-by-byte access to the FAT structure */
#define LD_WORD(ptr)        (uint16_t)(((uint16_t)*(uint8_t*)((ptr)+1)<<8)|(uint16_t)*(uint8_t*)(ptr))
#define LD_DWORD(ptr)       (uint32_t)(((uint32_t)*(uint8_t*)((ptr)+3)<<24)|((uint32_t)*(uint8_t*)((ptr)+2)<<16)|((uint16_t)*(uint8_t*)((ptr)+1)<<8)|*(uint8_t*)(ptr))
#define ST_WORD(ptr,val)    *(uint8_t*)(ptr)=(uint8_t)(val); *(uint8_t*)((ptr)+1)=(uint8_t)((uint16_t)(val)>>8)
#define ST_DWORD(ptr,val)   *(uint8_t*)(ptr)=(uint8_t)(val); *(uint8_t*)((ptr)+1)=(uint8_t)((uint16_t)(val)>>8); *(uint8_t*)((ptr)+2)=(uint8_t)((uint32_t)(val)>>16); *(uint8_t*)((ptr)+3)=(uint8_t)((uint32_t)(val)>>24)
#endif

#define LD_DWORD_BE(ptr)    (uint32_t)( \
							 (((uint32_t)(ptr)[0])<<24) | \
							 (((uint32_t)(ptr)[1])<<16) | \
							 (((uint32_t)(ptr)[2])<< 8) | \
							 ((uint32_t)(ptr)[3]) \
							)

#define ST_DWORD_BE(ptr, val)	(ptr)[0] = (uint8_t)((val) >> 24); \
								(ptr)[1] = (uint8_t)((val) >> 16); \
								(ptr)[2] = (uint8_t)((val) >> 8); \
								(ptr)[3] = (uint8_t)(val);



#define NR_OF_EL(a)		(sizeof(a) / sizeof(a[0]))
#define UNREFERENCED_PAR(a)	a=a

#define max(a,b) ((a>b)?a:b)

#endif // _INTEGER

