#ifndef MONITOR_DEBUG_BRK_SESSION_H
#define MONITOR_DEBUG_BRK_SESSION_H

#include "monitor_debug_session.h"
#include "monitor_debug_predictor.h"

class Keyboard;

class BrkDebugSession : public DebugSession
{
protected:
    virtual bool backend_ready(void) const = 0;
    virtual uint8_t current_cpu_port(void) const = 0;
    virtual bool begin_stopped_session(void) = 0;
    virtual void end_stopped_session(bool stopped_it) = 0;
    virtual uint8_t peek_cpu(uint16_t address, uint8_t cpu_port) = 0;
    virtual void poke_cpu(uint16_t address, uint8_t byte, uint8_t cpu_port) = 0;
    virtual uint8_t peek_visible(uint16_t address) = 0;
    virtual void poke_visible(uint16_t address, uint8_t byte) = 0;
    virtual void unfreeze_if_accessible(void) = 0;
    // Freeze-mode render-target preservation. A debug step must temporarily
    // unfreeze the C64 to run the live CPU; if the machine was frozen, it must
    // be re-frozen before control returns to the monitor so the monitor keeps
    // rendering into the firmware/menu screen instead of the live C64 screen.
    // Default no-op keeps backends without a freeze concept unaffected.
    virtual bool machine_is_frozen(void) const { return false; }
    virtual void refreeze_machine(void) { }
    virtual bool reset_machine(void) = 0;
    // Pulse NMI on the live CPU and release the stopped session. The pulse
    // MUST occur while the CPU is still stopped so the request bit is
    // observed when the CPU resumes; clearing the pulse only happens after
    // the resume. Implementations therefore bracket the resume with the
    // request/clear pair.
    virtual void pulse_nmi_and_release(bool stopped_it) = 0;
    virtual void delay_ms(int ms) = 0;
    virtual bool free_run_no_breakpoint(uint16_t address);
    virtual uint8_t read_patch_byte(uint16_t address, uint8_t cpu_port);
    virtual void write_patch_byte(uint16_t address, uint8_t byte, uint8_t cpu_port);

public:
    BrkDebugSession();
    virtual ~BrkDebugSession();

    virtual void set_cancel_keyboard(Keyboard *keyboard);
    virtual void set_run_window_refreeze_enabled(bool enabled);
    virtual Result snapshot(DebugContext *ctx);
    virtual Result over(const DebugContext &from,
                        const DebugPredictResult &pred,
                        DebugContext *ctx);
    virtual Result over(const DebugContext &from,
                        const DebugPredictResult &pred,
                        const MonitorBreakpoints *breakpoints,
                        DebugContext *ctx);
    virtual Result over_at(uint16_t start_pc,
                           const DebugPredictResult &pred,
                           DebugContext *ctx);
    virtual Result over_at(uint16_t start_pc,
                           const DebugPredictResult &pred,
                           const MonitorBreakpoints *breakpoints,
                           DebugContext *ctx);
    virtual Result trace(const DebugContext &from,
                         const DebugPredictResult &pred,
                         DebugContext *ctx);
    virtual Result trace_at(uint16_t start_pc,
                            const DebugPredictResult &pred,
                            DebugContext *ctx);
    virtual Result step_out(const DebugContext &from, DebugContext *ctx);
    virtual Result step_out(const DebugContext &from,
                            const MonitorBreakpoints *breakpoints,
                            DebugContext *ctx);
    virtual Result go(const DebugContext &from,
                      const MonitorBreakpoints *breakpoints,
                      uint16_t start_pc);
    virtual Result run_to(const DebugContext &from,
                          uint16_t target_pc,
                          const MonitorBreakpoints *breakpoints,
                          uint16_t start_pc,
                          DebugContext *ctx);
    virtual void cleanup(void);
    virtual void cleanup_to_context(const DebugContext *ctx);
    virtual bool read_step_bytes(uint16_t address, uint8_t *dst, uint8_t len);
    virtual void forget_context(void);
    virtual bool screen_render_target_invalidated(void) const { return screen_was_clobbered; }
    virtual bool claim_debug_ownership(bool remote);
    virtual void refresh_debug_ownership(void);
    virtual void release_debug_ownership(void);

private:
    enum PatchInstallResult {
        PATCH_INSTALL_OK = 0,
        PATCH_INSTALL_NOT_SUPPORTED,
        PATCH_INSTALL_FAILED
    };

    struct Patch {
        bool used;
        uint16_t address;
        uint8_t original;
        uint8_t cpu_port;
    };

    enum {
        MAX_PATCHES = 16,
        MAX_RETURN_TARGETS = 8
    };

    Keyboard *cancel_keyboard;
    Patch patches[MAX_PATCHES];
    bool handler_installed;
    bool cpu_parked_in_spin;
    // Nesting-safe CPU-run window. When enabled, begin_run_window() unfreezes a
    // frozen machine once; end_run_window() re-freezes it on the outermost exit
    // so a frozen monitor's render target survives the step.
    int run_window_depth;
    bool run_window_refreeze_enabled;
    bool run_window_unfroze;
    // Set by end_run_window() when it actually refreezes the machine. Callers
    // can read this via screen_render_target_invalidated() to know whether the
    // firmware chrome rows need restoring before the next visible redraw.
    bool screen_was_clobbered;
    uint8_t saved_handler_bytes[100];
    uint8_t saved_nmi_trampoline_bytes[16];
    bool nmi_trampoline_installed;
    uint8_t saved_nmi_vector[2];
    uint8_t saved_brk_vector[2];
    bool has_last_context;
    DebugContext last_context;
    uint16_t return_targets[MAX_RETURN_TARGETS];
    uint8_t return_target_count;

    bool reserved_patch_address(uint16_t addr) const;
    void begin_run_window(void);
    void end_run_window(void);
    void save_and_install_handler(void);
    void uninstall_handler(void);
    int find_free_patch(void);
    bool already_patched(uint16_t addr);
    PatchInstallResult install_brk_at(uint16_t addr, uint8_t cpu_port);
    PatchInstallResult install_breakpoints(const MonitorBreakpoints *breakpoints,
                                           uint16_t skip_address,
                                           bool skip_address_valid);
    bool context_at_breakpoint(const DebugContext &ctx,
                               const MonitorBreakpoints *breakpoints,
                               uint16_t skip_address,
                               bool skip_address_valid) const;
    void restore_patches(void);
    void fill_vectors(DebugContext *ctx, uint8_t cpu_port);
    void clear_return_targets(void);
    void push_return_target(uint16_t target);
    bool peek_return_target(uint16_t *target) const;
    void pop_return_target(uint16_t target);
    void drop_queued_execution_keys(void);
    Result wait_for_sentinel(int timeout_ms);
    void read_captured_context(DebugContext *ctx, uint8_t cpu_port);
    void release_to_run(const DebugContext *from);
    void resume_from_parked_context(const DebugContext &from);
    void reset_spin_target(void);
    void nmi_redirect_to(uint16_t target);
    Result perform_run(const DebugContext *from, uint16_t start_pc,
                       bool use_start_pc, DebugContext *out, uint8_t cpu_port);
    Result step_with_predict(const DebugContext *from, uint16_t start_pc,
                             const DebugPredictResult &pred,
                             bool prefer_jsr_target,
                             DebugContext *out, uint8_t cpu_port,
                             const MonitorBreakpoints *breakpoints = 0,
                             uint16_t skip_breakpoint_address = 0,
                             bool skip_breakpoint_address_valid = false);
};

#endif
