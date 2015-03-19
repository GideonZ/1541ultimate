/*
 * Copyright (c) 2001-2003, Swedish Institute of Computer Science.
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 * 3. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 * $Id: cc.h,v 1.7 2009/09/09 21:34:59 bauerbach Exp $
 */
#ifndef __CC_H__
#define __CC_H__

#include <string.h>

//#define LWIP_PROVIDE_ERRNO

#define LWIP_MEM_ALIGN(addr) ((void *)((((mem_ptr_t)(addr) + MEM_ALIGNMENT - 1) & ~(mem_ptr_t)(MEM_ALIGNMENT-1))))

#define BYTE_ORDER BIG_ENDIAN

typedef unsigned   char    u8_t;
typedef signed     char    s8_t;
typedef unsigned   short   u16_t;
typedef signed     short   s16_t;
typedef unsigned   long    u32_t;
typedef signed     long    s32_t;

typedef u32_t mem_ptr_t;

typedef u32_t sys_prot_t;

/* Compiler hints for packing structures */
#define PACK_STRUCT_USE_INCLUDES
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((__packed__))

//#define ALIGN_STRUCT_8_BEGIN #pragma pack(1,8,0)
//#define ALIGN_STRUCT_END #pragma pack()
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x //__attribute__((__packed__))

/* prototypes for printf() and abort() */
#include <stdio.h>
#include <stdlib.h>

#define LWIP_PLATFORM_DIAG(x)    { printf x ; }
#define LWIP_PLATFORM_ASSERT(x)  { printf("Assertion \"%s\" failed at line %d in %s\n", \
                                     #x, __LINE__, __FILE__); for(;;); }

/* Plaform specific diagnostic output */
#ifndef LWIP_PLATFORM_DIAG
#define LWIP_PLATFORM_DIAG(x)	do {printf x;} while(0)
int printf( const char *fmt, ... );
#endif

#ifndef LWIP_PLATFORM_ASSERT
#define LWIP_PLATFORM_ASSERT(x) do {printf("Assertion \"%s\" failed at line %d in %s\n", \
                                     #x, __LINE__, __FILE__); for(;;) ;} while(0)
#endif

#define U16_F "d"
#define X16_F "x"
#define U32_F "d"
#define X32_F "x"
#define S16_F "d"
#define S32_F "d"

#define LWIP_PLATFORM_BYTESWAP 1

#define ___lswap(x) ((((x >> 24) & 0x000000ff)) | \
		  (((x >>  8) & 0x0000ff00)) | \
		  (((x) & 0x0000ff00) <<  8) | \
		  (((x) & 0x000000ff) << 24))
#define LWIP_PLATFORM_HTONL(l) (___lswap((l)))
#define LWIP_PLATFORM_HTONS(s) ((((s) & 0xFF00) >> 8) | (((s) & 0xFF) << 8))

#define LWIP_RAND() ((u32_t)rand())

#endif /* __CC_H__ */
