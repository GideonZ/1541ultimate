/*
 * iec_log.h - the Software IEC log.
 *
 * The drive always writes one line for a command channel command that leaves an error, for
 * an open that fails, and for the first failure of a channel. With the setting "Log Every
 * Operation" in the Software IEC settings on, it also writes one line for every other
 * command, open and close, with the reply of a command that answers with data, the host
 * file an open reached or the first bytes of a listing, and the last line of a listing.
 * Each line starts with SOFTIEC_LOG_PREFIX and carries the bytes the host sent, the current
 * partition and directory, and the error channel's answer, and ends in " #" and a sequence
 * number that counts every line, so what a program does can be
 * followed in a device log without a special build. Bytes are never logged per bus byte.
 *
 * The formatter below takes an explicit length and never treats IEC data as a C string,
 * so an embedded zero, a carriage return and a shifted PETSCII character all survive
 * into the log and stay distinguishable from each other.
 */
#ifndef IEC_LOG_H
#define IEC_LOG_H

#include <stdint.h>

// Shared by every line. Grep for it in a device log.
#define SOFTIEC_LOG_PREFIX "SoftIEC: "

// The command buffer and a file name hold up to 254 bytes, more than a log line should
// carry, so a payload is rendered into a buffer of four characters for each of this many
// bytes: all of a command of printable text fits, and at least this many bytes of any
// other. A longer one is cut, the cut is marked with "..", and the line still reports the
// real length (SI-152).
#define SOFTIEC_LOG_MAX_BYTES 64

// Room a caller has to provide for the rendering of SOFTIEC_LOG_MAX_BYTES.
#define SOFTIEC_LOG_TEXT_SIZE (4 * SOFTIEC_LOG_MAX_BYTES + 4)

static const char softiec_log_digits[] = "0123456789ABCDEF";

// Renders len bytes as readable text. A printable ASCII byte stands for itself; a
// carriage return, a line feed and a zero get the usual short escapes, and every
// other byte, which includes shifted PETSCII and binary parameters, is written as
// \xNN. Writes at most out_size - 1 characters and always terminates. A rendering that
// did not fit ends in ".." so a reader can tell a short line from a short payload.
// Returns the number of characters written, not counting the terminator.
static inline int softiec_log_text(const uint8_t *data, int len, char *out, int out_size)
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
                esc[2] = softiec_log_digits[(b >> 4) & 15];
                esc[3] = softiec_log_digits[b & 15];
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

#endif /* IEC_LOG_H */
