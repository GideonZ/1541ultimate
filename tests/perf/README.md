# Performance benchmarks

Benchmarks that measure timing or throughput on a real device. The number they
produce is the result, so they are not part of the release gate and are not
registered in `run-tests`.

| File | Scope |
| --- | --- |
| `temp_auto_cleanup_perf_test.py` | Managed `/Temp` upload latency and throughput, with the device's Temp Auto Cleanup and Temp Subfolders settings enabled, then disabled |

## Running

`./run-tests -H <host> --perf` includes this stage. To invoke a benchmark
directly, use an explicit host.

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
