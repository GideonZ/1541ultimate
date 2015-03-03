#ifndef _CC_H
#define _CC_H

#define LWIP_PROVIDE_ERRNO

//  Typedefs for the types used by lwip -
//    u8_t, s8_t, u16_t, s16_t, u32_t, s32_t, mem_ptr_t

/* These types must be 8-bit integer */
typedef signed char		s8_t;
typedef unsigned char	u8_t;

/* These types must be 16-bit integer */
typedef short			s16_t;
typedef unsigned short	u16_t;

/* These types must be 32-bit integer */
typedef long			s32_t;
typedef unsigned long	u32_t;

/* Pointer */
typedef unsigned long   mem_ptr_t;

// Structure packing
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_FIELD(x) x __attribute__((__packed__))
#define PACK_STRUCT_STRUCT __attribute__((__packed__))
#define PACK_STRUCT_END

#define BYTE_ORDER BIG_ENDIAN

// FIXME
#define LWIP_RAM_HEAP_POINTER ((void *)0xC00000)
//#define LWIP_MEM_ALIGN(x) x
//#define LWIP_MEM_ALIGN_SIZE(x) x

// Diag
#define LWIP_PLATFORM_DIAG(x)    { small_printf x ; }
#define LWIP_PLATFORM_ASSERT(x)  { small_printf(x) ; while(1); }
#include "small_printf.h"

/* Define (sn)printf formatters for these lwIP types */
#define X8_F  "b"
#define U16_F "d"
#define S16_F "d"
#define X16_F "4x"
#define U32_F "d"
#define S32_F "d"
#define X32_F "8x"

#endif
