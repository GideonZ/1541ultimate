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
    virtual bool prepare_run_with_patches(uint8_t cpu_port);
    virtual void finish_run_with_patches(bool prepared);
    virtual void after_restore_patches(void);
    virtual uint8_t debug_run_cpu_ddr(uint8_t cpu_port);
    virtual uint8_t debug_run_cpu_port(uint8_t cpu_port);
    virtual uint8_t debug_restore_cpu_ddr(uint8_t cpu_port);
    virtual uint8_t debug_restore_cpu_port(uint8_t cpu_port);

public:
    BrkDebugSession();
    virtual ~BrkDebugSession();

    virtual void set_cancel_keyboard(Keyboard *keyboard);
    virtual Result snapshot(DebugContext *ctx);
    virtual Result over(const DebugContext &from,
                        const DebugPredictResult &pred,
                        DebugContext *ctx);
    virtual Result over_at(uint16_t start_pc,
                           const DebugPredictResult &pred,
                           DebugContext *ctx);
    virtual Result trace(const DebugContext &from,
                         const DebugPredictResult &pred,
                         DebugContext *ctx);
    virtual Result trace_at(uint16_t start_pc,
                            const DebugPredictResult &pred,
                            DebugContext *ctx);
    virtual Result step_out(const DebugContext &from, DebugContext *ctx);
    virtual Result go(const DebugContext &from,
                      const MonitorBreakpoints *breakpoints,
                      uint16_t start_pc);
    virtual void cleanup(void);

private:
    struct Patch {
        bool used;
        uint16_t address;
        uint8_t original;
        uint8_t cpu_port;
    };

    enum { MAX_PATCHES = 16 };

    Keyboard *cancel_keyboard;
    Patch patches[MAX_PATCHES];
    bool handler_installed;
    bool cpu_parked_in_spin;
    uint8_t saved_handler_bytes[160];
    uint8_t saved_nmi_trampoline_bytes[32];
    bool nmi_trampoline_installed;
    uint8_t saved_nmi_vector[2];
    uint8_t saved_brk_vector[2];
    bool has_last_context;
    DebugContext last_context;

    bool reserved_patch_address(uint16_t addr) const;
    void save_and_install_handler(void);
    void uninstall_handler(void);
    int find_free_patch(void);
    bool already_patched(uint16_t addr);
    bool install_brk_at(uint16_t addr, uint8_t cpu_port);
    void restore_patches(void);
    void fill_vectors(DebugContext *ctx, uint8_t cpu_port);
    bool find_stack_return_target(const DebugContext &from, uint8_t cpu_port,
                                  uint16_t *target);
    Result wait_for_sentinel(int timeout_ms);
    void read_captured_context(DebugContext *ctx, uint8_t cpu_port);
    void release_to_run(const DebugContext *from);
    void reset_spin_target(void);
    void set_debug_run_ports(uint8_t cpu_port);
    void nmi_redirect_to(uint16_t target);
    Result perform_run(const DebugContext *from, uint16_t start_pc,
                       bool use_start_pc, DebugContext *out, uint8_t cpu_port);
    Result step_with_predict(const DebugContext *from, uint16_t start_pc,
                             const DebugPredictResult &pred,
                             bool prefer_jsr_target,
                             DebugContext *out, uint8_t cpu_port);
};

#endif
