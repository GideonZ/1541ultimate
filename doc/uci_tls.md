# TLS sockets on the UCI network target

Commands `$21`-`$25` of network target 3 open TLS client connections. The WiFi module (ESP32) runs the TLS
sessions. The Ultimate keeps the TCP socket and carries the ciphertext between it and the module, so the C64 only ever
sees plaintext. Implementation: `software/io/network/network_target_tls.cc` (Ultimate side),
`software/wifi/tls/tls_engine.c` and `tls_rpc.c` (module side; the directory is shared by module firmware).

## Conventions

- Byte 0 of a command is the target (`$03`), byte 1 the command. A command is at most 895 bytes.
- Multi-byte integers in commands and replies are little endian, the IANA values too: TLS 1.3 (`0x0304`) travels as
  `04 03`.
- Hash bytes (the pin, the SPKI hash) are in digest order, as `openssl dgst` prints them.
- A reply is data (possibly empty) plus one status string `NN,TEXT`. Hex in status texts is upper case and zero
  padded: mbedTLS codes `-0x%04X`, flag words `0x%08X`, versions `0x%04X`.
- A TLS handle is an lwIP descriptor number, in the same space as TCP and UDP handles.
- `$21` and `$24` with the no-reply bit (`$80` in byte 0) are refused with 81 and open nothing. (A build without TLS
  answers them from their layout alone, 81 or 90.)
- TLS is built for the Ultimate 64 (Nios II) only. Every other build has the commands without TLS, see
  [Builds without TLS](#builds-without-tls).

## Commands

### `$21` OPEN_TLS

```
[2..3] port (LE)
[4]    trust: 0 BUNDLE, 1 CUSTOM, 2 PIN
[5]    flags: bit 0 REQUIRE_TLS13; bits 1-7 reserved (a set bit answers 82)
[6..37] pin, 32 bytes, only when trust = 2 (all zero answers 82)
tail   host [ $00 [ name [ $00 ] ] ]
```

`host` is 1..253 bytes without `$00` (a DNS name or an IPv4 literal). `name` is 1..253 bytes without `$00`: the server
name for SNI and certificate checking. `host`, or `host $00`, gives no explicit name. Anything else in the tail
(empty host, `host $00 $00`, `$00` inside a name, bytes after the name's `$00`, a host or name over 253 bytes)
answers 81.

The effective server name is the explicit name; else the host unless it is an IPv4 literal (anything lwIP's
`ip4addr_aton` accepts); else none.

| Trust | Effective name | Server name sent and checked |
|---|---|---|
| BUNDLE, CUSTOM | must exist and must not be an IPv4 literal, else 81 | SNI sent, certificate must carry the name |
| PIN | any; an IPv4 literal counts as none | SNI sent for a DNS name, name not checked; none: no SNI |

Success reply, 8 bytes, then `00,OK`:

| Byte | Content |
|---|---|
| 0 | handle |
| 1..2 | negotiated version (`04 03` TLS 1.3, `03 03` TLS 1.2) |
| 3..4 | cipher suite (`01 13` TLS_AES_128_GCM_SHA256) |
| 5..6 | key exchange group (`1D 00` x25519, `17 00` secp256r1); never 0 |
| 7 | bit 0 extended master secret used (1 on TLS 1.2, 0 on 1.3); other bits 0 |

A failure replies with no data and one status, and leaves nothing open. Refusals are applied in this order, and
nothing touches the network before step 5:

1. Layout, lengths and tail grammar: 81. Then the name rule for BUNDLE/CUSTOM: 81. Then trust > 2, reserved flag bits,
   all-zero pin: 82.
2. Module not available (no module, WiFi off, module image without TLS): 90.
3. No free table entry: 91.
4. CUSTOM and no CA loaded: 97 `NO CUSTOM CA`.
5. The module's open: 17, 87, 90, 92 or 97 (see the status table).
6. DNS: 84, or 17 if a C64 reset cut the wait short.
7. Socket and connect: 85, 11; 17 if a C64 reset cut the wait short; 86 if lwIP hands out a descriptor above 255.
8. Handshake: 13-19, 93-96.

### `$22` TLS_TRUST_CA

```
[2..3] offset (LE)   [4..5] total (LE)   [6..] DER bytes of this chunk (0..889)
```

Uploads the one custom CA that trust mode CUSTOM uses. At most one is held, not stored in flash, and cleared by a
C64 reset or a module restart.

| Command | Result |
|---|---|
| offset 0, total 1..1472 | starts an upload; any installed CA is dropped at once |
| offset 0, total 0, no data | clears the CA, `00,OK` |
| offset 0, total 0, with data | 81 |
| offset 0, total > 1472 | 82, no CA |
| offset > 0 | must continue the pending upload (offset = bytes so far, same total, not past total), else 81 and the upload is dropped. With no upload pending: 81, an installed CA untouched |
| intermediate chunk | empty reply, `00,OK` |
| final chunk | the module parses it: `00,OK` and the CA is LOADED, or one of the 97 texts below, or 90 / 17; the CA is then EMPTY |

Any other network target command while an upload is pending cancels it. An ABORT does not.

### `$23` TLS_INFO

`[2]` handle. The command must be exactly 3 bytes, else 81.

Handle `$FF`, the capability record, 16 bytes, `00,OK`. It makes one module STATS call, waited for at most 1 s:

| Byte | Content |
|---|---|
| 0 | interface version, 1 |
| 1 | maximum sessions, 2 |
| 2 | table entries in use, including failed ones not yet closed |
| 3 | trust modes: bit 0 BUNDLE, bit 1 CUSTOM, bit 2 PIN (always `07`) |
| 4 | ready: bit 0 the module answered a STATS (kept while the module is merely busy, cleared when it stops, has no TLS or restarts); bit 1 entropy (radio started, never set without bit 0); bit 2 trusted time (SNTP time not before 2025-01-01, or the module's clock while bit 0); bit 3 custom CA LOADED |
| 5 | versions: bit 0 TLS 1.2, bit 1 TLS 1.3 (always `03`) |
| 6 | module major version (0 with no module) |
| 7..8 | module minor version (LE). 0 while no module application is detected, and on builds without TLS |
| 9..11 | `FF FF FF` while bit 0 of byte 4 is set (a bundle is present, the date is unknown; not a date), else 0 |
| 12..13 | the longest the module's dispatcher was blocked since boot, ms (LE); 0 while bit 0 is clear |
| 14 | entries not yet claimed (0 or 1; what `$25` would close) |
| 15 | 0 |

Bytes 3 and 5 do not say whether a device has TLS: test bit 0 of byte 4. A device where it never sets has none.

A TLS handle, 48 bytes, `00,OK`. A handle that failed after its handshake still reports the handshake's values:

| Byte | Content |
|---|---|
| 0..1 | TLS version (LE) |
| 2..3 | cipher suite (LE) |
| 4..5 | group (LE) |
| 6 | trust mode used |
| 7 | the same bits as byte 7 of the open reply |
| 8..39 | SHA-256 of the leaf certificate's SubjectPublicKeyInfo |
| 40..43 | handshake time, ms (LE) |
| 44..47 | 0 |

Any other handle: no data, 81.

### `$24` START_TLS

Starts TLS on a TCP socket this target already owns and has connected.

```
[2]    handle
[3]    trust        [4] flags        (as $21 [4] and [5])
[5..36] pin, only when trust = 2
tail   [ name ] [ $00 ]       name 1..253 bytes without $00; a lone $00 means no name
```

BUNDLE and CUSTOM need a name that is not an IPv4 literal (else 81). PIN with no name sends no SNI. The handle must
be an owned, not yet TLS, stream socket: the check comes first and a UDP handle, a TLS handle or an unowned number
answers 81 with the socket untouched. The TLS layer starts at the socket's current read position. Success replies like
`$21`, with the same handle.

On failure the code decides what the handle is:

| Codes | Handle afterwards |
|---|---|
| 81, 82, 87, 90, 91, 92, 97 | unchanged: still an open TCP handle, nothing was sent |
| every other code (11-19, 93-96, 17, 86) | gone: closed and untracked; do not close it |

### `$25` TLS_RELEASE

Exactly 2 bytes, else 81. Closes every TLS entry that is still unclaimed (see below) and replies one byte, the number
closed (0 or 1), `00,OK`. The no-reply bit is allowed. It is for one use only: as the next network target command
after an `$21` whose reply the program did not read, so that a session the abandoned open may have created is closed.

## Sessions after an ABORT

An ABORT never closes or frees a session. Every network target command except `$25` and `$23 $FF` (exactly 3 bytes)
marks all open entries as claimed, so only the entry the most recent `$21`/`$24` created can be unclaimed, and only
until the next command.

- After an ABORTed `$21`: treat any handle from it as gone and send `$25` next. `$23 $FF` is allowed in between.
- After an ABORTed `$24`: send CLOSE on the handle. It answers `12,ERROR ON CLOSE: 9` if the handle was already gone,
  which is harmless if nothing was opened in between.
- After an ABORT during a READ, or before all of a READ reply was read, plaintext is lost from the stream without an
  error: CLOSE the handle.
- After an ABORTed WRITE: it was carried out in full, or the handle is failed. Do not resend it.
- A program that exits without a C64 reset leaves its sessions behind, claimed. The next program finds them with
  `$23 $FF` (byte 2 is the count) and `$23 h` for each handle, and CLOSEs those that answer `00,OK`. A C64 reset
  closes everything.

## Existing commands on a TLS handle

| Command | Behaviour |
|---|---|
| `$10` READ_SOCKET `[2]h [3..4]maxlen` | maxlen 1..1472 (else 82). Reply: 2-byte count, then that much plaintext, in blocks as for TCP. Plaintext already received is returned first; otherwise the target steps the session until plaintext arrives, the stream ends, or the read budget is spent. See the result table. |
| `$11` WRITE_SOCKET `[2]h [3..]data` | 0..892 bytes of plaintext. All or nothing: header `n` = bytes taken, then `00,OK`. 0 bytes: header 0, `00,OK`, nothing sent. A failure answers header `$FFFF`. Sent as TLS records; the plaintext never reaches the TCP socket. |
| `$16` WRITE_SOCKET_CHUNK `[2]h [3..4]offset [5..6]total [7..]data` | Where the firmware has it. The chunks are collected as for a plain socket (total at most 1472); the intermediate chunks answer an empty reply and `00,OK`, and the final chunk is written as one TLS write exactly as `$11` would, with header = total. |
| `$09` CLOSE_SOCKET `[2]h` | Sends close_notify (best effort, 100 ms) if the session is live, then closes the TCP socket. Always `00,OK`, failed or not. No module call if the session is already gone. |

A pending chunk of `$16` is dropped by any other command, as for plain sockets.

### READ and WRITE results

| Situation | READ header | WRITE header | Status | Handle afterwards |
|---|---|---|---|---|
| Data | n | n | `00,OK` | open |
| Nothing yet | `$FFFF` | | `02,NO DATA: 11` | open |
| close_notify from the peer, all delivered | `$0000` | | `01,CONNECTION CLOSED BY HOST` | gone |
| TCP end without close_notify, all delivered | `$0000` | | `05,TLS CLOSED WITHOUT NOTIFY` | gone |
| Fatal alert from the peer | `$0000` | `$FFFF` | `14,TLS ALERT RECEIVED: <n>` | failed, owned until CLOSE |
| Record or decode failure | `$0000` | `$FFFF` | `16,TLS SESSION FAILED: -0xNNNN` | failed |
| Module timeout, restart, bad reply, WRITE stall | `$0000` | `$FFFF` | `17,TLS MODULE NOT RESPONDING` | failed |
| TCP send failed | `$0000` (later READs) | `$FFFF` | `12,SEND ERROR: <errno>` | failed |
| TCP receive error (peer RST) | `$0000` | `$FFFF` | `12,RECEIVE ERROR: <errno>` | failed (see below) |

- A receive error fails the handle only when a READ ends in it, after all received plaintext was delivered. A WRITE
  that meets it answers `$FFFF` and the same 12 but sends nothing; the READs go on and end in that 12.
- `$0000` on READ means the stream is over. After 01 and 05 the handle is gone: do not CLOSE it, because the number
  may already belong to a newer socket. After 12, 14, 16 and 17 the handle is still owned: CLOSE it.
- `$FFFF` with `02,NO DATA: 9` means the number is not the program's handle any more; stop polling.
- 01, 05 and `12,RECEIVE ERROR` come only after every authenticated plaintext byte has been delivered. When a failure
  (12 SEND ERROR, 14, 16, 17) is recorded, plaintext received but not yet read is discarded.
- A WRITE ends only when the module has taken all the plaintext and has no more ciphertext to send. If it cannot
  within 4 module steps or 10 s, the handle fails with 17.
- Before a WRITE, READ until `$FFFF` with `02,NO DATA: 11` or `$0000`. If a WRITE still answers `$FFFF` with a `12`
  other than `RECEIVE ERROR`, CLOSE the handle at once.

## Status codes

| Code | Text | From | Meaning |
|---|---|---|---|
| 01 | CONNECTION CLOSED BY HOST | READ | clean close; handle gone |
| 05 | TLS CLOSED WITHOUT NOTIFY | READ | TCP end without close_notify, possible truncation; handle gone |
| 11 | ERROR ON CONNECT: errno | OPEN | as OPEN_TCP, including the connect timeout |
| 12 | SEND ERROR: errno, RECEIVE ERROR: errno | READ, WRITE | failed handle; never from `$21` or `$24` |
| 13 | TLS HANDSHAKE FAILED: -0xNNNN, or `: NO EMS`, `: PEER CLOSED`, `: PEER RESET: errno`, `: SEND ERROR: errno` | OPEN | a handshake failure that is none of 14, 15, 18, 19, 93-96 |
| 14 | TLS ALERT RECEIVED: n | OPEN, READ, WRITE | fatal alert n from the server; 70 is protocol_version |
| 15 | TLS HANDSHAKE TIMEOUT | OPEN | no established session in 15 s, or the 40 s open budget ran out in the handshake |
| 16 | TLS SESSION FAILED: -0xNNNN | READ, WRITE | bad record MAC, decode error, too many empty records, KeyUpdate |
| 17 | TLS MODULE NOT RESPONDING | any | module timeout, full queue, malformed reply, restart or WRITE stall |
| 18 | TLS VERSION REFUSED: 0xNNNN | OPEN | the ServerHello chose a version not offered (TLS 1.2 under REQUIRE_TLS13, or below 1.2) |
| 19 | TLS DOWNGRADE DETECTED | OPEN | a TLS 1.2 ServerHello carrying the RFC 8446 4.1.3 downgrade sentinel |
| 81 | INVALID PARAMS | all | layout, lengths, tail grammar, name rule, not a TLS handle for `$23`, bad handle for `$24`, `$22` sequence error, no-reply bit on `$21`/`$24` |
| 82 | PARAMETER(S) OUT OF RANGE | all | trust > 2, reserved flag bit, all-zero pin, READ maxlen 0 or > 1472, CA total > 1472 |
| 84 | UNRESOLVED HOST | OPEN | as OPEN_TCP, also a 10 s DNS timeout |
| 85 | ERROR OPENING SOCKET | OPEN | as OPEN_TCP |
| 86 | INTERNAL ERROR | any | |
| 87 | NO ENTROPY SOURCE | OPEN, START_TLS | the radio has not started, so there is no random seed |
| 90 | TLS NOT AVAILABLE | OPEN, START_TLS, TRUST | no module, WiFi disabled, a module image without TLS, or a build without TLS |
| 91 | TLS SESSIONS IN USE | OPEN, START_TLS | both table entries are used, failed ones included; none is ever evicted |
| 92 | NO TRUSTED TIME | OPEN, START_TLS | BUNDLE and CUSTOM need a trusted clock; PIN does not |
| 93 | CERTIFICATE NOT TRUSTED: 0xNNNNNNNN | OPEN | chain not trusted, bad signature, weak key or hash, bad key usage; `0xFFFFFFFF` when verification did not complete |
| 94 | CERTIFICATE NAME MISMATCH: 0xNNNNNNNN | OPEN | the certificate is not for the server name |
| 95 | CERTIFICATE EXPIRED: 0xNNNNNNNN | OPEN | expired or not yet valid by the trusted clock |
| 96 | PINNED KEY MISMATCH | OPEN | PIN only: the leaf key is not the pinned key |
| 97 | CUSTOM CA INVALID: -0xNNNN, CUSTOM CA REJECTED: RSA KEY < 2048, CURVE or NOT A CA, NO CUSTOM CA | TRUST, OPEN | the upload did not parse or was refused when uploaded, or CUSTOM without a loaded CA the module holds |

Where a chain is both untrusted and has the wrong name, the answer is 94. Only the first two digits are the code: a
client matches on those.

## Trust modes and flags

| Trust | Verifies against |
|---|---|
| 0 BUNDLE | the Mozilla root store built into the module (`software/u64ctrl/ca/cacert.pem`, the list the control module trusts too) |
| 1 CUSTOM | the one CA uploaded with `$22`, and nothing else |
| 2 PIN | the SHA-256 of the server leaf's SubjectPublicKeyInfo. The certificate chain and name are not checked; key usage and extended key usage still are |

There is no mode without verification. BUNDLE and CUSTOM need a trusted time, so 92 until the SNTP clock is set.

| Flag | Meaning |
|---|---|
| bit 0 REQUIRE_TLS13 | offer TLS 1.3 only. Clear: offer TLS 1.3 and hardened TLS 1.2 |
| bits 1-7 | reserved, answer 82 |

Policy of the module's client, for both versions:

- TLS 1.2 suites: ECDHE-ECDSA and ECDHE-RSA with AES-128-GCM, AES-256-GCM or ChaCha20-Poly1305 (`C02B C02C C02F C030
  CCA9 CCA8`). TLS 1.3 suites `1301 1302 1303`. No CBC, static key exchange, finite-field DH, PSK or SHA-1/MD5
  signatures.
- Key exchange groups, in order: x25519, secp256r1, secp384r1, secp521r1.
- TLS 1.2 requires the extended master secret (else `13,...: NO EMS`). No renegotiation, no session tickets or
  resumption.
- A ServerHello without `supported_versions` is 18 under REQUIRE_TLS13; in default mode it is 19 only when its random
  ends in the downgrade sentinel, else 13. A protocol_version alert is always 14 with 70.

## Timing

| Stage | Limit |
|---|---|
| module open | 5 s |
| DNS (inside `$21` only) | 10 s |
| TCP connect | 10 s |
| handshake | 15 s |
| `$21` or `$24` in total | 40 s; the command can take about 41 s before it answers |
| every other TLS command | 12 s at most |

A program waits at least 45 s for `$21` and `$24` and 12 s for the others before it treats the interface as wedged. Only
a C64 reset ends a command early.

## Builds without TLS

Builds other than the Ultimate 64 (the U2 family, the Ultimate 64-II) have the commands but no TLS:

- `$23 $FF` answers the 16-byte record with byte 4 = 0, byte 3 = `07` and byte 5 = `03`; byte 6..8 are 0.
- `$21` checks its layout (81) and otherwise answers 90, including trust > 2. `$24` answers 81 for a handle it does
  not own, else 90. `$22` answers 81 below 6 bytes, else 90 for every chunk, the clear included. `$25` answers one
  byte, 0, `00,OK`.
