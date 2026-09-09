/*
 * iec_trace.h - Software IEC compatibility diagnostics for GideonZ/1541ultimate#877.
 *
 * These are diagnostics, not part of what any command does. They exist so that a
 * reporter running C64 OS or CMD utilities can send back what the drive actually
 * received and answered, and they may be removed once that investigation is over.
 *
 * Everything they write starts with SOFTIEC_TRACE_PREFIX, so one search finds every
 * line in a device log, and every call site in the source is a SOFTIEC_TRACE() macro.
 * To remove the feature, delete this header, the block marked as diagnostics at the
 * top of software/io/iec/iec_channel.cc, the SOFTIEC_TRACE() calls and the trace_
 * members they use in software/io/iec, and the marked test blocks in
 * software/io/iec/cbmdos_parser_test.cc and software/test/iecdrive/testdrive.cc. To
 * turn it off without removing it, build with -DSOFTIEC_TRACE_ENABLED=0; no command
 * behaviour depends on it either way.
 *
 * The two formatters below take an explicit length and never treat IEC data as a C
 * string, so an embedded zero, a carriage return and a shifted PETSCII character all
 * survive into the log and stay distinguishable from each other.
 *
 * The logical file number is not logged because the drive never sees it: OPEN 2,11,15
 * puts the device number and the secondary address on the bus and keeps the file
 * number in the C64's own tables.
 */
#ifndef IEC_TRACE_H
#define IEC_TRACE_H

#include <stdint.h>

#ifndef SOFTIEC_TRACE_ENABLED
#define SOFTIEC_TRACE_ENABLED 1
#endif

// Shared by every diagnostic line. Grep for it in a device log.
#define SOFTIEC_TRACE_PREFIX "SOFTIEC-TRACE"

// The command buffer holds 64 bytes and so does a file name, so a payload is
// normally rendered whole. Anything longer is cut and the cut is marked.
#define SOFTIEC_TRACE_MAX_BYTES 64

// Room a caller has to provide for the two renderings of SOFTIEC_TRACE_MAX_BYTES.
#define SOFTIEC_TRACE_HEX_SIZE  (3 * SOFTIEC_TRACE_MAX_BYTES + 4)
#define SOFTIEC_TRACE_TEXT_SIZE (4 * SOFTIEC_TRACE_MAX_BYTES + 4)

static const char softiec_trace_digits[] = "0123456789ABCDEF";

// Renders len bytes as two upper case hex digits each, separated by single spaces.
// Writes at most out_size - 1 characters and always terminates. A rendering that did
// not fit ends in ".." so a reader can tell a short line from a short payload.
// Returns the number of characters written, not counting the terminator.
static inline int softiec_trace_hex(const uint8_t *data, int len, char *out, int out_size)
{
    int w = 0;
    if (!out || (out_size < 1)) {
        return 0;
    }
    out[0] = 0;
    if (!data || (len < 0)) {
        return 0;
    }
    for (int i = 0; i < len; i++) {
        int need = (w ? 3 : 2);
        if ((w + need) >= (out_size - 2)) { // keep room for ".." and the terminator
            if ((w + 2) < out_size) {
                out[w++] = '.';
                out[w++] = '.';
            }
            break;
        }
        if (w) {
            out[w++] = ' ';
        }
        out[w++] = softiec_trace_digits[(data[i] >> 4) & 15];
        out[w++] = softiec_trace_digits[data[i] & 15];
    }
    out[w] = 0;
    return w;
}

// Renders len bytes as readable text. A printable ASCII byte stands for itself; a
// carriage return, a line feed and a zero get the usual short escapes, and every
// other byte, which includes shifted PETSCII and binary parameters, is written as
// \xNN. Same truncation rule and return value as softiec_trace_hex().
static inline int softiec_trace_text(const uint8_t *data, int len, char *out, int out_size)
{
    int w = 0;
    if (!out || (out_size < 1)) {
        return 0;
    }
    out[0] = 0;
    if (!data || (len < 0)) {
        return 0;
    }
    for (int i = 0; i < len; i++) {
        uint8_t b = data[i];
        char esc[4];
        int n = 0;
        switch (b) {
        case 0x00: esc[0] = '\\'; esc[1] = '0'; n = 2; break;
        case 0x0A: esc[0] = '\\'; esc[1] = 'n'; n = 2; break;
        case 0x0D: esc[0] = '\\'; esc[1] = 'r'; n = 2; break;
        case '"':  esc[0] = '\\'; esc[1] = '"'; n = 2; break;
        case '\\': esc[0] = '\\'; esc[1] = '\\'; n = 2; break;
        default:
            if ((b >= 0x20) && (b < 0x7F)) {
                esc[0] = (char)b;
                n = 1;
            } else {
                esc[0] = '\\';
                esc[1] = 'x';
                esc[2] = softiec_trace_digits[(b >> 4) & 15];
                esc[3] = softiec_trace_digits[b & 15];
                n = 4;
            }
            break;
        }
        if ((w + n) >= (out_size - 2)) {
            if ((w + 2) < out_size) {
                out[w++] = '.';
                out[w++] = '.';
            }
            break;
        }
        for (int k = 0; k < n; k++) {
            out[w++] = esc[k];
        }
    }
    out[w] = 0;
    return w;
}

class IecDrive;

// Writes one diagnostic line. Defined in iec_channel.cc, which owns the Software IEC
// side; iec_drive.cc calls it as well, to report the drive's configuration.
void softiec_trace(IecDrive *drive, int channel, uint8_t secondary,
                   const char *op, const uint8_t *payload, int len,
                   const char *detail_fmt, ...);

// Every call site goes through this, so that a build with the diagnostics turned off
// does not even work out the arguments.
#if SOFTIEC_TRACE_ENABLED
#define SOFTIEC_TRACE(...) softiec_trace(__VA_ARGS__)
#else
#define SOFTIEC_TRACE(...) do { } while (0)
#endif

#endif /* IEC_TRACE_H */
