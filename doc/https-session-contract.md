# HTTPS session ownership proposal

This is a local design with an executable host model and staged firmware work.
It does not install a production TLS provider or change target-6 HTTP commands,
handles, rendering, response blocks or status strings. The model lives under
`software/network/tests` and is excluded from firmware builds.

## Ownership

Each exchange has one owner task. `HttpTarget` owns the connection returned by
its borrowed secure-provider factory; `HttpRequest` borrows that connection.
The request is destroyed before the connection. The provider's callback context
outlives both. A connection instance is single-use, including when it is closed
before open. Recovery creates a fresh instance; it never replays a failed write.

Only the owner calls open/read/write/close or mutates a session. A cancellation
source sets a session-specific atomic flag and wakes a cooperative wait; it
does not free TLS state, close a descriptor from another task, or call target
cleanup concurrently with a running exchange. The owner observes cancellation,
discards an uncommitted result, closes once, and then releases objects. Repeated
cleanup is harmless and preserves the first causal diagnostic.

The existing target executes exchanges synchronously. Its abort/reset handlers
are cleanup paths, not proof of an asynchronous cancellation mechanism. Firmware
wiring must demonstrate how abort/reset reaches the owner while it is blocked.

## Negotiation and volatile identity

A proposed backend link must complete a solicited capability exchange before
HTTPS admission. Match a contract version and required semantics: authenticated
open, authenticated EOF, one total deadline, cooperative cancellation and session
identity. Unknown optional features may be ignored; missing required features,
unknown versions, failed initialization and absent responses disable HTTPS.
Plain HTTP keeps its existing backend. Product/firmware version strings alone
are insufficient to establish these capabilities.

Each capability reply echoes the current controller incarnation and probe ID,
and supplies the provider incarnation. Only the outstanding probe can be accepted,
once. Re-probing retires the previous probe and makes existing leases unusable.
An unsolicited old reply must not roll the current provider identity backward.

An operation is identified by the negotiated pair, a controller-issued monotonic
session ID and a per-session operation ID. All fields and the local owner must
match before a completion can affect state or publish data. Only one operation
is outstanding on a session. Replayed, reordered or foreign completions do
nothing; a wrong-session cancellation cannot affect its neighbor.

The model uses scalar symbolic incarnation IDs. These are not a wire layout or
a source of uniqueness. Production must obtain fresh, adequately sized boot
identities from a qualified entropy source, independent of Wi-Fi state, or a
separately justified fresh-channel mechanism. Failure disables negotiation.
Counters must fail closed on exhaustion; they never wrap within an incarnation.
No files, persistent epoch writes or storage-based lockouts are proposed.

## Restart, cancellation and completion

| Event | Owner action |
| --- | --- |
| Open succeeds with matching identity | Permit I/O after authentication |
| Open fails | Retain failure; close once; require a new exchange |
| Read/write completion matches pending operation | Check cancellation/deadline/epoch before committing it |
| Cancellation and deadline observed together | Cancellation wins at that observation point |
| Cancellation before completion | Drop completion; close once; retain cancellation |
| Completion before cancellation | Bytes already committed cannot be retracted; prevent further operations |
| HTTP completion already committed | Later cancel/expiry cannot rewrite its result |
| Either endpoint restarts or link is invalidated | Stop publication from old leases; renegotiate; never resume/retry old work |
| Repeated close or stale completion | No second release or publication |

The absolute exchange deadline starts before open and is not refreshed by partial
progress, polling, operation changes or renegotiation. The adapter continues to
enforce its deadline during actual TLS work. The model uses a supplied monotonic
clock and checks the boundary before publication; it does not synchronize clocks
between processors. A future remote provider receives bounded remaining budgets
and must also enforce an owner-loss lease so orphaned sockets cannot persist.

Restart fencing and resource recovery are separate requirements. Matching a new
incarnation rejects old data but does not prove old sockets/workers were freed.
A production provider must reap the previous controller's work and acknowledge
initialization before admitting new work. If cleanup cannot be confirmed within
its bound, quarantine the link and report failure. Do not recycle a live slot
or accept a late close acknowledgement for a new occupant. Admission limits and
cleanup bounds require measured firmware resources before they can be set.

## Validation and remaining integration

The host model tests old/new capability pairings, failed initialization, probe
replay, independent restarts, duplicate/late operations, wrong owners, cancellation
interleavings, session isolation, deadline boundaries, exhaustion and fresh-session
recovery. It is a deterministic ordering model, not a thread scheduler, serializer,
UART transport, session pool or implementation of remote lease reclamation.

Actual TLS tests separately exercise atomic cancellation during handshake/read/
write, bounded cleanup, terminal adapters, exact peer bytes and recovery under
RSA and ECDSA. They do not validate the proposed remote identity protocol.

The first firmware increment connects C64 reset and UCI abort interrupts to a
lock-free cancellation source. Each queued event carries its admission token,
so queued commands from before a reset cannot be admitted afterwards, even if
the reset notification itself could not be queued. Sticky flags preserve owner
cleanup. Reset/abort handlers only signal; the dispatcher and target retain
ownership of destruction. A cancelled command or continuation cannot publish
its reply: the dispatcher checks the token with interrupts disabled while copying
the bounded reply and updating the handshake. The target also rejects cancelled
secure exchanges before creation, after receiving and before buffered delivery.

The optional secure factory receives a token by reference and must copy it into
the session callback context. Poll it in bounded transport waits and the TLS
cancellation callback. The existing adapter forwards that callback to the TLS
core. The token for a buffered exchange remains separate from later command
tokens. No HTTP command, handle layout or status text changes. The default
firmware still has no secure provider and HTTPS remains unavailable.

There is one serialized interrupt producer; this source is not a multi-producer
event bus. Generation exhaustion fails closed until reboot, without wrapping.
These in-memory generations detect local C64 resets/aborts only. They do not
identify ESP32 boots, survive management restart or implement the remote lease
protocol described above. Host dispatcher tests use real dispatcher/ISR code
with fake registers/queues; they do not prove physical interrupt timing. The
32-bit event grows from 1 to 12 bytes: the 16-entry queue adds 176 payload bytes.
Two target tokens add 16 bytes and the static source/abort flag add 5 bytes before
padding. No per-event heap allocation or persistent epoch storage is introduced.

Before enabling a firmware provider: choose provider placement/product scope; specify
message validation and queue bounds; implement owner routing, restart discovery,
orphan reclamation and admission control; qualify entropy/time/trust; prove UCI
publication ordering and cancellation races on the chosen scheduler; then measure
whole-device resources. Session identity is a freshness mechanism, not peer
authentication or protection against a malicious local transport.

## ESP32 identity and provider lifetime

The U64-II controller adds an optional extension to UART `CMD_IDENTIFY` (0x02).
Legacy four-byte requests retain the legacy response format. Extended requests
are exactly 16 bytes: command 0x02, thread 0xff, zero sequence, ASCII `PID1`, then
an eight-byte little-endian probe counter. The response retains the version and
NUL-terminated name at offset 8, followed immediately by a 32-byte trailer:
`PID1`, format version 1, three zero reserved bytes, echoed counter and a 16-byte
boot identity. No padding or trailing bytes are permitted. Names are bounded to
64 bytes including NUL. Identity format version is not a TLS capability version.

The boot identity is created once at application startup using `esp_fill_random`
while the ESP-IDF internal entropy source is enabled. That source is disabled
before ADC/RF initialization. An all-zero identity disables the extension; it
does not trigger a retry or a storage write. The controller version is bumped
to 1.15 so the existing updater can distinguish this local candidate later.

For U64-II only, a 100 ms software timer submits a probe at most every 5 seconds.
Buffer acquisition and transmit queue submission have zero wait. Identity replies
on thread 0xff are consumed and freed in the UART ISR, separately from legacy
per-task notifications. The 1500 ms reply deadline is not extended by malformed,
duplicate or stale packets. Timer-side updates and stop/disconnect notifications
are serialized with the receive ISR using short critical sections. Timer/resource
failure leaves the identity unavailable. No synchronous Wi-Fi RPC is called by
this monitor. Physical scheduling latency has not been measured.

A changed boot identity, timeout, submission failure or explicit invalidation
retires the old provider lifetime. A valid same-boot heartbeat preserves it.
Responses from retired probes cannot roll identity backwards. Stop/disconnect
invalidates existing tokens without touching owner resources. HTTP factories can
expose availability and a lifetime token; the target checks both around creation
and before response delivery. A factory must also poll its captured lifetime in
TLS callbacks and bounded waits. Recovery uses a fresh instance, never a replay.

The dispatcher calls a nonblocking target publication hook with interrupts masked
immediately before copying a reply. If the provider changed after HTTP parsing,
the target substitutes the existing empty 503 response. After leaving the critical
section, the owner releases buffered data and any newly created response handles
that were never published. C64-aborted or no-reply secure commands also release
those unpublished handles. Already committed responses are not retracted.

This implements identity monitoring, not TLS capability negotiation or remote
cleanup acknowledgement. The probe counter correlates replies within a management
run; it is not a qualified controller incarnation across management reboots.
Initial identity observation must not authorize remote session reuse. The monitor
does not change the Wi-Fi connection state or recover legacy blocked RPCs. Older
controllers and other product variants keep ordinary HTTP; they cannot satisfy
this identity extension. The default secure factory remains absent. Production
TLS admission still requires the boot-pair/capability/cleanup contract above.
