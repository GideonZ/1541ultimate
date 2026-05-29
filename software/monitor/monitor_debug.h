#ifndef MONITOR_DEBUG_H
#define MONITOR_DEBUG_H

#include "integer.h"

// Captured CPU state. The "valid" bit gates display of all registers/flags.
// IRQ/NMI vectors are gated separately because they live at memory locations
// the monitor may not have been able to read safely.
struct DebugContext {
    bool valid;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sr;
    uint8_t sp;
    bool irq_valid;
    uint16_t irq_vec;
    bool nmi_valid;
    uint16_t nmi_vec;
};

void debug_context_reset(DebugContext *ctx);

// Modal Debug state. Pure data plus footer/help formatting helpers - no
// machine access. MachineMonitor instantiates one of these and routes Debug
// key presses through it.
class MonitorDebug
{
    bool active;
    DebugContext ctx;
public:
    enum {
        FOOTER_POS_PC = 0,
        FOOTER_POS_AC = 5,
        FOOTER_POS_XR = 8,
        FOOTER_POS_YR = 11,
        FOOTER_POS_SP = 14,
        FOOTER_POS_FLAGS = 17,
        FOOTER_POS_IRQ = 26,
        FOOTER_POS_NMI = 31,
        FOOTER_WIDTH = 35
    };

    MonitorDebug();

    bool is_active(void) const { return active; }
    void enter(void);
    void leave(void);

    void invalidate_context(void);
    void set_context(const DebugContext &c);
    bool has_context(void) const { return ctx.valid; }
    const DebugContext &context(void) const { return ctx; }

    // Footer formatting. Both buffers must hold at least 40 bytes (including
    // NUL terminator); the footer body fits the monitor window's 38-column
    // budget.
    static void format_footer_header(char *out, int out_len);
    static void format_footer_values(const DebugContext &ctx, char *out, int out_len);

    // Help screen lines. Returns number of lines written.
    static int format_help_lines(const char *lines[], int max_lines);
};

#endif
