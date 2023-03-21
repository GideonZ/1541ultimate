#ifndef ENDIANNESS_H
#define ENDIANNESS_H

__inline uint32_t cpu_to_32le(uint32_t a)
{
    return a;
}

__inline uint16_t cpu_to_16le(uint16_t p)
{
    return p;
}

#define le2cpu(x)       cpu_to_16le(x)
#define cpu2le(x)       cpu_to_16le(x)
#define le16_to_cpu(x)  cpu_to_16le(x)
#define le_to_cpu_32(x) cpu_to_32le(x)

#define BYTE_ORDER LITTLE_ENDIAN

#endif /* ENDIANNESS_H */
