# Session contract model

This executable design model exercises the proposed lifecycle in
`doc/https-session-contract.md`. It is not linked into firmware or the TLS core.
It requires a Linux C++11 compiler and CMake, with ASan/UBSan available.

```sh
cmake -S target/pc/linux/test_http_session -B /tmp/http-session-build
cmake --build /tmp/http-session-build
ctest --test-dir /tmp/http-session-build --output-on-failure
/tmp/http-session-build/test_http_session
```

The final command prints the scenario list, six completion/cancel/restart event
orders and assertion total. Any failure returns nonzero. The model uses symbolic
identities, deterministic event ordering and logical release counters; it does
not test entropy, real resource reclamation, thread safety or hardware transport.
