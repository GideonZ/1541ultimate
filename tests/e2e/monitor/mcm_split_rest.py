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
code (overlay_lifecycle, mcm_localui, monitor_debug_stress.RestSession)
drives it unchanged - each call is routed to the right device by method (and,
for the generic req(), by path).

Confirmed live 2026-07-21: cursor key to the C64U moves the U2+L overlay
selection; U2+L machine:input = 501; C64U machine:menu_screen = 404.
"""

import json

import mcm_rest as R

# machine:* endpoints that must hit the C64U even when reached via the generic
# req() path (configs/version/files/menu default to the overlay device).
_MACHINE_REQ_TOKENS = (
    "machine:readmem",
    "machine:writemem",
    "machine:reset",
    "machine:input",
)

# Windows the machine host can read while a cartridge session holds the bus.
# The session holds the C64 in Ultimax, which leaves only $0000-$0FFF and the
# I/O space decoded on the cartridge bus; a machine-host read outside those
# ranges returns $FF regardless of what the memory holds. Measured with a
# session live: the C64U read $C800 as FFFFFFFF while the cartridge read
# 2B33C1DC, and both read 2B33C1DC once the machine was released.
_HOST_READABLE_WHILE_HELD = ((0x0000, 0x0FFF), (0xD000, 0xDFFF))


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

    def read_mem_oracle(self, addr, length):
        """Memory read that is valid whether or not a session holds the bus.

        Reads the cartridge for any window the machine host cannot see while
        it is held. The cartridge can always see it, so this is the read to use
        for a comparison whose result must not depend on session state.
        """
        end = addr + length - 1
        if any(lo <= addr and end <= hi for lo, hi in _HOST_READABLE_WHILE_HELD):
            return self.machine.read_mem(addr, length)
        return self.overlay.read_mem(addr, length)

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


# Fields of /v1/info that identify which image is running on which board. Held
# separately from the report-formatting list in monitor_debug_matrix_test: these
# are the fields a run compares, not the fields it prints.
IDENTITY_FIELDS = ("product", "firmware_version", "fpga_version",
                   "core_version", "unique_id")


def _identity_of(device):
    try:
        _status, payload = device.req("GET", "/v1/info")
        info = json.loads(payload)
    except Exception as exc:  # noqa: BLE001 - an unreadable stamp is not a change
        return {"error": f"{type(exc).__name__}: {exc}"}
    return {name: info[name] for name in IDENTITY_FIELDS if name in info}


def device_identity(rest):
    """Reported firmware identity of every device this fixture drives.

    Stamped at the start and at the end of a run and compared, so a device that
    was reflashed or swapped part-way through fails the run instead of silently
    producing numbers that describe no single image.
    """
    if isinstance(rest, SplitRest):
        return {rest.machine_host: _identity_of(rest.machine),
                rest.overlay_host: _identity_of(rest.overlay)}
    return {rest.host: _identity_of(rest)}


def identity_changes(before, after):
    """Hosts whose reported identity differs between two stamps.

    Only a difference between two readable stamps counts. A stamp that could
    not be read proves nothing about the image, so it is recorded by the caller
    rather than reported as a change here.
    """
    changed = {}
    for host in sorted(set(before) | set(after)):
        first, second = before.get(host), after.get(host)
        if first == second or not first or not second:
            continue
        if "error" in first or "error" in second:
            continue
        changed[host] = {"before": first, "after": second}
    return changed


def endpoint_liveness(rest, timeout=3):
    """Per-device liveness for whatever fixture this is: both physical devices
    of a split session, or the single host. Printed when a wedge is detected,
    so the log names which device stopped answering instead of asserting one.
    """
    if isinstance(rest, SplitRest):
        return {rest.machine_host: rest.machine.alive(timeout=timeout),
                rest.overlay_host: rest.overlay.alive(timeout=timeout)}
    return {rest.host: rest.alive(timeout=timeout)}


def make_rest(overlay_host, machine_host=None, timeout=10.0):
    """A REST fixture bound to a topology.

    `machine_host` is the C64U a U2+L cartridge is plugged into. With it the
    fixture is a SplitRest; without it a plain single-host Rest on
    `overlay_host`. Every tool that takes a split-host flag builds its fixture
    here, so one place decides what a split session routes where.
    """
    if machine_host:
        return SplitRest(machine_host=machine_host, overlay_host=overlay_host,
                         timeout=timeout)
    return R.Rest(overlay_host, timeout=timeout)
