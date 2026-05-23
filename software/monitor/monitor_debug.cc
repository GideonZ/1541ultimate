#include "monitor_debug.h"

#include <string.h>
#include <stdio.h>

namespace {

// Column positions for the fixed footer layout. Header labels and value rows
// share these positions so values align under their labels regardless of
// whether the context is known. Positions are referenced explicitly by name
// to keep the layout obvious and to avoid creeping field reorderings.
enum {
    FOOTER_POS_PC = 0,
    FOOTER_POS_SP = 5,
    FOOTER_POS_AC = 8,
    FOOTER_POS_XR = 11,
    FOOTER_POS_YR = 14,
    FOOTER_POS_FLAGS = 17,
    FOOTER_POS_IRQ = 26,
    FOOTER_POS_NMI = 31,
    FOOTER_WIDTH = 35
};

static void blank_buffer(char *buf, int len)
{
    for (int i = 0; i < len; i++) {
        buf[i] = ' ';
    }
    buf[len] = 0;
}

static void copy_at(char *buf, int pos, const char *src)
{
    int n = (int)strlen(src);
    memcpy(buf + pos, src, n);
}

static void hex4_at(char *buf, int pos, uint16_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    buf[pos + 0] = hex[(value >> 12) & 0x0F];
    buf[pos + 1] = hex[(value >> 8) & 0x0F];
    buf[pos + 2] = hex[(value >> 4) & 0x0F];
    buf[pos + 3] = hex[value & 0x0F];
}

static void hex2_at(char *buf, int pos, uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    buf[pos + 0] = hex[(value >> 4) & 0x0F];
    buf[pos + 1] = hex[value & 0x0F];
}

static void bin8_at(char *buf, int pos, uint8_t value)
{
    for (int i = 0; i < 8; i++) {
        buf[pos + i] = ((value >> (7 - i)) & 1) ? '1' : '0';
    }
}

}

void debug_context_reset(DebugContext *ctx)
{
    if (!ctx) {
        return;
    }
    ctx->valid = false;
    ctx->pc = 0;
    ctx->a = 0;
    ctx->x = 0;
    ctx->y = 0;
    ctx->sr = 0;
    ctx->sp = 0;
    ctx->irq_valid = false;
    ctx->irq_vec = 0;
    ctx->nmi_valid = false;
    ctx->nmi_vec = 0;
}

MonitorDebug :: MonitorDebug()
    : active(false)
{
    debug_context_reset(&ctx);
}

void MonitorDebug :: enter(void)
{
    active = true;
}

void MonitorDebug :: leave(void)
{
    active = false;
    // Leaving Debug mode does not invalidate the cached context: returning to
    // Debug should show the same numbers until execution resumes.
}

void MonitorDebug :: invalidate_context(void)
{
    debug_context_reset(&ctx);
}

void MonitorDebug :: set_context(const DebugContext &c)
{
    ctx = c;
}

void MonitorDebug :: format_footer_header(char *out, int out_len)
{
    if (!out || out_len < FOOTER_WIDTH + 1) {
        if (out && out_len > 0) {
            out[0] = 0;
        }
        return;
    }
    blank_buffer(out, FOOTER_WIDTH);
    copy_at(out, FOOTER_POS_PC, "PC");
    copy_at(out, FOOTER_POS_SP, "SP");
    copy_at(out, FOOTER_POS_AC, "AC");
    copy_at(out, FOOTER_POS_XR, "XR");
    copy_at(out, FOOTER_POS_YR, "YR");
    copy_at(out, FOOTER_POS_FLAGS, "NV-BDIZC");
    copy_at(out, FOOTER_POS_IRQ, "IRQ");
    copy_at(out, FOOTER_POS_NMI, "NMI");
}

void MonitorDebug :: format_footer_values(const DebugContext &ctx,
                                          char *out, int out_len)
{
    if (!out || out_len < FOOTER_WIDTH + 1) {
        if (out && out_len > 0) {
            out[0] = 0;
        }
        return;
    }
    blank_buffer(out, FOOTER_WIDTH);
    if (!ctx.valid) {
        return;
    }
    hex4_at(out, FOOTER_POS_PC, ctx.pc);
    hex2_at(out, FOOTER_POS_SP, ctx.sp);
    hex2_at(out, FOOTER_POS_AC, ctx.a);
    hex2_at(out, FOOTER_POS_XR, ctx.x);
    hex2_at(out, FOOTER_POS_YR, ctx.y);
    bin8_at(out, FOOTER_POS_FLAGS, ctx.sr);
    if (ctx.irq_valid) {
        hex4_at(out, FOOTER_POS_IRQ, ctx.irq_vec);
    }
    if (ctx.nmi_valid) {
        hex4_at(out, FOOTER_POS_NMI, ctx.nmi_vec);
    }
}

int MonitorDebug :: format_help_lines(const char *lines[], int max_lines)
{
    static const char *const text[] = {
        "",
        "D Step Over  T Step Into  O Step Out",
        "G Continue   R Breakpt    C=+R Brkpts",
        "C=+X Reset   RETURN Follow",
        "",
        "M Memory     I ASCII      V Screen",
        "A Assembly   B Binary     U Undoc/Case",
        "J Jump       P Poll       N Number",
        "E Edit       F Fill       W Width",
        "C Compare    H Hunt       Z Freeze",
        "L Load       S Save",
        "",
        "Bookmarks:     C=+B List  C=+0-9 Jump",
        "Monitor:       C=+O Open/Close",
        "Leave debug:   C=+D/RSTOP",
        "Leave edit:    C=+E/RSTOP",
        "Copy/Paste:    C=+C / C=+V"
    };
    int n = (int)(sizeof(text) / sizeof(text[0]));
    if (n > max_lines) {
        n = max_lines;
    }
    for (int i = 0; i < n; i++) {
        lines[i] = text[i];
    }
    return n;
}
