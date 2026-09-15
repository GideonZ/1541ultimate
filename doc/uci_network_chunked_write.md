# Ultimate Command Interface — Network Target, chunked socket write

| | |
| --- | --- |
| Author | JC-000 |
| Version | 0.3, September 2, 2026 |
| Status | Proposal for discussion on [GideonZ/1541ultimate#807](https://github.com/GideonZ/1541ultimate/issues/807) |

Sections 1 and 2 are normative and follow the structure of *UCI — Network Target*
v1.0. They state constants and required behaviour; they do not explain mechanism.
Every "because" lives in an appendix, with the file and line it rests on. The
appendices are not part of the specification; they record why the design is what
it is, and what was measured.

---

## 1. Introduction

### 1.1. Context

The "Network Target" is a target of the "Ultimate Command Interface" (UCI), and
is thus accessible from the cartridge port, through some I/O registers. The
document "Ultimate Command Interface – Register API" describes how commands are
sent over this interface.

### 1.2. Purpose of this document

`NET_CMD_WRITE_SOCKET` sends the payload carried by a single command. A command
carries at most 895 bytes, of which that command spends three, so a datagram
larger than 892 bytes cannot be sent. Splitting it across two commands sends two
datagrams, which a peer expecting one rejects.

> The 895-byte figure is not the 896 that `doc/command interface.docx` §2.5
> states. That document says the queue sizes "define the maximum transfer size
> per command" and gives 896 for the command queue; the queue is 896 bytes, and
> the maximum a command can carry is 895. See Appendix E.

This document specifies `NET_CMD_WRITE_SOCKET_CHUNK`, which assembles a payload
from several commands and sends it as one datagram.

Receiving is not affected. `NET_CMD_READ_SOCKET` returns up to 1472 bytes across
several reply blocks. After this extension the sending ceiling is the same 1472.

---

## 2. Commands

In the Ultimate products and the Commodore 64 Ultimate, the "Network Target" is
accessible through target $03. This shall be the first byte of the command.

### 2.1. NET_CMD_WRITE_SOCKET_CHUNK (0x16)

Command format: `$03 $16 <SOCKET_HANDLE> <OFFSET_LSB> <OFFSET_MSB> <TOTAL_LSB> <TOTAL_MSB> <DATA…>`

With this command a payload larger than one command can be written to an open
socket. Each command carries one chunk of the payload and states where that
chunk begins and how long the whole payload is.

The target holds the payload until it is complete. No part of a payload shall
reach the socket before the payload is complete. On a datagram socket, a
completed payload leaves as exactly one datagram.

Parameters: The offset and the total are 16-bit values (Little-Endian). The
offset is the position of this chunk's first byte within the payload. The total
is the length of the whole payload and is the same in every chunk of it. The
command length determines this chunk's data size. A chunk with an offset of zero
begins a payload; any other chunk continues the one in progress and shall repeat
its handle and its total. A payload is complete when the offset plus the length
of this chunk's data equals the total.

A total of zero names an empty payload, completed by the chunk that announces
it.

The handle shall name a socket opened by this target through
`NET_CMD_OPEN_TCP` or `NET_CMD_OPEN_UDP`. A handle that does not is reported on
the chunk that completes the payload, not earlier.

Response: The chunk that completes the payload responds with a 2-byte value,
LSB first, being the number of bytes written to the socket. A chunk that does
not complete the payload has no response.

Status: `00,OK`, except as follows, tested in this order.

1. A command shorter than seven bytes is answered `81,INVALID PARAMS`. Any
   payload in progress is discarded.
2. A chunk with an offset of zero discards any payload in progress. If its total
   is greater than 1472 it is answered `82,PARAMETER(S) OUT OF RANGE` and no new
   payload begins.
3. Any other chunk is answered `81,INVALID PARAMS` if the offset, the total or
   the handle does not match the payload in progress. The payload in progress is
   discarded.
4. A chunk whose data would carry the payload beyond its total is answered
   `81,INVALID PARAMS`, whether that chunk opened the payload or continued one.
   The payload is discarded.
5. The chunk that completes the payload is answered `12,SEND ERROR: <errno>` if
   the socket could not be written. A handle that does not name a socket opened
   by this target is reported here as `12,SEND ERROR: 9`.

### 2.2. Payload in progress

At most one payload is in progress at a time.

A payload in progress is discarded, without notice, by any other command to this
target, by an abort that reaches this target, and by a reset of the C64. An
abort reaches only the target that was addressed last. A client resumes by
sending a chunk with an offset of zero. A chunk that arrives when no payload is
in progress, and whose offset is not zero, is answered `81,INVALID PARAMS`.

A chunk shall carry at most 888 bytes of data. The interface neither enforces
this nor reports a violation on the chunk that commits it: a client that offers
more sends fewer. A chunk meant to complete a payload leaves it short instead;
any other is answered `00,OK`, and the violation surfaces one chunk later, when
the client's next offset does not match what the payload has reached and is
answered `81,INVALID PARAMS`.

A continuation chunk carrying no data is answered `00,OK` and does not advance
the payload in progress.

---

## Appendix A — Why the payload is assembled from several commands

*Not part of the specification.*

The protocol and the firmware provide no way for the C64 to say that more input
follows. The "more" bit of the state field, `state(0)`, is set in one place,
`command_protocol.vhd:224`, from the Ultimate side. The slot side writes the
state field at `:156`, and only to `01`, when pushing a command; it never writes
a data state. The more bit has meaning only in the data states `1x`, as the
header comment at `:48-52` defines them.

A command push is accepted only from the Idle state, and from any other state it
sets `error_busy` and does not start a command. It is not without effect: at
`:153-154` `freeze_i` and `trigger` are taken from the pushed byte before the
state is tested at `:155`, and `cmd_irq_en` is taken at `:161`, outside the
`else`. The command pointer is not rewound either. `get_more_data()` takes two
out-parameters, both replies. The firmware reads the command length only for a
new command.

So under the protocol as specified and the firmware as written, a payload larger
than one command is assembled from several complete transactions, each carrying
its own framing.

**The hardware does not enforce this, and the distinction is worth stating
plainly rather than leaving for a reviewer to find.** `do_write`
(`command_protocol.vhd:121`) is gated on `slot_base`/`enabled`, not on the state
machine's state, and the pointer increment at `:145-147` sits inside the same
guard. `command_intf.cc:169` writes `HANDSHAKE_ACCEPT_COMMAND`, which rewinds
the pointer (`command_protocol.vhd:210-213`), before `copy_result`
(`command_intf.cc:173`) sets the data state. A client could therefore write
chunk bytes to `$DF1D` — the command register at the default slot base, which
`slot_base` (`command_protocol.vhd:201-202`) can relocate — write `DATA_ACC`,
and the firmware would wake in `get_more_data` with those bytes and a valid
length in the command buffer. No VHDL change would be
required, and no firmware signature change either.

That route has one precondition the client cannot meet by itself. `DATA_ACC` is
read only in a data state: `:163` tests `state(1)='1'` before anything happens,
and `:164` raises `CMD_DATA_ACCEPTED` from `state(0)`, so the firmware is woken
only from state `11`, Data More. From state `10`, Data Last, the same write
clears `state(1)` and posts nothing, and the firmware never wakes. A client can
take this route only after the Ultimate has replied with
`data->last_part == false`, which is the target's choice and not the client's.

That route is rejected here for two reasons, neither of which is that it is
impossible. It parks the command interface in a data state for the duration of
the transfer, so the machine is not available for anything else while a payload
is being assembled; and a transfer abandoned part-way leaves the state machine
where it was, requiring an explicit abort or a reset to clear, with nothing in
the firmware that reads `error_busy` to notice. The transaction-per-chunk shape
costs seven header bytes per chunk and leaves the interface in Idle between
chunks.

The shape is also not new. `software/6502/fc3_wedge.tas`, the FC3 kernal wedge in
this repository, already writes to the SoftIEC target this way: it accumulates
bytes into the command buffer, and every 256 bytes it executes the command,
acknowledges, and starts a fresh one with a new header.

## Appendix B — Why the offset is on the wire

*Not part of the specification.*

The alternative is for the target to keep a running count and for each chunk to
carry only the total. That is simpler, and it fails silently.

### The case that does not depend on any implementation choice

A C64 reset does not reset the command interface logic. With an implicit
accumulator, a payload left in progress by a reset is silently continued by the
next program's chunks, and the handle does not save you: `c64_reset()`
(`network_target.cc:542-553`) closes every socket this target holds (`:552`), so
those descriptor numbers are free to be handed out again to the next program.

That `lwip` reissues the *low* descriptors is a property of its allocator:
`alloc_socket` (`software/lwip/src/api/sockets.c:547`) scans
`for (i = 0; i < NUM_SOCKETS; ++i)` and returns the first free slot, so the
lowest free descriptor is always the one handed out. `LWIP_SOCKET_OFFSET` is 0
(`software/lwip/src/include/lwip/opt.h:2058`, not overridden in
`software/network/config/lwipopts.h`) and `MEMP_NUM_NETCONN` is 16
(`software/network/config/lwipopts.h:255`), so the descriptors a program sees are
0 to 15 — the same small numbers, in the same order, for the program that runs
next.

Safety then rests entirely on the firmware remembering to discard the accumulator
in `c64_reset()` — which is precisely the class of defect that #808 and #814
addressed.

With the offset on the wire, the next program's first chunk carries offset zero
and restarts unconditionally. A stale payload cannot be silently completed
whether or not the firmware remembers. The protocol closes the hole instead of
the implementation.

### The second case, which does depend on one

A payload in progress is discarded by any other command to this target, and
`NET_CMD_READ_SOCKET` is such a command. **That rule is an implementation choice,
not something the transport forces.** It exists because `buffer[NET_CMD_BUFSIZE]`
(`network_target.h:70`) is shared three ways — the read payload, the
`gethostbyname_r` scratch (`network_target.cc:219`), and the write accumulation —
and the firmware says so at `network_target.cc:56-58`: *"That is what keeps the
accumulation out of the way of `buffer`'s other users…"* A second buffer would
remove the rule at the cost of `NET_CMD_BUFSIZE` bytes of RAM.

It is stated normatively in §2.2 because it is the behaviour of the reference
implementation and a client must be able to rely on it. But a reader who asks
"why can I not read a socket while assembling a write?" — the ordinary shape of a
program that both sends and receives — deserves the honest answer, which is that
the buffer is shared, not that the interface requires it.

Given the rule, a client that reads a socket between two chunks meets this:

```
chunk(handle, total=1000, 890 bytes)   00,OK, no response
READ_SOCKET                            payload discarded, silently
chunk(handle, total=1000, 110 bytes)   opens a NEW payload. 00,OK. Nothing sent.
chunk(handle, total=1000, 890 bytes)   completes: sends 1000 bytes, being 110 of
                                       the abandoned payload followed by 890 of
                                       the new one
```

Every command answers `00,OK`, and the datagram that leaves is wrong. A chunk
that does not complete a payload responds identically whether it opened one or
continued one, so a client cannot tell which happened.

With the offset on the wire, the chunk after the discard names a position nothing
is holding and is refused. The failure is reported at the command that caused it
rather than in the content of a later datagram.

### What the offset does not give you

It is not a retry mechanism. A repeated chunk that does not continue the payload
in progress is answered `81,INVALID PARAMS` **and discards the payload**; a
repeated chunk with an offset of zero silently restarts. A client recovers only
by restarting the payload at an offset of zero, and the specification does not
offer a way to resend a single lost chunk.

What it does rule out is the serious form of the same failure. A chunk that
carries more than the interface will take is truncated silently, and with the
offset on the wire that truncation can never produce a *complete* payload with
wrong bytes in it: the accumulation is left short of what the client believes,
so either the next chunk names an offset that does not match and is refused, or
the payload never completes. A datagram that leaves is a datagram every byte of
which some chunk placed.

The cost is two bytes per chunk, reducing the largest chunk from 890 bytes to
888.

## Appendix C — Command number, and a proposal about numbering

*Not part of the specification.*

`0x12` is the next number above those in use in this firmware, and should not be
used. `xlar54/ultimateii-dos-lib`, at commit
[`3a38e690`](https://github.com/xlar54/ultimateii-dos-lib/blob/3a38e690eaa5893d6cee4caaae0435677da7b945/src/lib/ultimate_lib.h#L76-L79),
defines `NET_CMD_TCP_LISTENER_START` `0x12`, `…_STOP` `0x13`,
`GET_LISTENER_STATE` `0x14` and `GET_LISTENER_SOCKET` `0x15` at
`src/lib/ultimate_lib.h:76-79`, and `TARGET_NETWORK` `0x03` at `:33`. The library
ships `src/samples/u-echoserver.c`, whose `uii_tcplistenstart()` calls
`uii_settarget(TARGET_NETWORK)` and then `uii_sendcommand()`, which writes the
target into the command's first byte; `$03 $12` goes on the wire. (`build.sh`
builds that sample into `u-echoserver.prg`.)

Those numbers are spoken for by programs in the field whether or not this
firmware implements them. Taking `0x12` here would mean that if TCP listeners are
ever added they cannot have the numbers existing programs already send. This
document therefore proposes `0x16`.

That collision was avoidable, and the general case is worth addressing. The
published specification for this target stops at `0x11` and says nothing about
what lies above it — not reserved, not free, absent. Three commands across two
targets (`NET_CMD_SET_INTERFACE` 0x03, `CTRL_CMD_READ_RTC` 0x02,
`CTRL_CMD_ENCODE_TRACK` 0x12) are defined in headers with no implementation, and
answer `21,UNKNOWN COMMAND` exactly as an unassigned number does. A client
cannot distinguish "not yet implemented", "withdrawn" and "never existed".

Two things would prevent a repeat, and neither requires a firmware change:

1. The target documents state which numbers are assigned, including to commands
   that are not implemented, and which ranges are reserved.
2. A number known to be used by a published third-party library is treated as
   assigned.

## Appendix D — What was measured, and what was not

*Not part of the specification.*

A spike implementing this document was built on upstream `test-merge` at
`d33b7802`, as `30a88b4d` (the tests, red) and `8c922633` (the firmware,
green), and run on both the REST and the native (6502 agent) routes. The spike
has been rebased four times: from `d2acbd4e` to `ed8bc28d`, from there to
`d33b7802`, from there to `883f608d`, and from there to `ac2fe909`, where the
branch now sits as `cedea616` (the tests) and `e278828b` (the firmware). Of the
files this document cites, only `software/network/config/lwipopts.h` changed
across any of those moves, and its edits are at `:766` and `:1011`, below every
line cited here. Neither the third move nor the fourth changed any file cited
here or any file this branch touches. The fourth was taken to clear a CI
failure that was not this branch's: upstream's own builds were red at
`883f608d` and for three commits around it, on a U2+L ECP5 place-and-route
check, and `8806a431` fixed it.

The bench machine, as `/v1/info` reports it:

| | |
| --- | --- |
| Machine | Ultimate 64 Elite, unique id `601A96` |
| Firmware | 3.15 |
| FPGA | 124 for the `ed8bc28d` and `d33b7802` runs, 125 for the `883f608d` run |
| Core | 1.4F |

The suite registers 20 scenarios and runs each on both routes, 40 combinations.
Ten of the scenarios, 20 combinations, are of this command. It is
`tests/e2e/io/command_interface/net_target_test.py`, registered in the **deep**
profile, so `./run-tests` at its default profile does not select it; these
figures come from `./run-tests --profile deep -s uci-net-target`.

| Build | Result |
| --- | --- |
| baseline, `ed8bc28d` | 20 of 20 chunked combinations fail, every one citing `21,UNKNOWN COMMAND`; the 20 pre-existing combinations pass |
| with the spike, `d33b7802` | 40 of 40 combinations pass, 197 checks |
| with the spike, `883f608d` | 40 of 40 combinations pass, 197 checks — identical |

The branch now sits on `ac2fe909`, two commits later, and has not been
re-measured there; those two touch `u2p_ecp5.lpf`, `routes.cc` and the two
OpenAPI documents, none of which this branch or this document reaches.

The baseline run took 937 s, the run with the spike 1323 s, and the
`883f608d` run 1085 s on each of the two UI transports the deep profile sweeps.

The two rows come from different bases, and that is worth stating rather than
leaving for a reader to notice. The baseline row was measured at `ed8bc28d`; the
spike row at `d33b7802`, seven upstream commits later, after the branch was
rebased. The baseline row is a claim about the *absence* of this command, and no
commit between the two adds it — `4d4f6a72` adds four commands in that window,
at `0x51`-`0x54` on the control target. The spike row, which is the claim about
what this command *does*, was measured twice: at `d33b7802`, and again after the
rebase at `883f608d`. Both give 40 of 40 and the same 197 checks. That agreement
is worth more than a repeat, because `883f608d` rebuilds the U64 bitstream and
the bench moved from FPGA 124 to 125 between the two runs, so the second is the
same result on different gateware rather than the same run twice.

What these figures do not cover is worth stating as plainly. They are a claim
of parity with the previous image, not a general clearance of `883f608d`: they
cover what this one suite covers, and nothing the default profile skips beyond
it.

> Nineteen of the baseline's twenty pre-existing passes come from that run. The
> twentieth, `reset-closes-uci-sockets` on the native route, did not:
> `tools/64tass/64tass` was replaced with the tracked Linux binary while the
> suite was running, so the native route could not assemble its 6502 agent and
> the scenario ended in "Exec format error" rather than a result. It was re-run
> on its own, against the same unpatched firmware, and passed with 32 checks.

Those ten scenarios are:

- `chunked-write-arrives-as-one-datagram` — a payload larger than one command
  arrives as a single datagram, byte-exact. A payload small enough for one chunk
  is a check inside this scenario, against what `NET_CMD_WRITE_SOCKET` does.
- `chunked-write-full-size-datagram` — the largest payload this command may
  announce, assembled and sent: 1472 bytes carried as 888 + 584, arriving as one
  datagram of 1472, the completing chunk replying `c0 05`.
- `chunked-write-zero-total` — a total of zero opens and completes in one
  command: a plain `NET_CMD_WRITE_SOCKET` with no data bytes and a chunk
  announcing a total of zero both answer `00,OK` with a count of zero, and both
  put `datagrams [0]` on the wire.
- `chunked-write-total-ceiling` — 1472 accepted as an announcement, 1473
  refused.
- `chunked-write-refuses-unowned-socket` — a payload completed for a handle no
  `OPEN` returned is refused, and nothing goes on the wire.
- `chunked-write-refuses-bad-chunks` — a chunk that contradicts the offset, the
  total or the handle in progress, or that overruns the total, is refused, and
  nothing goes on the wire.
- `chunked-write-refuses-offset-ahead` — a chunk placed past the accumulation
  point is refused rather than sending a hole.
- `chunked-write-refuses-short-commands` — a command too short to hold the
  seven-byte header is refused: three bytes and six bytes both answered
  `81,INVALID PARAMS` with nothing sent, exactly seven taken, the payload in
  progress discarded, and a fresh payload assembling afterwards.
- `chunked-write-discarded-by-abort` — a payload in progress does not survive an
  abort.
- `chunked-write-discarded-by-reset` — a payload in progress does not survive a
  C64 reset.

The oracles for the scenarios that the simulator could differentiate were checked
against a simulator of this contract with one defence removed at a time, so that
each of those scenarios fails for the defect it names.

### Byte weight

Measured at `ed8bc28d`, baseline against the spike, with `make u2_rv_swonly`.
The same comparison at `d33b7802` gives the same figure on the U64 image, +524
bytes, so the cost is a property of the change rather than of the base:

| | baseline | with the spike |
| --- | --- | --- |
| `ultimate.bin` | 760,952 bytes | 761,456 bytes |
| U2 application image | 743.1 KiB | 743.6 KiB |
| Free in the 792 KiB partition | 48.9 KiB, 6.2% | 48.4 KiB, 6.1% |

**+504 bytes.**

### Verified by an independent client

The runs above are this document's own suite. The `c64-wireguard` project has
since implemented this command in its own adapter and run it on the same bench,
which is a second client on unrelated code.

Their echo sweep passed 8 of 8: payloads of 888, 889, 891, 892, 893, 1452 and
1472 bytes each left as exactly one wire datagram of the right length, and 1473
was refused before it reached the device. Their observed byte counts, `0x37D`
and `0x5C0`, match what this command reported.

They also asserted §2.1 from a 6510-side debug-bus trace: a part that does not
complete a payload leaves the response count at zero and the response buffer
untouched, answering `00,OK`, and only the completing part writes the 2-byte
total. That is this document's Response paragraph, confirmed from outside.

Through their tunnel, at 48 MHz with the peer left at its default 1420-byte MTU
and no per-peer configuration at either end, a standard-MTU run passed 60 of 60
checks, twice. Outbound datagrams of 888 to 1472 bytes each left as one datagram
and decrypted at the peer; inbound datagrams of 892, 893, 1452 and 1472 bytes
were received whole.

Against a production peer — Cloudflare WARP's WireGuard edge, a conformant
responder enforcing TAI64N — the same build completed a handshake in 48.5 s and
again in 48.4 s, answered an ICMP echo through the tunnel, and received a
1278-byte DNS response whole, which is a multi-block `NET_CMD_READ_SOCKET` over
a real internet path. Inbound sizes above 1280 bytes could not be exercised
through that peer: WARP's own profile sets a 1280-byte MTU, which is a limit of
the peer rather than of this command.

That work is merged (`c64-wireguard` PR #112), so the first consumer of this
command has shipped support for it rather than holding a branch.

### What these runs do not establish

Four of the properties above are not demonstrated by either run, and are stated
here rather than left for a reader to discover.

- **One send is not observable; one datagram is.** The bench can see that a
  completed payload leaves as a single datagram, and only on UDP. It cannot see
  how many send calls produced it. On a stream socket the same defect is
  invisible once the stream reassembles, which is why §2.1 specifies the
  datagram property and not the call.
- **The 888-byte chunk ceiling cannot be probed.** An over-long chunk is not
  refused on the chunk that carries it — the command interface drops the 896th
  byte — so a firmware that accepted 889 bytes and one that truncated at 888 are
  indistinguishable from outside. §2.2 states the limit as unenforced for this
  reason.
- **The unowned-socket scenario does not show that ownership is consulted.** It
  uses handle `0x7F`, which is outside `lwip`'s descriptor range altogether:
  `LWIP_SOCKET_OFFSET` is 0 (`software/lwip/src/include/lwip/opt.h:2058`) and
  `NUM_SOCKETS` is `MEMP_NUM_NETCONN`
  (`software/lwip/src/include/lwip/priv/sockets_priv.h:52`), which is 16
  (`software/network/config/lwipopts.h:255`), so descriptors run 0 to 15. A
  firmware with no ownership tracking at all would answer `12,SEND ERROR: 9` for
  that handle too, straight out of `lwip`'s `EBADF`. The scenario shows that the
  chunked path refuses a handle nothing opened and sends nothing; it does not
  show that `owns_socket()` is consulted for a handle naming a live socket that
  belongs to something else.
- **The full-size scenario cannot fail on this tree for the reason it exists.**
  It is there to catch a firmware whose buffer is smaller than the total it
  accepts, and on this tree that is a compile error: `network_target.h:63` guards
  `#if NET_MAX_SOCKET_WRITE > NET_CMD_BUFSIZE`. Its value here is as a standing
  regression guard on both constants and on assembly at the full size.

## Appendix E — What this does not address

*Not part of the specification.*

**The 895-byte command ceiling.** `command_length` is `command_pointer`, stopped
at `c_cmd_if_command_buffer_end` (`command_protocol.vhd:95`, `:145-147`), so the
largest length the transport can report is 895 and the 896th byte written is
never counted. `doc/command interface.docx` §2.5 says, verbatim:

> The sizes of these queues are important to note, since they define the maximum
> transfer size per command. The command queue size is 896 bytes ($380), the
> response data queue is also 896 bytes ($380), and the status queue is 256 bytes
> ($100).

The queue is 896 bytes; the maximum transfer size per command is 895. The
document is wrong by one. The same sentence appears in
`doc/Command Interface V1.1.pdf`, so it is not an artefact of a stale source, and
a client that computes chunk sizes from it will offer one byte more than the
interface will carry.

**Datagrams above 1472 bytes.** `IP_REASSEMBLY` and `IP_FRAG` are both 0
(`software/network/config/lwipopts.h:343` and `:350`), so the device can neither
send nor receive a datagram that requires IP fragmentation. This is the origin of
the 1472 in §2.1. It is a build-time configuration of this firmware rather than a
property of the hardware, and it does not apply to stream sockets, on which the
same 1472 limit is imposed by this command for uniformity rather than by any
transport constraint.

**Stream sockets.** On a stream socket the boundary this command preserves does
not exist: repeated `NET_CMD_WRITE_SOCKET` already produces one contiguous
stream. The command is accepted on any socket for uniformity, and the 1472 limit
applies to both, but it has no purpose there — and §2.1's datagram requirement is
not falsifiable on a stream socket, so this command carries no testable framing
guarantee for one.

**Recovery of a partly assembled command.** A client that writes command bytes
and then abandons them cannot discard them: nothing on the C64 side rewinds the
command pointer, so the bytes prepend themselves to the next command and the
first of them is read as the target byte. This is a property of the interface
rather than of this command, and `fc3_wedge.tas` works around it by pushing an
empty command to make the Ultimate flush the buffer. It deserves a sentence in
the Register API document.
