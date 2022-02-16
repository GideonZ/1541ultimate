#ifndef ENDIANNESS_H
#define ENDIANNESS_H

__inline uint32_t cpu_to_32le(uint32_t a)
{
    uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
}

__inline uint16_t cpu_to_16le(uint16_t p)
{
    uint16_t out = (p >> 8) | (p << 8);
    return out;
}

#define le2cpu(x)       cpu_to_16le(x)
#define cpu2le(x)       cpu_to_16le(x)
#define le16_to_cpu(x)  cpu_to_16le(x)
#define le_to_cpu_32(x) cpu_to_32le(x)

#endif /* ENDIANNESS_H */
