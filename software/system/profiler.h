#ifndef PROFILER_H
#define PROFILER_H

#define PROFILER_MATCH    (*(volatile unsigned char *)0x4000016)
#define PROFILER_SECTION  (*(volatile unsigned char *)0x4000017)
#define PROFILER_DATA(x)  (*(volatile unsigned long *)(0x4090000 + 4*x))

#endif
