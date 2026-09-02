# Performance benchmarks

Benchmarks that measure timing or throughput on a real device. The number they
produce is the result rather than a pass or a fail, so they are registered in
`run-tests` under the `perf` category and never run as part of an ordinary run.

| File | Scope |
| --- | --- |
| `temp_auto_cleanup_perf_test.py` | Managed `/Temp` upload latency and throughput, with the device's Temp Auto Cleanup and Temp Subfolders settings enabled, then disabled |
| `typing_speed_perf_test.py` | The two ways the tree types into a menu field, compared on speed and on whether the characters survive |
| `telnet_key_latency_perf_test.py` | What one Telnet keystroke costs, reported separately for a printable character, an arrow key and a lone ESC |

`pacing.SPLIT_KEY_DRAIN_SECONDS` is not established here. Two instruments
outside this directory already cover it, each with a better oracle than a
benchmark could have: `tests/e2e/io/c64/key_injection_test.py` measures the
rate keys arrive at by reading the machine's own memory, and
`tests/soak/filemanager/menu_navigation_soak_test.py` measures how soon the
result may be read back by driving a real field and reading it while it is
still open. `tests/e2e/doc/key-injection-rate.md` carries the numbers.

## Running

`./run-tests --perf <target>` runs this category; `-s <name>` picks one
benchmark. To invoke a benchmark directly, use an explicit host.

```sh
# Inspect the complete CLI without contacting a device.
tests/perf/temp_auto_cleanup_perf_test.py --help

# Full benchmark: both stages, default warmup and measured counts.
tests/perf/temp_auto_cleanup_perf_test.py -H u64 -p PASSWORD

# One stage only, without touching device config.
tests/perf/temp_auto_cleanup_perf_test.py -H u64 -p PASSWORD \
  --stage enabled --no-config-change
```

`--help` is authoritative for stage selection, warmup and measured counts,
duration, and the disabled-stage upload cap.

## Safety

Run against a dedicated test device. Each stage purges the managed Temp upload
area before it starts and writes many small uploads during measurement. Temp
Auto Cleanup and Temp Subfolders are changed for the duration and restored on
exit, unless `--no-config-change` is used, which then requires a single
`--stage`.

## Rules for extending

- Put a benchmark here only when a duration, throughput or latency number is
  the result. A deterministic functional check belongs in `tests/e2e/`, even if
  it is slow.
- Name executable entry points `*_perf_test.py`. Python only.
- Report through `tests/lib/report.py`; see [its rules](../lib/README.md).
- Keep runs bounded by a duration or count budget, never an open-ended loop.
- Restore any device configuration the benchmark changes and remove generated
  files in a `finally` path or exit handler.
- Document destructive behaviour, configuration changes and expected run time
  here; keep flag-by-flag detail in `--help`.
