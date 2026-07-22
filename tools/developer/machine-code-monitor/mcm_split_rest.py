"""Split-session REST proxy for driving a U2+L cartridge's MCM.

The U2+L cartridge (overlay host) renders the MCM overlay and owns its own
menu_button, but its firmware does NOT support REST keyboard injection
(machine:input -> HTTP 501, it is #if U64 only) and, per the split-session
model, its C64 memory oracle is routed through the C64U it is plugged into.
So a single logical Rest is split across two physical devices:

  - machine ops (readmem / writemem / reset / input / release_all / send_text)
    -> the C64U (machine host); keystrokes reach the U2+L's MCM over the
    cartridge bus, memory is the shared physical C64 RAM/ROM.
  - overlay ops (menu_screen / menu_button) and device config / identity
    (/v1/configs, /v1/version, /v1/info, files) -> the U2+L (overlay host);
    menu_screen is per-device local UI state, only readable from the U2+L.

This presents the exact public surface of mcm_rest.Rest, so existing harness
code (overlay_lifecycle_clean, mcm_localui, monitor_debug_stress.RestSession)
drives it unchanged - each call is routed to the right device by method (and,
for the generic req(), by path).

Confirmed live 2026-07-21: cursor key to the C64U moves the U2+L overlay
selection; U2+L machine:input = 501; C64U machine:menu_screen = 404.
"""

import mcm_rest as R

# machine:* endpoints that must hit the C64U even when reached via the generic
# req() path (configs/version/files/menu default to the overlay device).
_MACHINE_REQ_TOKENS = (
    "machine:readmem",
    "machine:writemem",
    "machine:reset",
    "machine:input",
)


class SplitRest:
    def __init__(self, machine_host, overlay_host, timeout=10.0):
        self.machine = R.Rest(machine_host, timeout=timeout)
        self.overlay = R.Rest(overlay_host, timeout=timeout)
        self.machine_host = machine_host
        self.overlay_host = overlay_host

    # Identity: helpers that read .host do so for memory/bootstrap writes or a
    # ping/telnet liveness probe; the machine host satisfies both (the C64U runs
    # its own telnet/ftp too), and is the device whose memory the oracle reads.
    @property
    def host(self):
        return self.machine_host

    # --- machine ops -> C64U ---
    def read_mem(self, addr, length):
        return self.machine.read_mem(addr, length)

    def write_mem(self, addr, data):
        return self.machine.write_mem(addr, data)

    def reset(self):
        return self.machine.reset()

    def tap(self, inputs):
        return self.machine.tap(inputs)

    def release_all(self):
        return self.machine.release_all()

    def send_text(self, text, settle=0.12):
        return self.machine.send_text(text, settle=settle)

    # --- overlay ops -> U2+L ---
    def menu_button(self):
        return self.overlay.menu_button()

    def menu_screen_raw(self):
        return self.overlay.menu_screen_raw()

    def screen_lines(self):
        return self.overlay.screen_lines()

    def screen_text(self):
        return self.overlay.screen_text()

    # --- readiness: both physical devices must be up ---
    def alive(self, timeout=3):
        return self.machine.alive(timeout=timeout) and self.overlay.alive(timeout=timeout)

    # --- generic req(): machine:* -> C64U, everything else (configs, version,
    #     files, menu_button/menu_screen) -> U2+L overlay device ---
    def req(self, method, path, params=None, body=None, ctype=None):
        low = path.lower()
        target = self.machine if any(tok in low for tok in _MACHINE_REQ_TOKENS) else self.overlay
        return target.req(method, path, params=params, body=body, ctype=ctype)
