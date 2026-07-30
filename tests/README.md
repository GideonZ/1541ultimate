# Tests

This tree contains tests that exercise a built firmware image on real hardware.
They complement the fast host-side unit tests kept next to their owning code,
such as `software/api/tests/`, `software/filemanager/tests/`, and
`software/io/usb/tests/`.

| Suite | Purpose |
| --- | --- |
| [E2E](e2e/) | Deterministic functional and regression checks across complete device workflows. These are the hardware release gate. |
| [Soak and stress](soak/) | Time- and load-based checks for leaks, exhaustion, races, and transport degradation. These are diagnostic endurance tests, not the E2E gate. |

Put a test in the narrowest matching suite. Keep isolated logic tests beside
the production component; use E2E only when the behavior requires a real
device or crosses subsystem boundaries, and use soak/stress when duration or
repetition is essential to the result. Each linked README contains the
authoritative running and extension guidance for that suite.
