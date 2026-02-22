/*
##########################################################################
##                                                                      ##
##   RENE GARCIA                        GNU General Public Licence V3   ##
##                                                                      ##
##########################################################################
##                                                                      ##
##      Project : MPS Emulator                                          ##
##      Class   : MpsPrinter                                            ##
##      Piece   : mps_png.cc                                            ##
##      Language: C ANSI                                                ##
##      Author  : Rene GARCIA                                           ##
##                                                                      ##
##########################################################################
*/

/*-
 *
 *  This code is an extraction of lodepng written by Lode Vandevenne
 *  (https://lodev.org/lodepng/). The code has been modified to simplify
 *  it and to remove unnecessary parts. It has also been optimized to
 *  use much less memory.
 *
-*/

/******************************  Inclusions  ****************************/

#include <stdlib.h>
#include "mps_png.h"

#ifndef NOT_ULTIMATE
#include "iec_printer.h"    /* Only to handle progression bar while printing */
#include "FreeRTOS.h"
#endif /* NOT_ULTIMATE */

/*******************************  Constants  ****************************/

/*this is a good tradeoff between speed and compression ratio*/
#define DEFAULT_WINDOWSIZE 2048

/*the code length codes. 0-15: code lengths, 16: copy previous 3-6 times, 17: 3-10 zeros, 18: 11-138 zeros*/
#define NUM_CODE_LENGTH_CODES 19

#define FIRST_LENGTH_CODE_INDEX 257

/*the distance codes have their own symbols, 30 used, 2 unused*/
#define NUM_DISTANCE_SYMBOLS 32

/* amount of bits for first huffman table lookup (aka root bits), see HuffmanTree_makeTable and huffmanDecodeSymbol.*/
/* values 8u and 9u work the fastest */
#define FIRSTBITS 9u

/* a symbol value too big to represent any valid symbol, to indicate reading disallowed huffman bits combination,
which is possible in case of only 0 or 1 present symbols. */
#define INVALIDSYMBOL 65535u

/*256 literals, the end code, some length codes, and 2 unused codes*/
#define NUM_DEFLATE_CODE_SYMBOLS 288

/*3 bytes of data get encoded into two bytes. The hash cannot use more than 3
bytes as input because 3 is the minimum match length for deflate*/
static const unsigned HASH_NUM_VALUES = 65536;
static const unsigned HASH_BIT_MASK = 65535; /*HASH_NUM_VALUES - 1, but C90 does not like that as initializer*/

/* CRC polynomial: 0xedb88320 */
static unsigned lodepng_crc32_table[256] = {
           0u, 1996959894u, 3993919788u, 2567524794u,  124634137u, 1886057615u, 3915621685u, 2657392035u,
   249268274u, 2044508324u, 3772115230u, 2547177864u,  162941995u, 2125561021u, 3887607047u, 2428444049u,
   498536548u, 1789927666u, 4089016648u, 2227061214u,  450548861u, 1843258603u, 4107580753u, 2211677639u,
   325883990u, 1684777152u, 4251122042u, 2321926636u,  335633487u, 1661365465u, 4195302755u, 2366115317u,
   997073096u, 1281953886u, 3579855332u, 2724688242u, 1006888145u, 1258607687u, 3524101629u, 2768942443u,
   901097722u, 1119000684u, 3686517206u, 2898065728u,  853044451u, 1172266101u, 3705015759u, 2882616665u,
   651767980u, 1373503546u, 3369554304u, 3218104598u,  565507253u, 1454621731u, 3485111705u, 3099436303u,
   671266974u, 1594198024u, 3322730930u, 2970347812u,  795835527u, 1483230225u, 3244367275u, 3060149565u,
  1994146192u,   31158534u, 2563907772u, 4023717930u, 1907459465u,  112637215u, 2680153253u, 3904427059u,
  2013776290u,  251722036u, 2517215374u, 3775830040u, 2137656763u,  141376813u, 2439277719u, 3865271297u,
  1802195444u,  476864866u, 2238001368u, 4066508878u, 1812370925u,  453092731u, 2181625025u, 4111451223u,
  1706088902u,  314042704u, 2344532202u, 4240017532u, 1658658271u,  366619977u, 2362670323u, 4224994405u,
  1303535960u,  984961486u, 2747007092u, 3569037538u, 1256170817u, 1037604311u, 2765210733u, 3554079995u,
  1131014506u,  879679996u, 2909243462u, 3663771856u, 1141124467u,  855842277u, 2852801631u, 3708648649u,
  1342533948u,  654459306u, 3188396048u, 3373015174u, 1466479909u,  544179635u, 3110523913u, 3462522015u,
  1591671054u,  702138776u, 2966460450u, 3352799412u, 1504918807u,  783551873u, 3082640443u, 3233442989u,
  3988292384u, 2596254646u,   62317068u, 1957810842u, 3939845945u, 2647816111u,   81470997u, 1943803523u,
  3814918930u, 2489596804u,  225274430u, 2053790376u, 3826175755u, 2466906013u,  167816743u, 2097651377u,
  4027552580u, 2265490386u,  503444072u, 1762050814u, 4150417245u, 2154129355u,  426522225u, 1852507879u,
  4275313526u, 2312317920u,  282753626u, 1742555852u, 4189708143u, 2394877945u,  397917763u, 1622183637u,
  3604390888u, 2714866558u,  953729732u, 1340076626u, 3518719985u, 2797360999u, 1068828381u, 1219638859u,
  3624741850u, 2936675148u,  906185462u, 1090812512u, 3747672003u, 2825379669u,  829329135u, 1181335161u,
  3412177804u, 3160834842u,  628085408u, 1382605366u, 3423369109u, 3138078467u,  570562233u, 1426400815u,
  3317316542u, 2998733608u,  733239954u, 1555261956u, 3268935591u, 3050360625u,  752459403u, 1541320221u,
  2607071920u, 3965973030u, 1969922972u,   40735498u, 2617837225u, 3943577151u, 1913087877u,   83908371u,
  2512341634u, 3803740692u, 2075208622u,  213261112u, 2463272603u, 3855990285u, 2094854071u,  198958881u,
  2262029012u, 4057260610u, 1759359992u,  534414190u, 2176718541u, 4139329115u, 1873836001u,  414664567u,
  2282248934u, 4279200368u, 1711684554u,  285281116u, 2405801727u, 4167216745u, 1634467795u,  376229701u,
  2685067896u, 3608007406u, 1308918612u,  956543938u, 2808555105u, 3495958263u, 1231636301u, 1047427035u,
  2932959818u, 3654703836u, 1088359270u,  936918000u, 2847714899u, 3736837829u, 1202900863u,  817233897u,
  3183342108u, 3401237130u, 1404277552u,  615818150u, 3134207493u, 3453421203u, 1423857449u,  601450431u,
  3009837614u, 3294710456u, 1567103746u,  711928724u, 3020668471u, 3272380065u, 1510334235u,  755167117u
};

/*the base lengths represented by codes 257-285*/
static const unsigned LENGTHBASE[29]
  = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
     67, 83, 99, 115, 131, 163, 195, 227, 258};

/*the base backwards distances (the bits of distance codes appear after length codes and use their own huffman tree)*/
static const unsigned DISTANCEBASE[30]
  = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
     769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

/*the order in which "code length alphabet code lengths" are stored as specified by deflate, out of this the huffman
tree of the dynamic huffman tree lengths is generated*/
static const unsigned CLCL_ORDER[NUM_CODE_LENGTH_CODES]
  = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static const size_t MAX_SUPPORTED_DEFLATE_LENGTH = 258;

/*the extra bits used by codes 257-285 (added to base length)*/
static const unsigned LENGTHEXTRA[29]
  = {0, 0, 0, 0, 0, 0, 0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,
      4,  4,  4,   4,   5,   5,   5,   5,   0};

/*the extra bits of backwards distances (added to base)*/
static const unsigned DISTANCEEXTRA[30]
  = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,   6,   6,   7,   7,   8,
       8,    9,    9,   10,   10,   11,   11,   12,    12,    13,    13};


/********************************  Macros  ******************************/

/*Try the code, if it returns error, also return the error.*/
#define CERROR_TRY_RETURN(call){\
  unsigned error = call;\
  if(error) return error;\
}

/*
Often in case of an error a value is assigned to a variable and then it breaks
out of a loop (to go to the cleanup phase of a function). This macro does that.
It makes the error handling code shorter and more readable.

Example: if(!uivector_resize(&lz77_encoded, datasize)) ERROR_BREAK(83);
*/
#define CERROR_BREAK(errorvar, code){\
  errorvar = code;\
  break;\
}

/*version of CERROR_BREAK that assumes the common case where the error variable is named "error"*/
#define ERROR_BREAK(code) CERROR_BREAK(error, code)

#define LODEPNG_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define LODEPNG_MIN(a, b) (((a) < (b)) ? (a) : (b))
//#define LODEPNG_ABS(x) ((x) < 0 ? -(x) : (x))

/*TODO: this ignores potential out of memory errors*/
#define WRITEBIT(writer, bit){\
  /* append new byte */\
  if(((writer->bp) & 7u) == 0) {\
    if(!ucvector_resize(writer->data, writer->data->size + 1)) return;\
    writer->data->data[writer->data->size - 1] = 0;\
  }\
  (writer->data->data[writer->data->size - 1]) |= (bit << ((writer->bp) & 7u));\
  ++writer->bp;\
}

/*********************************  Types  ******************************/
/*dynamic vector of unsigned chars*/
typedef struct ucvector
{
    unsigned char* data;
    size_t size;            /*used size*/
    size_t allocsize;       /*allocated size*/
} ucvector;

/*dynamic vector of unsigned ints*/
typedef struct uivector
{
    unsigned* data;
    size_t size;            /*size in number of unsigned longs*/
    size_t allocsize;       /*allocated size in bytes*/
} uivector;

typedef struct Hash
{
    int* head;              /*hash value to head circular pos - can be outdated if went around window*/

    /*circular pos to prev circular pos*/
    unsigned short* chain;
    int* val;               /*circular pos to hash value*/

                            /*TODO: do this not only for zeros but for any repeated byte. However for PNG
                                    it's always going to be the zeros that dominate, so not important for PNG*/
    int* headz;             /*similar to head, but for chainz*/
    unsigned short* chainz; /*those with same amount of zeros*/
    unsigned short* zeros;  /*length of zeros streak, used as a second hash chain*/
} Hash;

typedef struct
{
    ucvector* data;
    unsigned char bp;       /*ok to overflow, indicates bit pos inside byte*/
} LodePNGBitWriter;

typedef struct HuffmanTree
{
    unsigned* codes;        /*the huffman codes (bit patterns representing the symbols)*/
    unsigned* lengths;      /*the lengths of the huffman codes*/
    unsigned maxbitlen;     /*maximum number of bits a single code can get*/
    unsigned numcodes;  /*number of symbols in the alphabet = number of codes*/

    /* for reading only */
    unsigned char* table_len; /*length of symbol from lookup table, or max length if secondary lookup needed*/
    unsigned short* table_value; /*value of symbol from lookup table, or pointer to secondary table if needed*/
} HuffmanTree;

typedef struct BPMNode
{
    int weight;             /*the sum of all weights in this chain*/
    unsigned index;         /*index of this leaf node (called "count" in the paper)*/
    struct BPMNode* tail;   /*the next nodes in this chain (null if last)*/
    int in_use;
} BPMNode;

/*lists of chains*/
typedef struct BPMLists
{
    /*memory pool*/
    unsigned memsize;
    BPMNode* memory;
    unsigned numfree;
    unsigned nextfree;
    BPMNode** freelist;

    /*two heads of lookahead chains per list*/
    unsigned listsize;
    BPMNode** chains0;
    BPMNode** chains1;
} BPMLists;

/*********************************  Functions  ******************************/

static void HuffmanTree_init(HuffmanTree* tree)
{
    tree->codes = 0;
    tree->lengths = 0;
    tree->table_len = 0;
    tree->table_value = 0;
}

static void LodePNGBitWriter_init(LodePNGBitWriter* writer, ucvector* data)
{
    writer->data = data;
    writer->bp = 0;
}

/*returns 1 if success, 0 if failure ==> nothing done*/
static unsigned ucvector_reserve(ucvector* p, size_t size)
{
    if(size > p->allocsize)
    {
        size_t newsize = size + (p->allocsize >> 1u);
        void* data = realloc(p->data, newsize);

        if(data)
        {
            p->allocsize = newsize;
            p->data = (unsigned char*)data;
        }
        else
            return 0; /*error: not enough memory*/
    }

    return 1; /*success*/
}

/*returns 1 if success, 0 if failure ==> nothing done*/
static unsigned ucvector_resize(ucvector* p, size_t size)
{
    p->size = size;
    return ucvector_reserve(p, size);
}

static ucvector ucvector_init(unsigned char* buffer, size_t size)
{
    ucvector v;
    v.data = buffer;
    v.allocsize = v.size = size;
    return v;
}

static void uivector_cleanup(void* p)
{
    ((uivector*)p)->size = ((uivector*)p)->allocsize = 0;
    free(((uivector*)p)->data);
    ((uivector*)p)->data = NULL;
}

/*returns 1 if success, 0 if failure ==> nothing done*/
static unsigned uivector_resize(uivector* p, size_t size)
{
    size_t allocsize = size * sizeof(unsigned);

    if(allocsize > p->allocsize)
    {
        size_t newsize = allocsize + (p->allocsize >> 1u);
        void* data = realloc(p->data, newsize);

        if(data)
        {
            p->allocsize = newsize;
            p->data = (unsigned*)data;
        }
        else
            return 0; /*error: not enough memory*/
    }

    p->size = size;

    return 1; /*success*/
}

/*returns 1 if success, 0 if failure ==> nothing done*/
static unsigned uivector_push_back(uivector* p, unsigned c)
{
    if(!uivector_resize(p, p->size + 1))
        return 0;

    p->data[p->size - 1] = c;

    return 1;
}

static void uivector_init(uivector* p)
{
    p->data = NULL;
    p->size = p->allocsize = 0;
}

static unsigned lodepng_read32bitInt(const unsigned char* buffer)
{
    return (((unsigned)buffer[0] << 24u) | ((unsigned)buffer[1] << 16u) |
            ((unsigned)buffer[2] << 8u) | (unsigned)buffer[3]);
}

/*buffer must have at least 4 allocated bytes available*/
static void lodepng_set32bitInt(unsigned char* buffer, unsigned value)
{
  buffer[0] = (unsigned char)((value >> 24) & 0xff);
  buffer[1] = (unsigned char)((value >> 16) & 0xff);
  buffer[2] = (unsigned char)((value >>  8) & 0xff);
  buffer[3] = (unsigned char)((value      ) & 0xff);
}

unsigned lodepng_chunk_length(const unsigned char* chunk)
{
    return lodepng_read32bitInt(chunk);
}

/*Return the CRC of the bytes buf[0..len-1].*/
unsigned lodepng_crc32(const unsigned char* data, size_t length)
{
    unsigned r = 0xffffffffu;
    size_t i;

    for(i = 0; i < length; ++i)
    {
        r = lodepng_crc32_table[(r ^ data[i]) & 0xffu] ^ (r >> 8u);
    }

    return r ^ 0xffffffffu;
}

void lodepng_chunk_generate_crc(unsigned char* chunk)
{
    unsigned length = lodepng_chunk_length(chunk);
    unsigned CRC = lodepng_crc32(&chunk[4], length + 4);
    lodepng_set32bitInt(chunk + 8 + length, CRC);
}

static unsigned writeSignature(ucvector* out)
{
    size_t pos = out->size;
    const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};

    /*8 bytes PNG signature, aka the magic bytes*/
    if(!ucvector_resize(out, out->size + 8))
        return 83; /*alloc fail*/

    memcpy(out->data + pos, signature, 8);

    return 0;
}

/* Safely check if adding two integers will overflow (no undefined
behavior, compiler removing the code, etc...) and output result. */
static int lodepng_addofl(size_t a, size_t b, size_t* result)
{
    *result = a + b; /* Unsigned addition is well defined and safe in C90 */
    return *result < a;
}

/*Sets length and name and allocates the space for data and crc but does not
set data or crc yet. Returns the start of the chunk in chunk. The start of
the data is at chunk + 8. To finalize chunk, add the data, then use
lodepng_chunk_generate_crc */
static unsigned lodepng_chunk_init(unsigned char** chunk,
                                   ucvector* out,
                                   unsigned length, const char* type)
{
    size_t new_length = out->size;

    if(lodepng_addofl(new_length, length, &new_length))
        return 77;

    if(lodepng_addofl(new_length, 12, &new_length))
        return 77;

    if(!ucvector_resize(out, new_length))
        return 83; /*alloc fail*/

    *chunk = out->data + new_length - length - 12u;

    /*1: length*/
    lodepng_set32bitInt(*chunk, length);

    /*2: chunk name (4 letters)*/
    memcpy(*chunk + 4, type, 4);

    return 0;
}

/* like lodepng_chunk_create but with custom allocsize */
static unsigned lodepng_chunk_createv(ucvector* out,
                                      unsigned length, const char* type, const unsigned char* data)
{
    unsigned char* chunk;
    CERROR_TRY_RETURN(lodepng_chunk_init(&chunk, out, length, type));

    /*3: the data*/
    memcpy(chunk + 8, data, length);

    /*4: CRC (of the chunkname characters and the data)*/
    lodepng_chunk_generate_crc(chunk);

    return 0;
}

static unsigned addChunk_IHDR(ucvector* out, unsigned w, unsigned h,
                              LodePNGColorType colortype, unsigned bitdepth, unsigned interlace_method)
{
    unsigned char *chunk, *data;
    CERROR_TRY_RETURN(lodepng_chunk_init(&chunk, out, 13, "IHDR"));
    data = chunk + 8;

    lodepng_set32bitInt(data + 0, w); /*width*/
    lodepng_set32bitInt(data + 4, h); /*height*/
    data[8] = (unsigned char)bitdepth; /*bit depth*/
    data[9] = (unsigned char)colortype; /*color type*/
    data[10] = 0; /*compression method*/
    data[11] = 0; /*filter method*/
    data[12] = interlace_method; /*interlace method*/

    lodepng_chunk_generate_crc(chunk);

    return 0;
}

static unsigned addChunk_PLTE(ucvector* out, const LodePNGColorMode* info)
{
    unsigned char* chunk;
    size_t i, j = 8;

    if(info->palettesize == 0 || info->palettesize > 256)
    {
        return 68; /*invalid palette size, it is only allowed to be 1-256*/
    }

    CERROR_TRY_RETURN(lodepng_chunk_init(&chunk, out, info->palettesize * 3, "PLTE"));

    for(i = 0; i != info->palettesize; ++i)
    {
        /*add all channels except alpha channel*/
        chunk[j++] = info->palette[i * 4 + 0];
        chunk[j++] = info->palette[i * 4 + 1];
        chunk[j++] = info->palette[i * 4 + 2];
    }

    lodepng_chunk_generate_crc(chunk);

    return 0;
}

static unsigned addChunk_pHYs(ucvector* out, const LodePNGInfo* info)
{
    unsigned char* chunk;
    CERROR_TRY_RETURN(lodepng_chunk_init(&chunk, out, 9, "pHYs"));
    lodepng_set32bitInt(chunk + 8, info->phys_x);
    lodepng_set32bitInt(chunk + 12, info->phys_y);
    chunk[16] = info->phys_unit;
    lodepng_chunk_generate_crc(chunk);

    return 0;
}

static unsigned addChunk_IEND(ucvector* out)
{
    return lodepng_chunk_createv(out, 0, "IEND", 0);
}

static unsigned reverseBits(unsigned bits, unsigned num)
{
    /*TODO: implement faster lookup table based version when needed*/
    unsigned i, result = 0;

    for(i = 0; i < num; i++)
        result |= ((bits >> (num - i - 1u)) & 1u) << i;

    return result;
}

/* make table for huffman decoding */
static unsigned HuffmanTree_makeTable(HuffmanTree* tree)
{
    static const unsigned headsize = 1u << FIRSTBITS; /*size of the first table*/
    static const unsigned mask = (1u << FIRSTBITS) /*headsize*/ - 1u;
    size_t i, numpresent, pointer, size; /*total table size*/
    unsigned* maxlens = (unsigned*)malloc(headsize * sizeof(unsigned));

    if(!maxlens)
        return 83; /*alloc fail*/

    /* compute maxlens: max total bit length of symbols sharing prefix in the first table*/
    memset(maxlens, 0, headsize * sizeof(*maxlens));

    for(i = 0; i < tree->numcodes; i++)
    {
        unsigned symbol = tree->codes[i];
        unsigned l = tree->lengths[i];
        unsigned index;

        if(l <= FIRSTBITS)
            continue; /*symbols that fit in first table don't increase secondary table size*/

        /*get the FIRSTBITS MSBs, the MSBs of the symbol are encoded first. See later comment about the reversing*/
        index = reverseBits(symbol >> (l - FIRSTBITS), FIRSTBITS);
        maxlens[index] = LODEPNG_MAX(maxlens[index], l);
    }

    /* compute total table size: size of first table plus all secondary tables for symbols longer than FIRSTBITS */
    size = headsize;

    for(i = 0; i < headsize; ++i)
    {
        unsigned l = maxlens[i];

        if(l > FIRSTBITS)
            size += (1u << (l - FIRSTBITS));
    }

    tree->table_len = (unsigned char*)malloc(size * sizeof(*tree->table_len));
    tree->table_value = (unsigned short*)malloc(size * sizeof(*tree->table_value));

    if(!tree->table_len || !tree->table_value)
    {
        free(maxlens);
        /* freeing tree->table values is done at a higher scope */
        return 83; /*alloc fail*/
    }

    /*initialize with an invalid length to indicate unused entries*/
    for(i = 0; i < size; ++i)
        tree->table_len[i] = 16;

    /*fill in the first table for long symbols: max prefix size and pointer to secondary tables*/
    pointer = headsize;

    for(i = 0; i < headsize; ++i)
    {
        unsigned l = maxlens[i];

        if(l <= FIRSTBITS)
            continue;

        tree->table_len[i] = l;
        tree->table_value[i] = pointer;
        pointer += (1u << (l - FIRSTBITS));
    }

    free(maxlens);

    /*fill in the first table for short symbols, or secondary table for long symbols*/
    numpresent = 0;

    for(i = 0; i < tree->numcodes; ++i)
    {
        unsigned l = tree->lengths[i];
        unsigned symbol, reverse;

        if(l == 0)
            continue;

        symbol = tree->codes[i]; /*the huffman bit pattern. i itself is the value.*/
        /*reverse bits, because the huffman bits are given in MSB first order but the bit reader reads LSB first*/
        reverse = reverseBits(symbol, l);
        numpresent++;

        if(l <= FIRSTBITS)
        {
            /*short symbol, fully in first table, replicated num times if l < FIRSTBITS*/
            unsigned num = 1u << (FIRSTBITS - l);
            unsigned j;

            for(j = 0; j < num; ++j)
            {
                /*bit reader will read the l bits of symbol first, the remaining FIRSTBITS - l bits go to the MSB's*/
                unsigned index = reverse | (j << l);

                if(tree->table_len[index] != 16)
                    return 55; /*invalid tree: long symbol shares prefix with short symbol*/

                tree->table_len[index] = l;
                tree->table_value[index] = i;
            }
        }
        else
        {
            /*long symbol, shares prefix with other long symbols in first lookup table, needs second lookup*/
            /*the FIRSTBITS MSBs of the symbol are the first table index*/
            unsigned index = reverse & mask;
            unsigned maxlen = tree->table_len[index];
            /*log2 of secondary table length, should be >= l - FIRSTBITS*/
            unsigned tablelen = maxlen - FIRSTBITS;
            unsigned start = tree->table_value[index]; /*starting index in secondary table*/
            unsigned num = 1u << (tablelen - (l - FIRSTBITS)); /*amount of entries of this symbol in secondary table*/
            unsigned j;

            if(maxlen < l)
                return 55; /*invalid tree: long symbol shares prefix with short symbol*/

            for(j = 0; j < num; ++j)
            {
                unsigned reverse2 = reverse >> FIRSTBITS; /* l - FIRSTBITS bits */
                unsigned index2 = start + (reverse2 | (j << (l - FIRSTBITS)));
                tree->table_len[index2] = l;
                tree->table_value[index2] = i;
            }
        }
    }

    if(numpresent < 2)
    {
        /* In case of exactly 1 symbol, in theory the huffman symbol needs 0 bits,
           but deflate uses 1 bit instead. In case of 0 symbols, no symbols can
           appear at all, but such huffman tree could still exist (e.g. if distance
           codes are never used). In both cases, not all symbols of the table will be
           filled in. Fill them in with an invalid symbol value so returning them from
           huffmanDecodeSymbol will cause error. */

        for(i = 0; i < size; ++i)
        {
            if(tree->table_len[i] == 16)
            {
                /* As length, use a value smaller than FIRSTBITS for the head table,
                   and a value larger than FIRSTBITS for the secondary table, to ensure
                   valid behavior for advanceBits when reading this symbol. */

                tree->table_len[i] = (i < headsize) ? 1 : (FIRSTBITS + 1);
                tree->table_value[i] = INVALIDSYMBOL;
            }
        }
    }
    else
    {
        /* A good huffman tree has N * 2 - 1 nodes, of which N - 1 are internal nodes.
           If that is not the case (due to too long length codes), the table will not
           have been fully used, and this is an error (not all bit combinations can be
           decoded): an oversubscribed huffman tree, indicated by error 55. */

        for(i = 0; i < size; ++i)
        {
            if(tree->table_len[i] == 16)
                return 55;
        }
    }

    return 0;
}

/*
Second step for the ...makeFromLengths and ...makeFromFrequencies functions.
numcodes, lengths and maxbitlen must already be filled in correctly. return
value is error.
*/
static unsigned HuffmanTree_makeFromLengths2(HuffmanTree* tree)
{
    unsigned* blcount;
    unsigned* nextcode;
    unsigned error = 0;
    unsigned bits, n;

    tree->codes = (unsigned*)malloc(tree->numcodes * sizeof(unsigned));
    blcount = (unsigned*)malloc((tree->maxbitlen + 1) * sizeof(unsigned));
    nextcode = (unsigned*)malloc((tree->maxbitlen + 1) * sizeof(unsigned));

    if(!tree->codes || !blcount || !nextcode)
        error = 83; /*alloc fail*/

    if(!error)
    {
        for(n = 0; n != tree->maxbitlen + 1; n++)
            blcount[n] = nextcode[n] = 0;

        /*step 1: count number of instances of each code length*/
        for(bits = 0; bits != tree->numcodes; ++bits)
            ++blcount[tree->lengths[bits]];

        /*step 2: generate the nextcode values*/
        for(bits = 1; bits <= tree->maxbitlen; ++bits)
        {
            nextcode[bits] = (nextcode[bits - 1] + blcount[bits - 1]) << 1u;
        }

        /*step 3: generate all the codes*/
        for(n = 0; n != tree->numcodes; ++n)
        {
            if(tree->lengths[n] != 0)
            {
                tree->codes[n] = nextcode[tree->lengths[n]]++;
                /*remove superfluous bits from the code*/
                tree->codes[n] &= ((1u << tree->lengths[n]) - 1u);
            }
        }
    }

    free(blcount);
    free(nextcode);

    if(!error)
        error = HuffmanTree_makeTable(tree);

    return error;
}

/*
given the code lengths (as stored in the PNG file), generate the tree as defined
by Deflate. maxbitlen is the maximum bits that a code in the tree can have.
return value is error.
*/
static unsigned HuffmanTree_makeFromLengths(HuffmanTree* tree, const unsigned* bitlen,
                                            size_t numcodes, unsigned maxbitlen)
{
    unsigned i;

    tree->lengths = (unsigned*)malloc(numcodes * sizeof(unsigned));

    if(!tree->lengths)
        return 83; /*alloc fail*/

    for(i = 0; i != numcodes; ++i)
        tree->lengths[i] = bitlen[i];

    tree->numcodes = (unsigned)numcodes; /*number of symbols*/
    tree->maxbitlen = maxbitlen;

    return HuffmanTree_makeFromLengths2(tree);
}

/* LSB of value is written first, and LSB of bytes is used first */
static void writeBits(LodePNGBitWriter* writer, unsigned value, size_t nbits)
{
    if(nbits == 1)
    { /* compiler should statically compile this case if nbits == 1 */
        WRITEBIT(writer, value);
    }
    else
    {
        /* TODO: increase output size only once here rather than in each WRITEBIT */
        size_t i;

        for(i = 0; i != nbits; ++i)
        {
            WRITEBIT(writer, (unsigned char)((value >> i) & 1));
        }
    }
}

/* This one is to use for adding huffman symbol, the value bits are written MSB first */
static void writeBitsReversed(LodePNGBitWriter* writer, unsigned value, size_t nbits)
{
    size_t i;

    for(i = 0; i != nbits; ++i)
    {
        /* TODO: increase output size only once here rather than in each WRITEBIT */
        WRITEBIT(writer, (unsigned char)((value >> (nbits - 1u - i)) & 1u));
    }
}

/*
write the lz77-encoded data, which has lit, len and dist codes, to compressed stream using huffman trees.
tree_ll: the tree for lit and len codes.
tree_d: the tree for distance codes.
*/
static void writeLZ77data(LodePNGBitWriter* writer, const uivector* lz77_encoded,
                          const HuffmanTree* tree_ll, const HuffmanTree* tree_d)
{
    size_t i = 0;

    for(i = 0; i != lz77_encoded->size; ++i)
    {
        unsigned val = lz77_encoded->data[i];
        writeBitsReversed(writer, tree_ll->codes[val], tree_ll->lengths[val]);

        if(val > 256) /*for a length code, 3 more things have to be added*/
        {
            unsigned length_index = val - FIRST_LENGTH_CODE_INDEX;
            unsigned n_length_extra_bits = LENGTHEXTRA[length_index];
            unsigned length_extra_bits = lz77_encoded->data[++i];

            unsigned distance_code = lz77_encoded->data[++i];

            unsigned distance_index = distance_code;
            unsigned n_distance_extra_bits = DISTANCEEXTRA[distance_index];
            unsigned distance_extra_bits = lz77_encoded->data[++i];

            writeBits(writer, length_extra_bits, n_length_extra_bits);
            writeBitsReversed(writer, tree_d->codes[distance_code], tree_d->lengths[distance_code]);
            writeBits(writer, distance_extra_bits, n_distance_extra_bits);
        }
    }
}

static void HuffmanTree_cleanup(HuffmanTree* tree)
{
    free(tree->codes);
    free(tree->lengths);
    free(tree->table_len);
    free(tree->table_value);
}

/*creates a new chain node with the given parameters, from the memory in the lists */
static BPMNode* bpmnode_create(BPMLists* lists, int weight, unsigned index, BPMNode* tail)
{
    unsigned i;
    BPMNode* result;

    /*memory full, so garbage collect*/

    if(lists->nextfree >= lists->numfree)
    {
        /*mark only those that are in use*/
        for(i = 0; i != lists->memsize; ++i)
            lists->memory[i].in_use = 0;

        for(i = 0; i != lists->listsize; ++i)
        {
            BPMNode* node;

            for(node = lists->chains0[i]; node != 0; node = node->tail)
                node->in_use = 1;

            for(node = lists->chains1[i]; node != 0; node = node->tail)
                node->in_use = 1;
        }

        /*collect those that are free*/
        lists->numfree = 0;

        for(i = 0; i != lists->memsize; ++i)
        {
            if(!lists->memory[i].in_use)
                lists->freelist[lists->numfree++] = &lists->memory[i];
        }

        lists->nextfree = 0;
    }

    result = lists->freelist[lists->nextfree++];
    result->weight = weight;
    result->index = index;
    result->tail = tail;

    return result;
}

/*Boundary Package Merge step, numpresent is the amount of leaves, and c is the current chain.*/
static void boundaryPM(BPMLists* lists, BPMNode* leaves, size_t numpresent, int c, int num)
{
    unsigned lastindex = lists->chains1[c]->index;

    if(c == 0)
    {
        if(lastindex >= numpresent)
            return;

        lists->chains0[c] = lists->chains1[c];
        lists->chains1[c] = bpmnode_create(lists, leaves[lastindex].weight, lastindex + 1, 0);
    }
    else
    {
        /*sum of the weights of the head nodes of the previous lookahead chains.*/
        int sum = lists->chains0[c - 1]->weight + lists->chains1[c - 1]->weight;
        lists->chains0[c] = lists->chains1[c];

        if(lastindex < numpresent && sum > leaves[lastindex].weight)
        {
            lists->chains1[c] = bpmnode_create(lists, leaves[lastindex].weight, lastindex + 1, lists->chains1[c]->tail);
            return;
        }

        lists->chains1[c] = bpmnode_create(lists, sum, lastindex, lists->chains1[c - 1]);

        /*in the end we are only interested in the chain of the last list, so no
          need to recurse if we're at the last one (this gives measurable speedup)*/

        if(num + 1 < (int)(2 * numpresent - 2))
        {
            boundaryPM(lists, leaves, numpresent, c - 1, num);
            boundaryPM(lists, leaves, numpresent, c - 1, num);
        }
    }
}

/*sort the leaves with stable mergesort*/
static void bpmnode_sort(BPMNode* leaves, size_t num)
{
    BPMNode* mem = (BPMNode*)malloc(sizeof(*leaves) * num);
    size_t width, counter = 0;

    for(width = 1; width < num; width *= 2)
    {
        BPMNode* a = (counter & 1) ? mem : leaves;
        BPMNode* b = (counter & 1) ? leaves : mem;
        size_t p;

        for(p = 0; p < num; p += 2 * width)
        {
            size_t q = (p + width > num) ? num : (p + width);
            size_t r = (p + 2 * width > num) ? num : (p + 2 * width);
            size_t i = p, j = q, k;

            for(k = p; k < r; k++)
            {
                if(i < q && (j >= r || a[i].weight <= a[j].weight))
                    b[k] = a[i++];
                else
                    b[k] = a[j++];
            }
        }

        counter++;
    }

    if(counter & 1)
        memcpy(leaves, mem, sizeof(*leaves) * num);

    free(mem);
}

unsigned lodepng_huffman_code_lengths(unsigned* lengths, const unsigned* frequencies,
                                      size_t numcodes, unsigned maxbitlen)
{
    unsigned error = 0;
    unsigned i;
    size_t numpresent = 0; /*number of symbols with non-zero frequency*/
    BPMNode* leaves; /*the symbols, only those with > 0 frequency*/

    if(numcodes == 0)
        return 80; /*error: a tree of 0 symbols is not supposed to be made*/

    if((1u << maxbitlen) < (unsigned)numcodes)
        return 80; /*error: represent all symbols*/

    leaves = (BPMNode*)malloc(numcodes * sizeof(*leaves));

    if(!leaves)
        return 83; /*alloc fail*/

    for(i = 0; i != numcodes; ++i)
    {
        if(frequencies[i] > 0)
        {
            leaves[numpresent].weight = (int)frequencies[i];
            leaves[numpresent].index = i;
            ++numpresent;
        }
    }

    memset(lengths, 0, numcodes * sizeof(*lengths));

    /*ensure at least two present symbols. There should be at least one symbol
      according to RFC 1951 section 3.2.7. Some decoders incorrectly require two. To
      make these work as well ensure there are at least two symbols. The
      Package-Merge code below also doesn't work correctly if there's only one
      symbol, it'd give it the theoretical 0 bits but in practice zlib wants 1 bit*/

    if(numpresent == 0)
    {
        lengths[0] = lengths[1] = 1; /*note that for RFC 1951 section 3.2.7, only lengths[0] = 1 is needed*/
    }
    else if(numpresent == 1)
    {
        lengths[leaves[0].index] = 1;
        lengths[leaves[0].index == 0 ? 1 : 0] = 1;
    }
    else
    {
        BPMLists lists;
        BPMNode* node;

        bpmnode_sort(leaves, numpresent);

        lists.listsize = maxbitlen;
        lists.memsize = 2 * maxbitlen * (maxbitlen + 1);
        lists.nextfree = 0;
        lists.numfree = lists.memsize;
        lists.memory = (BPMNode*)malloc(lists.memsize * sizeof(*lists.memory));
        lists.freelist = (BPMNode**)malloc(lists.memsize * sizeof(BPMNode*));
        lists.chains0 = (BPMNode**)malloc(lists.listsize * sizeof(BPMNode*));
        lists.chains1 = (BPMNode**)malloc(lists.listsize * sizeof(BPMNode*));

        if(!lists.memory || !lists.freelist || !lists.chains0 || !lists.chains1)
            error = 83; /*alloc fail*/

        if(!error)
        {
            for(i = 0; i != lists.memsize; ++i)
                lists.freelist[i] = &lists.memory[i];

            bpmnode_create(&lists, leaves[0].weight, 1, 0);
            bpmnode_create(&lists, leaves[1].weight, 2, 0);

            for(i = 0; i != lists.listsize; ++i)
            {
                lists.chains0[i] = &lists.memory[0];
                lists.chains1[i] = &lists.memory[1];
            }

            /*each boundaryPM call adds one chain to the last list, and we need 2 * numpresent - 2 chains.*/
            for(i = 2; i != 2 * numpresent - 2; ++i)
                boundaryPM(&lists, leaves, numpresent, (int)maxbitlen - 1, (int)i);

            for(node = lists.chains1[maxbitlen - 1]; node; node = node->tail)
            {
                for(i = 0; i != node->index; ++i)
                    ++lengths[leaves[i].index];
            }
        }

        free(lists.memory);
        free(lists.freelist);
        free(lists.chains0);
        free(lists.chains1);
    }

    free(leaves);

    return error;
}

/*Create the Huffman tree given the symbol frequencies*/
static unsigned HuffmanTree_makeFromFrequencies(HuffmanTree* tree, const unsigned* frequencies,
                                                size_t mincodes, size_t numcodes, unsigned maxbitlen)
{
    unsigned error = 0;

    while(!frequencies[numcodes - 1] && numcodes > mincodes)
        --numcodes; /*trim zeroes*/

    tree->lengths = (unsigned*)malloc(numcodes * sizeof(unsigned));

    if(!tree->lengths)
        return 83; /*alloc fail*/

    tree->maxbitlen = maxbitlen;
    tree->numcodes = (unsigned)numcodes; /*number of symbols*/

    error = lodepng_huffman_code_lengths(tree->lengths, frequencies, numcodes, maxbitlen);

    if(!error)
        error = HuffmanTree_makeFromLengths2(tree);

    return error;
}

/*search the index in the array, that has the largest value smaller than or equal to the given value,
given array must be sorted (if no value is smaller, it returns the size of the given array)*/
static size_t searchCodeIndex(const unsigned* array, size_t array_size, size_t value)
{
    /*binary search (only small gain over linear). TODO: use CPU log2 instruction for getting symbols instead*/
    size_t left = 1;
    size_t right = array_size - 1;

    while(left <= right)
    {
        size_t mid = (left + right) >> 1;

        if(array[mid] >= value)
            right = mid - 1;
        else
            left = mid + 1;
    }

    if(left >= array_size || array[left] > value)
        left--;

    return left;
}

static void addLengthDistance(uivector* values, size_t length, size_t distance)
{
    /*values in encoded vector are those used by deflate:
      0-255: literal bytes
      256: end
      257-285: length/distance pair (length code, followed by extra length bits, distance code, extra distance bits)
      286-287: invalid*/

    unsigned length_code = (unsigned)searchCodeIndex(LENGTHBASE, 29, length);
    unsigned extra_length = (unsigned)(length - LENGTHBASE[length_code]);
    unsigned dist_code = (unsigned)searchCodeIndex(DISTANCEBASE, 30, distance);
    unsigned extra_distance = (unsigned)(distance - DISTANCEBASE[dist_code]);

    size_t pos = values->size;
    /*TODO: return error when this fails (out of memory)*/
    unsigned ok = uivector_resize(values, values->size + 4);

    if(ok)
    {
        values->data[pos + 0] = length_code + FIRST_LENGTH_CODE_INDEX;
        values->data[pos + 1] = extra_length;
        values->data[pos + 2] = dist_code;
        values->data[pos + 3] = extra_distance;
    }
}

/*wpos = pos & (windowsize - 1)*/
static void updateHashChain(Hash* hash, size_t wpos, unsigned hashval, unsigned short numzeros)
{
    hash->val[wpos] = (int)hashval;

    if(hash->head[hashval] != -1)
        hash->chain[wpos] = hash->head[hashval];

    hash->head[hashval] = (int)wpos;
    hash->zeros[wpos] = numzeros;

    if(hash->headz[numzeros] != -1)
        hash->chainz[wpos] = hash->headz[numzeros];

    hash->headz[numzeros] = (int)wpos;
}

static unsigned getHash(const unsigned char* data, size_t size, size_t pos)
{
    unsigned result = 0;

    if(pos + 2 < size)
    {
        /*A simple shift and xor hash is used. Since the data of PNGs is dominated
          by zeroes due to the filters, a better hash does not have a significant
          effect on speed in traversing the chain, and causes more time spend on
          calculating the hash.*/

        result ^= ((unsigned)data[pos + 0] << 0u);
        result ^= ((unsigned)data[pos + 1] << 4u);
        result ^= ((unsigned)data[pos + 2] << 8u);
    }
    else
    {
        size_t amount, i;

        if(pos >= size)
            return 0;

        amount = size - pos;

        for(i = 0; i != amount; ++i)
            result ^= ((unsigned)data[pos + i] << (i * 8u));
    }

    return result & HASH_BIT_MASK;
}

static unsigned countZeros(const unsigned char* data, size_t size, size_t pos)
{
    const unsigned char* start = data + pos;
    const unsigned char* end = start + MAX_SUPPORTED_DEFLATE_LENGTH;

    if(end > data + size)
        end = data + size;

    data = start;

    while(data != end && *data == 0)
        ++data;

    /*subtracting two addresses returned as 32-bit number (max value is MAX_SUPPORTED_DEFLATE_LENGTH)*/
    return (unsigned)(data - start);
}

/*
LZ77-encode the data. Return value is error code. The input are raw bytes, the output
is in the form of unsigned integers with codes representing for example literal bytes, or
length/distance pairs.
It uses a hash table technique to let it encode faster. When doing LZ77 encoding, a
sliding window (of windowsize) is used, and all past bytes in that window can be used as
the "dictionary". A brute force search through all possible distances would be slow, and
this hash technique is one out of several ways to speed this up.
*/
static unsigned encodeLZ77(uivector* out, Hash* hash,
                           const unsigned char* in, size_t inpos, size_t insize, unsigned windowsize,
                           unsigned minmatch, unsigned nicematch, unsigned lazymatching)
{
    size_t pos;
    unsigned i, error = 0;

    /*for large window lengths, assume the user wants no compression loss. Otherwise, max hash chain length speedup.*/
    unsigned maxchainlength = windowsize >= 8192 ? windowsize : windowsize / 8u;
    unsigned maxlazymatch = windowsize >= 8192 ? MAX_SUPPORTED_DEFLATE_LENGTH : 64;

    unsigned usezeros = 1; /*not sure if setting it to false for windowsize < 8192 is better or worse*/
    unsigned numzeros = 0;

    unsigned offset; /*the offset represents the distance in LZ77 terminology*/
    unsigned length;
    unsigned lazy = 0;
    unsigned lazylength = 0, lazyoffset = 0;
    unsigned hashval;
    unsigned current_offset, current_length;
    unsigned prev_offset;
    const unsigned char *lastptr, *foreptr, *backptr;
    unsigned hashpos;

    if(windowsize == 0 || windowsize > 32768)
        return 60; /*error: windowsize smaller/larger than allowed*/

    if((windowsize & (windowsize - 1)) != 0)
        return 90; /*error: must be power of two*/

    if(nicematch > MAX_SUPPORTED_DEFLATE_LENGTH)
        nicematch = MAX_SUPPORTED_DEFLATE_LENGTH;

    for(pos = inpos; pos < insize; ++pos)
    {
        size_t wpos = pos & (windowsize - 1); /*position for in 'circular' hash buffers*/
        unsigned chainlength = 0;

        hashval = getHash(in, insize, pos);

        if(usezeros && hashval == 0)
        {
            if(numzeros == 0)
                numzeros = countZeros(in, insize, pos);
            else
                if(pos + numzeros > insize || in[pos + numzeros - 1] != 0)
                    --numzeros;
        }
        else
        {
            numzeros = 0;
        }

        updateHashChain(hash, wpos, hashval, numzeros);

        /*the length and offset found for the current position*/
        length = 0;
        offset = 0;

        hashpos = hash->chain[wpos];

        lastptr = &in[insize < pos + MAX_SUPPORTED_DEFLATE_LENGTH ? insize : pos + MAX_SUPPORTED_DEFLATE_LENGTH];

        /*search for the longest string*/
        prev_offset = 0;

        for(;;)
        {
            if(chainlength++ >= maxchainlength)
                break;

            current_offset = (unsigned)(hashpos <= wpos ? wpos - hashpos : wpos - hashpos + windowsize);

            if(current_offset < prev_offset)
                break; /*stop when went completely around the circular buffer*/

            prev_offset = current_offset;

            if(current_offset > 0)
            {
                /*test the next characters*/
                foreptr = &in[pos];
                backptr = &in[pos - current_offset];

                /*common case in PNGs is lots of zeros. Quickly skip over them as a speedup*/
                if(numzeros >= 3)
                {
                    unsigned skip = hash->zeros[hashpos];

                    if(skip > numzeros)
                        skip = numzeros;

                    backptr += skip;
                    foreptr += skip;
                }

                while(foreptr != lastptr && *backptr == *foreptr) /*maximum supported length by deflate is max length*/
                {
                    ++backptr;
                    ++foreptr;
                }

                current_length = (unsigned)(foreptr - &in[pos]);

                if(current_length > length)
                {
                    length = current_length; /*the longest length*/
                    offset = current_offset; /*the offset that is related to this longest length*/

                    /*jump out once a length of max length is found (speed gain). This also jumps
                      out if length is MAX_SUPPORTED_DEFLATE_LENGTH*/
                    if(current_length >= nicematch)
                        break;
                }
            }

            if(hashpos == hash->chain[hashpos])
                break;

            if(numzeros >= 3 && length > numzeros)
            {
                hashpos = hash->chainz[hashpos];

                if(hash->zeros[hashpos] != numzeros)
                    break;
            }
            else
            {
                hashpos = hash->chain[hashpos];
                /*outdated hash value, happens if particular value was not encountered in whole last window*/
                if(hash->val[hashpos] != (int)hashval)
                    break;
            }
        }

        if(lazymatching)
        {
            if(!lazy && length >= 3 && length <= maxlazymatch && length < MAX_SUPPORTED_DEFLATE_LENGTH)
            {
                lazy = 1;
                lazylength = length;
                lazyoffset = offset;
                continue; /*try the next byte*/
            }

            if(lazy)
            {
                lazy = 0;

                if(pos == 0)
                    ERROR_BREAK(81);

                if(length > lazylength + 1)
                {
                    /*push the previous character as literal*/
                    if(!uivector_push_back(out, in[pos - 1]))
                        ERROR_BREAK(83 /*alloc fail*/);
                }
                else
                {
                    length = lazylength;
                    offset = lazyoffset;
                    hash->head[hashval] = -1; /*the same hashchain update will be done, this ensures no wrong alteration*/
                    hash->headz[numzeros] = -1; /*idem*/
                    --pos;
                }
            }
        }

        if(length >= 3 && offset > windowsize)
            ERROR_BREAK(86 /*too big (or overflown negative) offset*/);

        /*encode it as length/distance pair or literal value*/
        if (length < 3) /*only lengths of 3 or higher are supported as length/distance pair*/
        {
            if (!uivector_push_back(out, in[pos]))
                ERROR_BREAK(83 /*alloc fail*/);
        }
        else if(length < minmatch || (length == 3 && offset > 4096))
        {
            /*compensate for the fact that longer offsets have more extra bits, a
              length of only 3 may be not worth it then*/
            if(!uivector_push_back(out, in[pos]))
                ERROR_BREAK(83 /*alloc fail*/);
        }
        else
        {
            addLengthDistance(out, length, offset);

            for(i = 1; i < length; ++i)
            {
                ++pos;
                wpos = pos & (windowsize - 1);
                hashval = getHash(in, insize, pos);

                if(usezeros && hashval == 0)
                {
                    if(numzeros == 0)
                        numzeros = countZeros(in, insize, pos);
                    else if(pos + numzeros > insize || in[pos + numzeros - 1] != 0)
                        --numzeros;
                }
                else
                {
                    numzeros = 0;
                }

                updateHashChain(hash, wpos, hashval, numzeros);
            }
        }
    } /*end of the loop through each character of input*/

    return error;
}

/*Deflate for a block of type "dynamic", that is, with freely, optimally, created huffman trees*/
static unsigned deflateDynamic(LodePNGBitWriter* writer, Hash* hash,
                               const unsigned char* data, size_t datapos, size_t dataend,
                               const LodePNGCompressSettings* settings, unsigned final)
{
    unsigned error = 0;

    /*
    A block is compressed as follows: The PNG data is lz77 encoded, resulting in
    literal bytes and length/distance pairs. This is then huffman compressed with
    two huffman trees. One huffman tree is used for the lit and len values ("ll"),
    another huffman tree is used for the dist values ("d"). These two trees are
    stored using their code lengths, and to compress even more these code lengths
    are also run-length encoded and huffman compressed. This gives a huffman tree
    of code lengths "cl". The code lengths used to describe this third tree are
    the code length code lengths ("clcl").
    */

    /*The lz77 encoded data, represented with integers since there will also be length and distance codes in it*/
    uivector lz77_encoded;
    HuffmanTree tree_ll; /*tree for lit,len values*/
    HuffmanTree tree_d; /*tree for distance codes*/
    HuffmanTree tree_cl; /*tree for encoding the code lengths representing tree_ll and tree_d*/
    unsigned* frequencies_ll = 0; /*frequency of lit,len codes*/
    unsigned* frequencies_d = 0; /*frequency of dist codes*/
    unsigned* frequencies_cl = 0; /*frequency of code length codes*/
    unsigned* bitlen_lld = 0; /*lit,len,dist code lengths (int bits), literally (without repeat codes).*/
    unsigned* bitlen_lld_e = 0; /*bitlen_lld encoded with repeat codes (this is a rudimentary run length compression)*/
    size_t datasize = dataend - datapos;

    /*
    If we could call "bitlen_cl" the the code length code lengths ("clcl"), that is the bit lengths of codes to represent
    tree_cl in CLCL_ORDER, then due to the huffman compression of huffman tree representations ("two levels"), there are
    some analogies:
    bitlen_lld is to tree_cl what data is to tree_ll and tree_d.
    bitlen_lld_e is to bitlen_lld what lz77_encoded is to data.
    bitlen_cl is to bitlen_lld_e what bitlen_lld is to lz77_encoded.
    */

    unsigned BFINAL = final;
    size_t i;
    size_t numcodes_ll, numcodes_d, numcodes_lld, numcodes_lld_e, numcodes_cl;
    unsigned HLIT, HDIST, HCLEN;

    uivector_init(&lz77_encoded);
    HuffmanTree_init(&tree_ll);
    HuffmanTree_init(&tree_d);
    HuffmanTree_init(&tree_cl);
    /* could fit on stack, but >1KB is on the larger side so allocate instead */
    frequencies_ll = (unsigned*)malloc(286 * sizeof(*frequencies_ll));
    frequencies_d = (unsigned*)malloc(30 * sizeof(*frequencies_d));
    frequencies_cl = (unsigned*)malloc(NUM_CODE_LENGTH_CODES * sizeof(*frequencies_cl));

    if(!frequencies_ll || !frequencies_d || !frequencies_cl)
        error = 83; /*alloc fail*/

    /*This while loop never loops due to a break at the end, it is here to
    allow breaking out of it to the cleanup phase on error conditions.*/
    while(!error)
    {
        memset(frequencies_ll, 0, 286 * sizeof(*frequencies_ll));
        memset(frequencies_d, 0, 30 * sizeof(*frequencies_d));
        memset(frequencies_cl, 0, NUM_CODE_LENGTH_CODES * sizeof(*frequencies_cl));

        error = encodeLZ77(&lz77_encoded, hash, data, datapos, dataend, settings->windowsize,
                           settings->minmatch, settings->nicematch, settings->lazymatching);
        if(error)
            break;

        /*Count the frequencies of lit, len and dist codes*/
        for (i = 0; i != lz77_encoded.size; ++i)
        {
            unsigned symbol = lz77_encoded.data[i];
            ++frequencies_ll[symbol];

            if(symbol > 256)
            {
                unsigned dist = lz77_encoded.data[i + 2];
                ++frequencies_d[dist];
                i += 3;
            }
        }

        frequencies_ll[256] = 1; /*there will be exactly 1 end code, at the end of the block*/

        /*Make both huffman trees, one for the lit and len codes, one for the dist codes*/
        error = HuffmanTree_makeFromFrequencies(&tree_ll, frequencies_ll, 257, 286, 15);

        if(error)
            break;

        /*2, not 1, is chosen for mincodes: some buggy PNG decoders require at least 2 symbols in the dist tree*/
        error = HuffmanTree_makeFromFrequencies(&tree_d, frequencies_d, 2, 30, 15);

        if(error)
            break;

        numcodes_ll = LODEPNG_MIN(tree_ll.numcodes, 286);
        numcodes_d = LODEPNG_MIN(tree_d.numcodes, 30);
        /*store the code lengths of both generated trees in bitlen_lld*/
        numcodes_lld = numcodes_ll + numcodes_d;
        bitlen_lld = (unsigned*)malloc(numcodes_lld * sizeof(*bitlen_lld));
        /*numcodes_lld_e never needs more size than bitlen_lld*/
        bitlen_lld_e = (unsigned*)malloc(numcodes_lld * sizeof(*bitlen_lld_e));

        if(!bitlen_lld || !bitlen_lld_e)
            ERROR_BREAK(83); /*alloc fail*/

        numcodes_lld_e = 0;

        for(i = 0; i != numcodes_ll; ++i)
            bitlen_lld[i] = tree_ll.lengths[i];

        for(i = 0; i != numcodes_d; ++i)
            bitlen_lld[numcodes_ll + i] = tree_d.lengths[i];

        /*run-length compress bitlen_ldd into bitlen_lld_e by using repeat codes 16 (copy length 3-6 times),
        17 (3-10 zeroes), 18 (11-138 zeroes)*/
        for(i = 0; i != numcodes_lld; ++i)
        {
            unsigned j = 0; /*amount of repetitions*/

            while(i + j + 1 < numcodes_lld && bitlen_lld[i + j + 1] == bitlen_lld[i])
                ++j;

            if(bitlen_lld[i] == 0 && j >= 2) /*repeat code for zeroes*/
            {
                ++j; /*include the first zero*/

                if (j <= 10) /*repeat code 17 supports max 10 zeroes*/
                {
                    bitlen_lld_e[numcodes_lld_e++] = 17;
                    bitlen_lld_e[numcodes_lld_e++] = j - 3;
                }
                else /*repeat code 18 supports max 138 zeroes*/
                {
                    if (j > 138)
                        j = 138;

                    bitlen_lld_e[numcodes_lld_e++] = 18;
                    bitlen_lld_e[numcodes_lld_e++] = j - 11;
                }

                i += (j - 1);
            }
            else if(j >= 3) /*repeat code for value other than zero*/
            {
                size_t k;
                unsigned num = j / 6u, rest = j % 6u;
                bitlen_lld_e[numcodes_lld_e++] = bitlen_lld[i];

                for(k = 0; k < num; ++k)
                {
                    bitlen_lld_e[numcodes_lld_e++] = 16;
                    bitlen_lld_e[numcodes_lld_e++] = 6 - 3;
                }

                if(rest >= 3)
                {
                    bitlen_lld_e[numcodes_lld_e++] = 16;
                    bitlen_lld_e[numcodes_lld_e++] = rest - 3;
                }
                else
                    j -= rest;

                i += j;
            }
            else /*too short to benefit from repeat code*/
            {
                bitlen_lld_e[numcodes_lld_e++] = bitlen_lld[i];
            }
        }

        /*generate tree_cl, the huffmantree of huffmantrees*/
        for(i = 0; i != numcodes_lld_e; ++i)
        {
            ++frequencies_cl[bitlen_lld_e[i]];
            /*after a repeat code come the bits that specify the number of repetitions,
            those don't need to be in the frequencies_cl calculation*/
            if(bitlen_lld_e[i] >= 16)
                ++i;
        }

        error = HuffmanTree_makeFromFrequencies(&tree_cl, frequencies_cl,
                                                NUM_CODE_LENGTH_CODES, NUM_CODE_LENGTH_CODES, 7);

        if(error)
            break;

        /*compute amount of code-length-code-lengths to output*/
        numcodes_cl = NUM_CODE_LENGTH_CODES;

        /*trim zeros at the end (using CLCL_ORDER), but minimum size must be 4 (see HCLEN below)*/
        while(numcodes_cl > 4u && tree_cl.lengths[CLCL_ORDER[numcodes_cl - 1u]] == 0)
        {
            numcodes_cl--;
        }

        /*
        Write everything into the output

        After the BFINAL and BTYPE, the dynamic block consists out of the following:
        - 5 bits HLIT, 5 bits HDIST, 4 bits HCLEN
        - (HCLEN+4)*3 bits code lengths of code length alphabet
        - HLIT + 257 code lengths of lit/length alphabet (encoded using the code length
          alphabet, + possible repetition codes 16, 17, 18)
        - HDIST + 1 code lengths of distance alphabet (encoded using the code length
          alphabet, + possible repetition codes 16, 17, 18)
        - compressed data
        - 256 (end code)
        */

        /*Write block type*/
        writeBits(writer, BFINAL, 1);
        writeBits(writer, 0, 1); /*first bit of BTYPE "dynamic"*/
        writeBits(writer, 1, 1); /*second bit of BTYPE "dynamic"*/

        /*write the HLIT, HDIST and HCLEN values*/
        /*all three sizes take trimmed ending zeroes into account, done either by HuffmanTree_makeFromFrequencies
          or in the loop for numcodes_cl above, which saves space. */
        HLIT = (unsigned)(numcodes_ll - 257);
        HDIST = (unsigned)(numcodes_d - 1);
        HCLEN = (unsigned)(numcodes_cl - 4);
        writeBits(writer, HLIT, 5);
        writeBits(writer, HDIST, 5);
        writeBits(writer, HCLEN, 4);

        /*write the code lengths of the code length alphabet ("bitlen_cl")*/
        for(i = 0; i != numcodes_cl; ++i)
            writeBits(writer, tree_cl.lengths[CLCL_ORDER[i]], 3);

        /*write the lengths of the lit/len AND the dist alphabet*/
        for(i = 0; i != numcodes_lld_e; ++i)
        {
            writeBitsReversed(writer, tree_cl.codes[bitlen_lld_e[i]], tree_cl.lengths[bitlen_lld_e[i]]);

            /*extra bits of repeat codes*/
            if(bitlen_lld_e[i] == 16)
                writeBits(writer, bitlen_lld_e[++i], 2);
            else if(bitlen_lld_e[i] == 17)
                writeBits(writer, bitlen_lld_e[++i], 3);
            else if(bitlen_lld_e[i] == 18)
                writeBits(writer, bitlen_lld_e[++i], 7);
        }

        /*write the compressed data symbols*/
        writeLZ77data(writer, &lz77_encoded, &tree_ll, &tree_d);

        /*error: the length of the end code 256 must be larger than 0*/
        if(tree_ll.lengths[256] == 0)
            ERROR_BREAK(64);

        /*write the end code*/
        writeBitsReversed(writer, tree_ll.codes[256], tree_ll.lengths[256]);

        break; /*end of error-while*/
    }

    /*cleanup*/
    uivector_cleanup(&lz77_encoded);
    HuffmanTree_cleanup(&tree_ll);
    HuffmanTree_cleanup(&tree_d);
    HuffmanTree_cleanup(&tree_cl);
    free(frequencies_ll);
    free(frequencies_d);
    free(frequencies_cl);
    free(bitlen_lld);
    free(bitlen_lld_e);

    return error;
}

static unsigned hash_init(Hash* hash, unsigned windowsize)
{
    unsigned i;

    hash->head = (int*)malloc(sizeof(int) * HASH_NUM_VALUES);
    hash->val = (int*)malloc(sizeof(int) * windowsize);
    hash->chain = (unsigned short*)malloc(sizeof(unsigned short) * windowsize);

    hash->zeros = (unsigned short*)malloc(sizeof(unsigned short) * windowsize);
    hash->headz = (int*)malloc(sizeof(int) * (MAX_SUPPORTED_DEFLATE_LENGTH + 1));
    hash->chainz = (unsigned short*)malloc(sizeof(unsigned short) * windowsize);

    if (!hash->head || !hash->chain || !hash->val  || !hash->headz|| !hash->chainz || !hash->zeros)
    {
        return 83; /*alloc fail*/
    }

    /*initialize hash table*/
    for(i = 0; i != HASH_NUM_VALUES; ++i)
        hash->head[i] = -1;

    for(i = 0; i != windowsize; ++i)
        hash->val[i] = -1;

    for(i = 0; i != windowsize; ++i)
        hash->chain[i] = i; /*same value as index indicates uninitialized*/

    for(i = 0; i <= MAX_SUPPORTED_DEFLATE_LENGTH; ++i)
        hash->headz[i] = -1;

    for(i = 0; i != windowsize; ++i)
        hash->chainz[i] = i; /*same value as index indicates uninitialized*/

    return 0;
}

/* ////////////////////////////////////////////////////////////////////////// */
/* / Adler32                                                                / */
/* ////////////////////////////////////////////////////////////////////////// */

static unsigned update_adler32(unsigned adler, const unsigned char* data, unsigned len)
{
    unsigned s1 = adler & 0xffffu;
    unsigned s2 = (adler >> 16u) & 0xffffu;

    while(len != 0u)
    {
        unsigned i;
        /*at least 5552 sums can be done before the sums overflow, saving a lot of module divisions*/
        unsigned amount = len > 5552u ? 5552u : len;
        len -= amount;

        for(i = 0; i != amount; ++i)
        {
            s1 += (*data++);
            s2 += s1;
        }

        s1 %= 65521u;
        s2 %= 65521u;
    }

    return (s2 << 16u) | s1;
}

/*Return the adler32 of the bytes data[0..len-1]*/
static unsigned adler32(const unsigned char* data, unsigned len)
{
    return update_adler32(1u, data, len);
}

/* ////////////////////////////////////////////////////////////////////////// */
/* / Zlib                                                                   / */
/* ////////////////////////////////////////////////////////////////////////// */

unsigned lodepng_zlib_compress(unsigned char** out, size_t* outsize, const unsigned char* in,
                               size_t insize, const LodePNGCompressSettings* settings)
{
    size_t i;
    unsigned error;
    unsigned char* deflatedata = 0;
    size_t deflatesize = 0;

    error = lodepng_deflate(&deflatedata, &deflatesize, in, insize, settings);

    *out = NULL;
    *outsize = 0;

    if(!error)
    {
        *outsize = deflatesize + 6;
        *out = (unsigned char*) malloc(*outsize);

        if(!*out)
            error = 83; /*alloc fail*/
    }

    if(!error)
    {
        unsigned ADLER32 = adler32(in, (unsigned)insize);
        /*zlib data: 1 byte CMF (CM+CINFO), 1 byte FLG, deflate data, 4 byte ADLER32 checksum of the Decompressed data*/
        unsigned CMF = 120; /*0b01111000: CM 8, CINFO 7. With CINFO 7, any window size up to 32768 can be used.*/
        unsigned FLEVEL = 0;
        unsigned FDICT = 0;
        unsigned CMFFLG = 256 * CMF + FDICT * 32 + FLEVEL * 64;
        unsigned FCHECK = 31 - CMFFLG % 31;

        CMFFLG += FCHECK;

        (*out)[0] = (unsigned char)(CMFFLG >> 8);
        (*out)[1] = (unsigned char)(CMFFLG & 255);

        for(i = 0; i != deflatesize; ++i)
            (*out)[i + 2] = deflatedata[i];

        lodepng_set32bitInt(&(*out)[*outsize - 4], ADLER32);
    }

    free(deflatedata);

    return error;
}

static void hash_cleanup(Hash* hash)
{
    free(hash->head);
    free(hash->val);
    free(hash->chain);

    free(hash->zeros);
    free(hash->headz);
    free(hash->chainz);
}

static unsigned lodepng_deflatev(ucvector* out, const unsigned char* in, size_t insize,
                                 const LodePNGCompressSettings* settings)
{
    unsigned error = 0;
    size_t i, blocksize, numdeflateblocks;
    Hash hash;
    LodePNGBitWriter writer;

    LodePNGBitWriter_init(&writer, out);

    /*on PNGs, deflate blocks of 65-262k seem to give most dense encoding*/
    blocksize = insize / 8u + 8;

    if(blocksize < 65536)
        blocksize = 65536;

    if(blocksize > 262144)
        blocksize = 262144;

    numdeflateblocks = (insize + blocksize - 1) / blocksize;

    if(numdeflateblocks == 0)
        numdeflateblocks = 1;

    error = hash_init(&hash, settings->windowsize);

    if(!error)
    {
        for(i = 0; i != numdeflateblocks && !error; ++i)
        {
            unsigned final = (i == numdeflateblocks - 1);
            size_t start = i * blocksize;
            size_t end = start + blocksize;

            if(end > insize)
                end = insize;

            error = deflateDynamic(&writer, &hash, in, start, end, settings, final);
#ifndef NOT_ULTIMATE
            iec_printer->updateFlushProgressBar();
            vTaskDelay(2);    // Force task switch and prevent holding CPU for too long
#endif /* NOT_ULTIMATE */
        }
    }

    hash_cleanup(&hash);

    return error;
}

unsigned lodepng_deflate(unsigned char** out, size_t* outsize,
                         const unsigned char* in, size_t insize,
                         const LodePNGCompressSettings* settings)
{
    ucvector v = ucvector_init(*out, *outsize);
    unsigned error = lodepng_deflatev(&v, in, insize, settings);

    *out = v.data;
    *outsize = v.size;

    return error;
}

static unsigned addChunk_IDAT(ucvector* out, const unsigned char* data, size_t datasize,
                              LodePNGCompressSettings* zlibsettings)
{
    unsigned error = 0;
    unsigned char* zlib = 0;
    size_t zlibsize = 0;

    error = lodepng_zlib_compress(&zlib, &zlibsize, data, datasize, zlibsettings);

    if(!error)
    {
        error = lodepng_chunk_createv(out, zlibsize, "IDAT", zlib);
    }

    free(zlib);

    return error;
}

void lodepng_compress_settings_init(LodePNGCompressSettings* settings)
{
    /*compress with dynamic huffman tree (not in the mathematical sense, just not the predefined one)*/
    settings->windowsize = DEFAULT_WINDOWSIZE;
    settings->minmatch = 3;
    settings->nicematch = 128;
    settings->lazymatching = 1;
}

const LodePNGCompressSettings lodepng_default_compress_settings = {DEFAULT_WINDOWSIZE, 3, 128, 1};

void lodepng_encoder_settings_init(LodePNGEncoderSettings* settings)
{
    lodepng_compress_settings_init(&settings->zlibsettings);
    settings->filter_palette_zero = 1;
    settings->filter_strategy = LFS_MINSUM;
    settings->auto_convert = 1;
    settings->force_palette = 0;
    settings->predefined_filters = 0;
    settings->add_id = 0;
}

void lodepng_color_mode_init(LodePNGColorMode* info)
{
    info->key_defined = 0;
    info->key_r = info->key_g = info->key_b = 0;
    info->colortype = LCT_RGBA;
    info->bitdepth = 8;
    info->palette = 0;
    info->palettesize = 0;
}

void lodepng_info_init(LodePNGInfo* info)
{
    lodepng_color_mode_init(&info->color);
    info->interlace_method = 0;
    info->compression_method = 0;
    info->filter_method = 0;
    info->phys_x = 1;   
    info->phys_y = 1;   
    info->phys_unit = 0; /* unknown unit */
}

void lodepng_state_init(LodePNGState* state)
{
    lodepng_encoder_settings_init(&state->encoder);
    lodepng_info_init(&state->info_png);
    state->error = 1;
}

/*allocates palette memory if needed, and initializes all colors to black*/
static void lodepng_color_mode_alloc_palette(LodePNGColorMode* info)
{
    size_t i;
    /*if the palette is already allocated, it will have size 1024 so no reallocation needed in that case*/
    /*the palette must have room for up to 256 colors with 4 bytes each.*/
    if(!info->palette)
        info->palette = (unsigned char*) malloc(1024);

    if(!info->palette)
        return; /*alloc fail*/

    for(i = 0; i != 256; ++i)
    {
        /*Initialize all unused colors with black, the value used for invalid palette indices.
        This is an error according to the PNG spec, but common PNG decoders make it black instead.
        That makes color conversion slightly faster due to no error handling needed.*/
        info->palette[i * 4 + 0] = 0;
        info->palette[i * 4 + 1] = 0;
        info->palette[i * 4 + 2] = 0;
        info->palette[i * 4 + 3] = 255;
    }
}

unsigned lodepng_palette_add(LodePNGColorMode* info,
                             unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    if(!info->palette) /*allocate palette if empty*/
    {
        lodepng_color_mode_alloc_palette(info);

        if(!info->palette)
            return 83; /*alloc fail*/
    }

    if(info->palettesize >= 256)
    {
        return 108; /*too many palette values*/
    }

    info->palette[4 * info->palettesize + 0] = r;
    info->palette[4 * info->palettesize + 1] = g;
    info->palette[4 * info->palettesize + 2] = b;
    info->palette[4 * info->palettesize + 3] = a;
    ++info->palettesize;

    return 0;
}

void lodepng_palette_clear(LodePNGColorMode* info)
{
    if(info->palette)
        free(info->palette);

    info->palette = 0;
    info->palettesize = 0;
}

void lodepng_color_mode_cleanup(LodePNGColorMode* info)
{
    lodepng_palette_clear(info);
}

void lodepng_info_cleanup(LodePNGInfo* info)
{
    lodepng_color_mode_cleanup(&info->color);
}

void lodepng_state_cleanup(LodePNGState* state)
{
    lodepng_info_cleanup(&state->info_png);
}

/*in an idat chunk, each scanline is a multiple of 8 bits, unlike the lodepng output buffer,
and in addition has one extra byte per line: the filter byte. So this gives a larger
result than lodepng_get_raw_size. Set h to 1 to get the size of 1 row including filter byte. */
static size_t lodepng_get_raw_size_idat(unsigned w, unsigned h, unsigned bpp)
{
    /* + 1 for the filter byte, and possibly plus padding bits per line. */
    /* Ignoring casts, the expression is equal to (w * bpp + 7) / 8 + 1, but avoids overflow of w * bpp */
    size_t line = ((size_t)(w / 8u) * bpp) + 1u + ((w & 7u) * bpp + 7u) / 8u;
    return (size_t)h * line;
}

unsigned lodepng_encode_mps(unsigned char** out, size_t* outsize,
                            const unsigned char* image, unsigned w, unsigned h,
                            LodePNGState* state)
{
    unsigned char* data = (unsigned char *) image; /*uncompressed version of the IDAT chunk data*/
    size_t datasize = lodepng_get_raw_size_idat(w, h, state->info_png.color.bitdepth);
    ucvector outv = ucvector_init(NULL, 0);

    /*provide some proper output values if error will happen*/
    *out = 0;
    *outsize = 0;
    state->error = 0;

    /*write signature and chunks*/
    state->error = writeSignature(&outv);

    if(state->error)
        goto cleanup;

    /*IHDR*/
    state->error = addChunk_IHDR(&outv, w, h, state->info_png.color.colortype, state->info_png.color.bitdepth, state->info_png.interlace_method);

    if(state->error)
        goto cleanup;

    /*PLTE*/
    state->error = addChunk_PLTE(&outv, &state->info_png.color);

    if(state->error)
        goto cleanup;

    state->error = addChunk_pHYs(&outv, &state->info_png);

    if(state->error)
        goto cleanup;

    /*IDAT (multiple IDAT chunks must be consecutive)*/
    state->error = addChunk_IDAT(&outv, data, datasize, &state->encoder.zlibsettings);

    if(state->error)
        goto cleanup;

    state->error = addChunk_IEND(&outv);

    if(state->error)
        goto cleanup;

cleanup:
    /*instead of cleaning the vector up, give it to the output*/
    *out = outv.data;
    *outsize = outv.size;

    return state->error;
}

/*****************************  END OF FILE  ****************************/
