/*
 * profiler.c
 *
 *  Created on: Apr 22, 2015
 *      Author: Gideon
 */

#include "profiler.h"

void *profiled_memcpy(void *str1, const void *str2, size_t n)
{
	BYTE tmp = PROFILER_SUB;
	PROFILER_SUB = 14;
//	memcpy(str1, str2, n);
	DWORD src_addr = (DWORD)str2;
	DWORD dest_addr = (DWORD)str1;
	DWORD overlap = (src_addr | dest_addr);
	int remain = 0;
	int size;
	if ((overlap & 3) == 0) {
		remain = (n & 3);
		size = (n >> 2);
		DWORD *src = (DWORD *)str2;
		DWORD *dest = (DWORD *)str1;
		for (int i=0;i<size;i++) {
			*(dest++) = *(src++);
		}
		if (remain) {
			src_addr += (n & ~3);
			dest_addr += (n & ~3);
		}
	} else if((overlap & 1) == 0) {
		remain = (n & 1);
		size = (n >> 1);
		WORD *src = (WORD *)str2;
		WORD *dest = (WORD *)str1;
		for (int i=0;i<size;i++) {
			*(dest++) = *(src++);
		}
		if (remain) {
			src_addr += (n & ~1);
			dest_addr += (n & ~1);
		}
	} else {
		remain = n;
	}
	if (remain) {
		BYTE *src = (BYTE *)src_addr;
		BYTE *dest = (BYTE *)dest_addr;
		for(int i=0;i<remain;i++) {
			*(dest++) = *(src++);
		}
	}
	PROFILER_SUB = tmp;
}
