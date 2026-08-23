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
    virtual uint8_t peek_run_marker(uint16_t address) { return peek_visible(address); }
    virtual void poke_visible(uint16_t address, uint8_t byte) = 0;
    virtual void poke_visible_preserving_freeze_restore(uint16_t address,
                                                        uint8_t byte)
    {
        poke_visible(address, byte);
    }
    virtual void unfreeze_if_accessible(void) = 0;
    // A debug step must temporarily unfreeze the C64 to run the live CPU, then
    // re-freeze it before returning, or the monitor renders the live C64
    // screen instead of the firmware/menu screen. Default no-op below is for
    // backends without a freeze concept.
    virtual bool machine_is_frozen(void) const { return false; }
    virtual void refreeze_machine(void) { }
    virtual bool reset_machine(void) = 0;
    // Pulse NMI and release the stopped session. The request MUST be raised
    // while still stopped so it is observed on resume; implementations
    // bracket the resume with the request/clear pair.
    virtual void pulse_nmi_and_release(bool stopped_it) = 0;
    virtual void request_staged_nmi(void) { }
    virtual void clear_staged_nmi(void) { }
    // Begin a stopped session suitable for immediate patched high-memory release.
    // The default is a normal stopped session; the U64 backend overrides it with a
    // raster-synced ("clean") stop so the live 6510 reliably observes the patch.
    virtual bool begin_clean_stopped_session(void);
    // True when visible-ROM opcode fetches can lag monitor writes.
    virtual bool visible_rom_fetch_lags(void) const { return false; }
    virtual void delay_ms(int ms) = 0;
    virtual bool free_run_no_breakpoint(uint16_t address);
    // A cartridge target can launch a contextless run through its boot-cart
    // handoff when it cannot deliver the debugger's NMI redirect. The caller
    // keeps the BRK handler and patches installed across this launch.
    virtual bool supports_contextless_breakpoint_launch(void) const
    { return false; }
    virtual bool prepare_contextless_breakpoint_launch(uint16_t) { return true; }
    virtual bool launch_contextless_with_breakpoints(uint16_t) { return false; }
    // Page-three facts for a backend that has to rebuild that page itself.
    static uint16_t hard_brk_stub_address(void);
    // Zeroes, in the caller's image of page three, the bytes only the launched
    // program may write, so a trap taken during the handoff cannot be read back
    // as the run's result.
    static void clear_run_result_markers(uint8_t *page, uint16_t base,
                                         uint16_t length);
    virtual uint8_t read_patch_byte(uint16_t address, uint8_t cpu_port);
    // The byte a patch records so it can put it back, which is not always the
    // byte read_patch_byte answers with. A backend whose patch store and live
    // aperture can disagree - the U64's volatile ROM images - reads this one
    // from the store, while everything else keeps reading what the 6510 will
    // fetch. Only what a patch will later write back goes through here.
    virtual uint8_t read_patch_original_byte(uint16_t address, uint8_t cpu_port)
    { return read_patch_byte(address, cpu_port); }
    virtual void write_patch_byte(uint16_t address, uint8_t byte, uint8_t cpu_port);
    // write_patch_byte plus a read-back and a bounded rewrite, for the restore
    // direction, where a lost write leaves a BRK behind in the program.
    void write_patch_byte_verified(uint16_t address, uint8_t byte,
                                   uint8_t cpu_port, MonitorBackingStore target);
    virtual void note_captured_cpu_port(uint8_t) { }
public:
    BrkDebugSession();
    virtual ~BrkDebugSession();

    virtual bool blocking_breakpoint(uint16_t *address) const
    {
        if (!blocking_bp_valid) {
            return false;
        }
        if (address) {
            *address = blocking_bp_address;
        }
        return true;
    }
    virtual void set_cancel_keyboard(Keyboard *keyboard);
    virtual void set_run_window_refreeze_enabled(bool enabled);
    virtual void request_reset_cancel(void);
    virtual Result snapshot(DebugContext *ctx);
    // True while the BRK handler or spin loop owns the CPU.
    virtual bool is_debug_session_active(void) const {
        return handler_installed || cpu_parked_in_spin;
    }
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
    virtual bool has_parked_context_handoff(void) const;
    virtual bool has_parked_context(void) const { return cpu_parked_in_spin; }
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
        MonitorBackingStore target;
    };

    enum {
        MAX_PATCHES = 16,
        // Step Out walks this stack of Step-Into return targets. The 6510 hardware
        // stack ($0100-$01FF) holds at most 128 return addresses, so sizing this to
        // 128 means the cap can never drop a real frame and falsely report
        // NOT IN SUBROUTINE - which an 8-entry cap did once nesting passed depth 8.
        MAX_RETURN_TARGETS = 128
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
    volatile bool reset_cancel_requested;
    uint8_t saved_handler_bytes[128];
    uint8_t saved_nmi_trampoline_bytes[24];
    bool nmi_trampoline_installed;
    uint8_t saved_nmi_vector[2];
    uint8_t saved_brk_vector[2];
    uint8_t saved_hard_nmi_vector[2];
    uint8_t saved_hard_vector[2];
    uint8_t saved_hard_rom_vector[2];
    uint8_t saved_hard_brk_stub_bytes[48];
    uint8_t saved_hard_brk_vector_ptr[2];
    bool hard_nmi_vector_installed;
    bool hard_vector_installed;
    bool hard_rom_vector_installed;
    bool has_last_context;
    DebugContext last_context;
    bool has_resume_context;
    DebugContext resume_context;
    uint16_t return_targets[MAX_RETURN_TARGETS];
    uint8_t return_target_count;

    bool reserved_patch_address(uint16_t addr) const;
    // Called once when the CPU is about to run, for a backend that caches a
    // CPU-port reading the running program can invalidate. Default no-op: a
    // backend that reads the port register directly has nothing to drop.
    virtual void on_cpu_run_window_open(void) { }
    void begin_run_window(void);
    void end_run_window(void);
    void save_and_install_handler(void);
    void save_and_install_hard_vector(void);
    void save_and_install_visible_hard_vector(void);
    void restore_stale_visible_hard_vector(void);
    void save_and_install_hard_nmi_vector(uint8_t cpu_port);
    void uninstall_hard_nmi_vector(void);
    void uninstall_hard_vector(void);
    void uninstall_handler(void);
    int find_free_patch(void);
    bool already_patched(uint16_t addr, MonitorBackingStore target);
    PatchInstallResult install_brk_at(uint16_t addr, uint8_t cpu_port);
    PatchInstallResult install_brk_at(uint16_t addr, uint8_t cpu_port,
                                      MonitorBackingStore target);
    PatchInstallResult install_breakpoints(const MonitorBreakpoints *breakpoints,
                                           uint16_t skip_address,
                                           MonitorBackingStore skip_target,
                                           bool skip_address_valid,
                                           bool skip_all_at_address = false);
    // The armed breakpoint install_breakpoints() could not place. Recorded so
    // the refusal can name it; cleared on every successful table install.
    uint16_t blocking_bp_address;
    bool blocking_bp_valid;
    bool context_at_breakpoint(const DebugContext &ctx,
                               const MonitorBreakpoints *breakpoints,
                               uint16_t skip_address,
                               MonitorBackingStore skip_target,
                               bool skip_address_valid,
                               bool skip_all_at_address = false) const;
    void restore_patches(void);
    bool has_banked_ram_patch(void) const;
    bool has_high_memory_patch(void) const;
    bool has_any_patch(void) const;
    bool captured_at_installed_patch(uint16_t *captured_brk_pc = 0);
    void reinstall_handler_bytes(void);
    void repark_running_cpu(uint8_t cpu_port);
    Result relaunch_on_breakpoint_runaway(Result waited,
                                          const DebugContext *launch_ctx,
                                          bool nmi_launch_valid,
                                          uint16_t nmi_target,
                                          bool nmi_force_cpu_port,
                                          uint8_t cpu_port,
                                          int wait_ms);
    void fill_vectors(DebugContext *ctx, uint8_t cpu_port);
    void clear_return_targets(void);
    void push_return_target(uint16_t target);
    bool peek_return_target(uint16_t *target) const;
    void pop_return_target(uint16_t target);
    uint8_t execution_cpu_port(const DebugContext *ctx) const;
    void drop_queued_execution_keys(void);
    Result wait_for_sentinel(int timeout_ms);
    void read_captured_context(DebugContext *ctx, uint8_t cpu_port);
    void restore_cpu_port_registers(const DebugContext &from);
    void release_to_run(const DebugContext *from);
    void resume_from_parked_context(const DebugContext &from);
    void reset_spin_target(void);
    void nmi_redirect_to(uint16_t target, uint8_t cpu_port,
                         bool force_cpu_port, bool staged = false);
    Result perform_run(const DebugContext *from, uint16_t start_pc,
                       bool use_start_pc, DebugContext *out, uint8_t cpu_port);
    // Opens the run window through the backend's own contextless launch. On
    // failure the patches and handler are already removed, so the caller only
    // has to report the refusal.
    bool launch_contextless_run_window(uint16_t start_pc);
    Result step_with_predict(const DebugContext *from, uint16_t start_pc,
                             const DebugPredictResult &pred,
                             bool prefer_jsr_target,
                             DebugContext *out, uint8_t cpu_port,
                             const MonitorBreakpoints *breakpoints = 0,
                             uint16_t skip_breakpoint_address = 0,
                             bool skip_breakpoint_address_valid = false,
                             const uint8_t *linear_step_bytes = 0,
                             bool allow_linear_interpret = true);
    Result step_linear_via_trampoline(const DebugContext *from,
                                      uint16_t start_pc,
                                      const DebugPredictResult &pred,
                                      DebugContext *out, uint8_t cpu_port,
                                      const uint8_t *instruction_bytes = 0);
    Result interpret_simple_linear(const DebugContext *from,
                                   uint16_t start_pc,
                                   const DebugPredictResult &pred,
                                   DebugContext *out, uint8_t cpu_port,
                                   const uint8_t *instruction_bytes = 0);
    bool step_bank_is_ram_under_rom(uint16_t addr, uint8_t cpu_port) const;
    bool step_bank_fetch_unreliable(uint16_t addr, uint8_t cpu_port) const;
    Result emulate_control_flow_step(const DebugContext *from,
                                     uint16_t start_pc,
                                     const DebugPredictResult &pred,
                                     bool prefer_jsr_target,
                                     DebugContext *out, uint8_t cpu_port,
                                     bool force = false,
                                     const uint8_t *insn_bytes = 0);
    bool frozen_rom_run_unreliable(uint16_t launch_pc, uint16_t landing_pc,
                                   bool landing_valid, uint8_t cpu_port);
    Result parked_step_walk(const DebugContext &start, uint16_t stop_pc,
                            uint8_t stop_sp, const MonitorBreakpoints *bps,
                            uint16_t skip_breakpoint_address,
                            bool skip_breakpoint_address_valid,
                            DebugContext *out, uint8_t cpu_port);
};

#endif
