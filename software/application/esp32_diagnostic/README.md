# RAM-only controller identification

This local diagnostic targets the C64 Ultimate RV32I loader and FPGA version 122.
It is not an installer. Physical execution has not been qualified.

The `.ue2` contains one executable RAM segment at `0x03000000`, not FPGA,
management or ESP32 firmware payloads. It links no operating system, filesystem,
flash drivers, heap or global constructors. The installed loader transfers control
to it, so normal firmware and networking stop servicing requests until a later
planned maintenance exit. Do not treat it as a command that returns to BASIC.

The diagnostic accepts only the existing 5 Mbaud, flow-controlled SLIP session
with the module in its normal C64U run state. It preserves the module enable,
boot and route pins and baud rate. Local DMA is reset to take ownership of the
receive buffer; that interrupts outstanding network traffic. One runtime
`CMD_IDENTIFY` request is allowed, with a two-second timer budget and a finite
poll limit. There are no controller restarts, baud sweeps, retries or download-mode
commands. Unsupported state, overflow, malformed response or timeout produces
UNKNOWN. Unknown results must never authorize installation.

The response must match command, thread, sequence, length, terminated printable
name and numeric version. The legacy three-byte structure padding is allowed.
The reported version is not an image hash. The display is inherited from the
existing menu video configuration; actual display and UART timing remain to be
verified on hardware in a separately approved session.

Build from `target/u64ii/riscv/esp32_diagnostic` with `make CROSS=...` using the
project's RISC-V compiler prefix. `result/esp32-identity.ue2` is held for review,
not automatically copied to a device. No hardware run command is supplied here.

Local verification:

- `tests/identity_test.cc`: parser and UART state/control tests; run with address
  and undefined-behavior sanitizers, including a stopped timer.
- `tests/rv32_audit.py PACKAGE`: executes the actual package instructions in a
  constrained RV32I/MMIO model. Writes outside RAM, display, interrupt disable
  and approved UART registers fail; any transmission other than one identity
  request fails. This is not FPGA simulation or physical timing evidence.
- `tests/installer_test.py ROOT OUTPUT`: compiles the actual updater function
  bodies with recording platform stubs. Includes alone are replaced. Checks
  pre-write rejection, both board paths, cancellation of the update prompt and
  terminal behavior after write failures. Flash drivers and UI initialization
  are not simulated by those stubs.
- `tests/mutation_audit.py ROOT OUTPUT`: builds unsafe local variants and requires
  behavioral rejection. Compiler failure alone does not count as rejection.

## Coordinated updater changes

The standard updater retains upstream behavior. A separate
`COORDINATED_HTTPS_UPDATE=1` build selects `update_https_u64ii.cc`. It checks board support, native/FPGA/controller payload checksums and
controller identity before `setup`, whose flash mount can otherwise format an
uninitialized volume. It stops on unknown, equal-but-unverified or newer versions.
Only an identified older controller is eligible. `NO_ESP` builds stop rather than
installing an unmatched native image. An equal version requires a future explicit
exact-image policy; no downgrade or version-number workaround is provided.

Unlike this diagnostic, updater preflight intentionally starts the controller once
and waits before querying. It must not be described as a read-only procedure.
The existing UI/configuration setup can allocate settings or mount/format storage
after preflight succeeds; cancelling its final update prompt is not a guarantee
that startup made no persistent changes. The tests do not claim that guarantee.

FPGA/management programming failures now stop progression. Controller download or
programming failures propagate to a terminal failed result instead of continuing
to reset configuration or normal completion. Already completed writes cannot be
rolled back by that stop. No transactional-update claim is made.

Post-write exact ESP32 image readback/hash verification and practical recovery
qualification remain unresolved. Transfer acknowledgements alone do not provide
that proof. Neither the diagnostic build nor the revised updater is installation
ready on the strength of the local tests alone.

Coordinated updater builds must set `MANAGEMENT_SHA256` to the approved
`../ultimate/result/ultimate.app` hash. This verifies the supplied file and prevents
the recursive management build from silently changing the paired payload. They
also require `CONTROLLER_SHA256`; the exact controller payload hash and its
advertised provider version are checked before linking. The native updater uses
the provider-enabled version branch for its preflight comparison. These build
checks do not replace post-write verification. All carried payloads still require
the independent package/checksum audit.
