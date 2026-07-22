#!/usr/bin/env python3
"""Anti-masking enforcement for the debugger gate harnesses.

The debugger's contextless visible-ROM breakpoint entry can, under concurrent
REST/DMA load, miss the 6510's first fetch because the closed U64 C64 core can
serve a stale pre-patch ROM byte to the live instruction fetch (documented in
doc/machine-code-monitor-rom-fetch-coherency.md). A reset does NOT create
coherency; masking that miss with a reset-and-retry loop fabricates a green result
and is prohibited. The debugger reports the genuine miss precisely
(DBG_ROM_ENTRY_UNCOHERENT) after a bounded, no-reset in-place relaunch.

This test fails if a prohibited masking pattern reappears in the pass/fail gate
harnesses. It is STRUCTURAL (AST), not plain text matching: it flags any loop
whose body both (a) resets/reconnects the machine or debug session and (b)
re-issues a debugger launch/step. Diagnostic/measurement tools (rom_entry_stress,
warmup probes) are intentionally out of scope - they reset to MEASURE the race,
not to mask a gate result.
"""
import ast
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# Gate harnesses whose green result must never depend on a hidden reset-retry.
GATE_FILES = ("monitor_debug_test.py", "monitor_debug_matrix_gate.py")

# Calls that reset the machine or re-establish a debug session/transport.
RESET_CALLS = {
    "_reset_c64_core", "reset_baseline", "reset", "reset_machine",
    "reset_from_debug_ui", "reconnect", "_reconnect_rom_debug_view",
    "_reopen_monitor", "open_monitor",
}
# Calls/keys that release the CPU toward a debugger target (a launch/step).
LAUNCH_CALLS = {
    "enter_debug_at", "_bootstrap_hit_rom_breakpoint",
    "_contextless_visible_jsr_step_over", "_acquire_rom_context_at",
    "go", "step_over", "step_into", "step_out", "continue_run",
    "continue_to_cursor", "continue_to_breakpoint", "_wait_for_pc", "wait_pc",
}
LAUNCH_KEY_ARGS = {"G", "D", "T", "U", "K"}

# Symbols that must not exist at all (their sole purpose was reset-retry masking).
BANNED_SYMBOLS = ("ROM_ENTRY_MAX_ATTEMPTS", "_reconnect_rom_debug_view")

# Documented, reviewed exceptions, keyed by (file, enclosing function name). Each
# entry must state why the loop cannot mask a debugger-command result. Currently
# empty: no gate loop legitimately resets and re-launches. (Post-debug hygiene
# recovery in _restore_safe_banking_display_hygiene resets + re-verifies machine
# LIVENESS only - it never re-issues a debugger launch, so it is not flagged.)
ALLOW_LIST: set = set()


def _call_names(node: ast.AST) -> set:
    """All attribute/function names called anywhere under `node`."""
    names = set()
    for sub in ast.walk(node):
        if isinstance(sub, ast.Call):
            fn = sub.func
            if isinstance(fn, ast.Attribute):
                names.add(fn.attr)
            elif isinstance(fn, ast.Name):
                names.add(fn.id)
            # send_key("G") / send_char("G") style launches
            if isinstance(fn, ast.Attribute) and fn.attr in ("send_key", "send_char"):
                for arg in sub.args:
                    if isinstance(arg, ast.Constant) and arg.value in LAUNCH_KEY_ARGS:
                        names.add(f"__key_{arg.value}")
    return names


def _enclosing_func(path: list) -> str:
    for node in reversed(path):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            return node.name
    return "<module>"


def _find_violations(fname: str, src: str) -> list:
    tree = ast.parse(src)
    violations = []

    # Banned symbols anywhere.
    for sym in BANNED_SYMBOLS:
        if sym in src:
            # Confirm it is a real reference, not only inside this file's own name.
            for node in ast.walk(tree):
                if isinstance(node, ast.Name) and node.id == sym:
                    violations.append(
                        f"{fname}: banned masking symbol {sym!r} reintroduced "
                        f"(line {node.lineno})")
                    break

    # Loops that both reset and launch = reset-retry masking.
    parents: dict = {}
    for node in ast.walk(tree):
        for child in ast.iter_child_nodes(node):
            parents[child] = node

    def path_to(node):
        chain = []
        cur = node
        while cur in parents:
            cur = parents[cur]
            chain.append(cur)
        return list(reversed(chain))

    def is_attempt_loop(loop) -> bool:
        # A retry loop counts attempts: `while ...` or `for _ in range(...)`.
        # A `for x in <collection>` loop iterates distinct test cases, not retries.
        if isinstance(loop, ast.While):
            return True
        it = loop.iter
        return isinstance(it, ast.Call) and isinstance(it.func, ast.Name) \
            and it.func.id == "range"

    def reset_in_except(loop) -> bool:
        for handler in ast.walk(loop):
            if isinstance(handler, ast.ExceptHandler):
                if _call_names(handler) & RESET_CALLS:
                    return True
        return False

    for node in ast.walk(tree):
        if not isinstance(node, (ast.For, ast.While)):
            continue
        names = _call_names(node)
        resets = names & RESET_CALLS
        launches = (names & LAUNCH_CALLS) | {n for n in names if n.startswith("__key_")}
        if not (resets and launches):
            continue
        # Masking = an attempt/retry loop that resets and re-launches, OR any loop
        # that resets inside an except handler around a debugger launch. A plain
        # `for case in cases` iteration (per-case setup + one launch) is not masking.
        if not (is_attempt_loop(node) or reset_in_except(node)):
            continue
        func = _enclosing_func(path_to(node))
        if (fname, func) in ALLOW_LIST:
            continue
        violations.append(
            f"{fname}:{node.lineno}: attempt/except loop in {func}() both resets "
            f"({sorted(resets)}) and launches a debugger op "
            f"({sorted(launches)}) -> reset-retry masking. If legitimate, add "
            f"({fname!r}, {func!r}) to ALLOW_LIST with justification.")
    return violations


def main() -> int:
    all_violations = []
    for fname in GATE_FILES:
        path = HERE / fname
        if not path.exists():
            all_violations.append(f"{fname}: missing (expected gate harness)")
            continue
        all_violations.extend(_find_violations(fname, path.read_text(encoding="utf-8")))

    # The matrix gate must declare the prohibited masking counters and keep them
    # in its acceptance invariant.
    try:
        sys.path.insert(0, str(HERE))
        import monitor_debug_matrix_gate as gate  # noqa: E402
        for required in ("recovery_reset", "command_retry", "session_replay",
                         "transparent_reset_restore_failure"):
            if required not in gate.ResetRetryCounters.PROHIBITED:
                all_violations.append(
                    f"monitor_debug_matrix_gate.py: {required!r} missing from "
                    f"ResetRetryCounters.PROHIBITED acceptance invariant")
    except Exception as exc:  # noqa: BLE001
        all_violations.append(f"could not import matrix gate for counter check: {exc}")

    if all_violations:
        print("ANTI-MASKING ENFORCEMENT: FAIL")
        for v in all_violations:
            print(f"  - {v}")
        return 1
    print("ANTI-MASKING ENFORCEMENT: OK (no reset-retry masking in gate harnesses)")
    return 0


def test_no_reset_retry_masking():
    assert main() == 0


if __name__ == "__main__":
    sys.exit(main())
