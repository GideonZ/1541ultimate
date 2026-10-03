# Command-interface HTTP/HTTPS soak tests

`https_repetition_test.py` runs a bounded number of paired HTTP/HTTPS requests.
`https_timed_test.py` requires a measured duration and fresh controller telemetry.
Both are registered manual suites in `run-tests` and share the functional
tests' transport, settings-preservation checks and cleanup.

See the [hardware test guide](../../../e2e/io/command_interface/https.md) for
prerequisites and invocation. A stopped, failed or incomplete run is never a pass.
