# Restart identity and TLS provisioning

The optional provider remains inactive by default. The HTTP command interface is
unchanged. Building a TLS backend does not establish production readiness.

## Native restart identity

`DurableIncarnation` adapts a qualified journal to `IncarnationSource`. Its 128-bit
identity combines a provisioned 64-bit journal domain and a 64-bit durable epoch.
The journal must reserve and commit each epoch before acknowledging it, serialize
all callers and never reuse an epoch after power loss. Failed or ambiguous writes
disable the current instance; maximum epoch is usable once, then exhausted.

The domain must distinguish journals, including restored or cloned storage.
Deleting or rolling back the journal requires provisioning a new domain. Neither
MAC addresses, RTC values nor volatile counters meet this contract. This identity
provides uniqueness, not cryptographic entropy or peer authentication.

`FlashIncarnationJournal` implements the native Flash API using two explicitly
reserved 4 KiB configuration sectors and 256-byte pages. Configuration slots are
counted backwards from physical flash end, inside the advertised configuration
image. Unsupported geometry is rejected before writes. Provisioning requires
both entire sectors erased and a fresh externally supplied domain; it does not
erase existing settings. Slot reservation and provisioning must happen before
ConfigManager starts allocating configuration pages, with serialized access.

Both records must have valid format, CRC, domain and consecutive counters. Each
reservation erases the older sector, programs its successor and verifies readback
before returning an identity. An erased, torn or corrupt record disables the
journal rather than falling back to an older counter. A fully programmed write
whose acknowledgment was lost burns that epoch on reconstruction. This favors
uniqueness over availability after interrupted writes.

The native FPGA configuration disables its TRNG. Sector allocation and unique
domain installation remain explicit product work; no journal is automatically
installed. A settings reset may erase these records and must require a new domain.
Restoring or cloning two valid records cannot be detected by CRC. Host fault tests
do not establish physical durability, endurance or rollback protection.

## ESP32 trust, UTC and entropy

`provider_control_provision_tls` configures explicitly supplied roots and a
`TlsPolicy` before owner startup. Parsing must succeed for the entire bundle;
input is bounded to 64 KiB and live replacement is refused.

The opt-in `ULTIMATE_PROVIDER_SOURCES` configuration instead selects the full
certificate bundle in the pinned ESP-IDF 5.3.6 source. Its `cacrt_all.pem` SHA-256
is `1794c1d4f7055b7d02c2170337b61b48a2ef6c90d77e95444fd2596f4cac609f`.
Custom/deprecated root additions and changed bundle bytes fail the build. Roots
are initialized before owner startup and updated through a reviewed firmware
build, never downloaded at runtime. This does not establish secure firmware
delivery or rollback protection; those require product-level qualification.

`TlsPolicy` requires a bounded UTC qualification and a continuously qualified
entropy source. UTC is checked against a monotonic anchor, with an explicit
lifetime (at most one day) and tolerance (at most 60 seconds). Invalid input,
expiry and observed clock discontinuities revoke the qualification. Existing
SNTP completion and a plausible RTC date are not automatically trusted by this
generic policy.

The selected source adapter uses the firmware's configured ordinary SNTP server.
This time is unauthenticated: a network attacker can influence initial time and
certificate date checks. Jump bounds and freshness do not authenticate time.
The first sample must fall within 2020--2100; later samples must remain within
300 seconds of the monotonic projection and arrive within one day. A rejected
sample or failed system-clock write latches the source unavailable until boot.
Each accepted sample qualifies UTC for two hours with a five-second drift bound.
The immediate SNTP setter revokes existing sessions before changing UTC; smooth
slewing is not used. Mutex contention revokes readiness without changing UTC and
allows a later valid sample to recover. This policy is practical for the current
device, but applications requiring authenticated time need another source.

The product must serialize policy updates with bounded task-context critical
sections and notify entropy loss before disabling its qualified hardware source.
Each TLS session captures a generation. Clock updates and entropy revoke/restore
retire earlier generations even if readiness is restored between checks. A new
session may capture the new qualification after the old session is cleaned up.
Entropy is checked before and after generation; output is discarded on revocation.

The actual Wi-Fi start/stop call sites revoke entropy readiness before transitions
and grant it only after successful driver start. A stop event also revokes it.
Association alone never qualifies entropy. This follows the SDK requirement for
RF-backed randomness while Wi-Fi is enabled; it does not claim that emulator RNG
behavior proves the physical source. Unexpected hardware failures and radio power
management still need device qualification.

The callback configuration entry point remains for explicit integration fixtures.
Sources are optional and disabled by default, and do not activate the native HTTP
provider. The emulator's UTC and entropy assertions are fixture inputs, not
hardware qualification.

## Validation

`test_provider_provisioning` covers restart identity, ambiguous reservations,
replay, exhaustion, clock boundaries, rollback and concurrent policy changes.
The ESP32 production owner-task fixture exercises the platform policy callbacks,
trust parsing, generation revocation and RSA/ECDSA requests. Its renewal actor
reserves a 250 ms reply margin before the original request deadline and accounts
for a pending renewal before Close. Scheduling and cleanup limits are unchanged.

`test_provider_flash` exercises the actual storage adapter against NOR-like Flash
faults, corrupt records, geometry and reconstruction. `test_provider_sources`
compiles the actual source adapter with controlled SDK calls to verify ordering,
clock failure, initialization failure and radio lifecycle revocation. The embedded
source probe additionally verifies a pinned public root with the real SDK bundle,
rejects the private fixture root and preserves certificate expiry failures.
