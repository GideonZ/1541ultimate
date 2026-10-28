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

The native FPGA configuration disables its TRNG. No flash journal adapter or
domain provisioning mechanism is selected here. Host tests simulate durable
commits and crashes; they do not establish physical storage atomicity, endurance
or rollback protection. Those remain activation requirements.

## ESP32 trust, UTC and entropy

`provider_control_provision_tls` configures explicitly supplied roots and a
`TlsPolicy` before owner startup. Parsing must succeed for the entire bundle;
input is bounded to 64 KiB and live replacement is refused. A product must still
select and deliver an authenticated trust bundle and its update mechanism.

`TlsPolicy` requires a bounded UTC qualification and a continuously qualified
entropy source. UTC is checked against a monotonic anchor, with an explicit
lifetime (at most one day) and tolerance (at most 60 seconds). Invalid input,
expiry and observed clock discontinuities revoke the qualification. Existing
SNTP completion and a plausible RTC date are not automatically trusted.

The product must serialize policy updates with bounded task-context critical
sections and notify entropy loss before disabling its qualified hardware source.
Each TLS session captures a generation. Clock updates and entropy revoke/restore
retire earlier generations even if readiness is restored between checks. A new
session may capture the new qualification after the old session is cleaned up.
Entropy is checked before and after generation; output is discarded on revocation.

The existing callback configuration entry point remains for explicit integration
fixtures. Production integration should use the generation-bound policy. Neither
entry point installs default roots or silently treats SNTP or an RNG call as proof
of trust. Hardware source selection, radio/ADC lifecycle integration and a trusted
UTC authority remain open. The emulator's UTC and entropy assertions are fixture
inputs, not hardware qualification.

## Validation

`test_provider_provisioning` covers restart identity, ambiguous reservations,
replay, exhaustion, clock boundaries, rollback and concurrent policy changes.
The ESP32 production owner-task fixture exercises the platform policy callbacks,
trust parsing, generation revocation and RSA/ECDSA requests. Its renewal actor
reserves a 250 ms reply margin before the original request deadline and accounts
for a pending renewal before Close. Scheduling and cleanup limits are unchanged.
