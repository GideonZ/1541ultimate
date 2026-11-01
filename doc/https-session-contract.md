# HTTPS session ownership and restart contract

This contract is implemented by the native manager, copied UART queues and ESP32
owner task. A separate host model checks event ordering; the model is not linked
into firmware. Target-6 HTTP commands, handles, rendering and status strings are
unchanged. All production activation remains opt-in.

## Ownership

`HttpTarget` owns its connection; `HttpRequest` borrows it. The request is destroyed
first. A connection is single-use, including close before open. Recovery creates
a fresh exchange and never replays a failed write.

Only the owner executes TLS operations or frees TLS resources. Other tasks signal
cancellation and copy messages into bounded queues. They do not close sockets or
free session state while the owner is blocked. The owner observes cancellation,
discards uncommitted results and cleans up once before acknowledging close.

## Identity and admission

A solicited identity exchange and the control handshake must succeed before work
is admitted. Product version numbers are not capability evidence. Replies must
match the outstanding request, native incarnation, provider boot, grant, session
and operation identifiers. Foreign, stale, replayed or out-of-order completions
cannot publish data. Identity counters fail closed on exhaustion.

The native identity combines a provisioned journal domain and a committed durable
epoch. ESP32 boot identity uses its qualified boot entropy path. A host model's
symbolic identity is not a substitute for either source. The journal's rollback,
clone and power-loss limitations are described in [provisioning](../target/pc/linux/test_http_tls/PROVISIONING.md).

The control protocol uses Offer, Initialize, Renew and Close. Initialization waits
for confirmed cleanup of the previous owner. Leases expire without renewal; a
renewal must be the next expected sequence for the current unexpired grant.
Lost communication never produces a fabricated cleanup acknowledgement.

## Cancellation, restart and deadlines

| Event | Required behavior |
| --- | --- |
| Open succeeds | Admit I/O only after authentication and identity checks |
| Open fails | Retain the diagnostic, close, require a fresh exchange |
| Data completion arrives | Validate identity, cancellation and deadline before publishing |
| Cancellation is observed | Fence uncommitted data and let the owner clean up |
| Either endpoint restarts | Retire old grants; renegotiate without resuming old work |
| Reply/queue overflow or send failure | Retire the affected generation |
| Lease expires | Cancel work and release resources through the owner |
| Repeated close | No second release or publication |

The native exchange budget is 30 seconds and cleanup bound is 500 ms. Those are
configured software bounds, not measured physical guarantees. Control servicing
during long TLS calls consumes at most one event per cancellation check and never
recursively performs resource cleanup. Valid renewals preserve the owner lease
but cannot extend the original HTTP deadline.

Data ingress copies bounded frames before returning. One data operation is pending
per session; waiting for the owner consumes the same deadline. ISR code only
copies/fences input. Boot, control and data framing are explicitly versioned and
reject malformed lengths and padding. The current development framing still needs
maintainer agreement with other UART clients before it becomes a stable protocol.

## Evidence boundary

Host tests exercise real TLS peers through the connected target/manager/provider
components and separately test deterministic scheduler, flash and ISR ports.
ESP32 QEMU tests execute the production owner task with a protocol actor in place
of the native processor and physical UART. Neither proves real-device scheduling,
entropy, storage durability or safe installation. Global heap snapshots include
network cleanup state and must not be equated with scoped TLS allocation balance.
