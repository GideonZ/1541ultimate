# Internal TLS stream messages

Development payload contract, not a public C64 interface. Outer UART command
0x30 with thread marker 0xfe is experimental and requires upstream allocation
review. `tls_wire.c` validates frames independently of FreeRTOS. The ESP
dispatcher and management receive ISR copy frames into bounded queues and
release their UART buffers immediately. Separate workers own TLS and TCP.

All multibyte fields are little endian. The header is 32 bytes and the payload
is at most 512 bytes, within the existing UART buffer size including RPC framing.

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 2 | Magic ASCII US |
| 2 | 1 | Version 2 (version 1 is rejected) |
| 3 | 1 | Request 1, reply 2 |
| 4 | 1 | TLS stream 1, TCP tunnel 2 |
| 5 | 1 | Open 1, read 2, write 3, close 4, time 5, reserve 6 |
| 6 | 2 | Reserved, zero |
| 8 | 4 | Nonzero session generation; 0xffffffff for reserve |
| 12 | 4 | Nonzero operation sequence |
| 16 | 2 | Remaining milliseconds, 1–15000 in requests, zero in replies |
| 18 | 2 | Payload length, or requested read length |
| 20 | 4 | Signed result; zero in requests |
| 24 | 4 | Nonzero client exchange counter |
| 28 | 4 | Nonzero durable management boot counter |

Open carries a two-byte TCP port followed by an ASCII DNS hostname without a
terminator. Read requests carry no payload. Write carries bytes. Close and
time requests carry no payload. Time is a TCP tunnel operation only; a successful
reply carries four-byte UTC epoch seconds and four-byte synchronization age in
seconds. The receiver must reject stale time, account for transit latency, and
set its system UTC clock before calling TLS. Shape validation alone does not
establish freshness or trust.

Negative replies carry no payload. Successful open/close replies have result
zero and no payload. Read returns the byte count and exactly that many bytes;
write returns the consumed count without payload. Counts may not exceed the
corresponding request. Time returns result zero and exactly eight payload bytes.

Reserve is a TLS-domain operation with no request payload. Its successful reply
has result zero and a four-byte session token, excluding zero and 0xffffffff.
The controller closes an existing session before granting a new token. Tokens
advance from a random controller boot seed without wrapping, and each grant
permits only one open. An unused grant expires at its deadline or when replaced
by a newer reservation. Management adopts the token before sending open.

Match domain, operation, session, sequence and the eight-byte client epoch
before consuming a reply, including reserve replies. Retire
a session before a counter wraps; do not reuse identifiers while old replies
can remain in flight. Only one pending operation per session is intended.
Malformed/stale frames must be discarded with their UART buffer released.
The workers own socket/session cleanup and accept a matching close even after
its queue deadline. The session-service tests cover ownership, repeated
sequences, errors and expiry. Real scheduler, queue-pressure and Wi-Fi
coexistence tests remain hardware validation requirements.

## Restart handling

The client epoch is `(boot_counter << 32) | exchange_counter`. Management
durably advances its boot counter before its first HTTPS exchange in each
firmware run, then increments its RAM exchange counter for each connection.
Neither counter wraps. Every request and reply, in both domains, carries this
epoch. TCP ownership additionally requires the currently active epoch. The TLS
service accepts reserve only for an epoch newer than its current one; a delayed
older reserve cannot close the current stream. Other operations must match the
reserved/active epoch exactly, including cleanup.

A restarted manager therefore rejects its old grant/open/data replies even
when their session and operation counters match. After an independent
controller restart, the next client exchange still has a different epoch,
even if the controller repeats a session token or random seed. An in-flight
exchange may fail or time out on controller restart; it is never automatically
retried as a new HTTP request. Bridge version 1.16 advertises this wire revision.

The durable counter uses two private 16-byte records in
`/flash/.https-epoch-a` and `/flash/.https-epoch-b`. Each contains the magic
`UTEPOCH2`, a little-endian boot counter and its bitwise complement. Advance
overwrites only the older slot, synchronizes storage and verifies readback
before issuing an epoch. Both absent means first-use initialization; one
missing, malformed data, a storage error or counter exhaustion fails closed.
Initialization and advancement failures disable HTTPS for that management
run; HTTP remains available. These files are device state, not exportable
configuration, and must not be deleted, restored from backup or rolled back
while either endpoint or its UART queues can retain previous traffic.

The guarantee assumes committed flash data survives ordinary resets. Host
tests cover torn record writes and I/O failures, but do not prove FAT metadata
or physical flash power-loss behavior. Erasing/restoring the flash filesystem
requires a full shutdown of both endpoints and clearing transport state before
HTTPS is used again. This protocol rejects accidental stale UART traffic;
it does not authenticate an attacker with access to the internal UART bus.

The restart regression tests replay recorded grant/open/read/close packets,
repeat controller seeds, and check both TLS and TCP reply matching. Real UART
queue pressure, independent hardware resets and storage power interruption
remain device validation requirements.

## Read-only failure telemetry

Bridge 1.18 keeps stream wire version 2 and extends the separate `CMD_TLS_METRICS`
packet to telemetry version 2. Its four-byte `TM`, version, reserved header is
followed by 18 little-endian 32-bit words (76 bytes total): the original ten heap,
stack, uptime and identity values, followed by failure count, epoch low/high,
session, TLS stage, `https_status`, signed error bits and verification flags.
Management accepts the original 44-byte version-1 heap packet too; it does not
invent failure fields for that version. Any other header/length is rejected
without modifying the cached sample.

The worker latches the latest stream failure before cleanup. At most one metrics
packet per second is attempted without blocking; dropped telemetry is retried
by later periodic samples, without delaying or retrying the HTTP exchange.
Management separately retains the first local failure per monotonically newer
epoch under a critical section. Match epoch/session and boot identity when
correlating both records. A historical failure can remain visible after recovery.
No public UCI command, authentication rule, timeout or plaintext fallback changes.

Management records `tcp_session_deadline` before expiring an active socket
owner between requests, and `tcp_queue_deadline` when an active request's
remaining time expires in the queue. Both use code zero; queue detail is the
TCP wire operation. Cancellation or stale ownership does not create a deadline
record. These primary records survive a later generic `tls_reply` failure for
the same epoch. They complement `tcp_timeout` from a timed socket wait and
`tcp_deadline` from a wait entered after its budget elapsed.
