# Virtual printer E2E harness

Reproduces and validates fixes for crashes in the Ultimate 64/64e virtual
printer (`software/io/printer/*.cc`) against a **real Ultimate 64 or 64e**
over its REST API and FTP server. No emulator, no MCP/bridge tooling.

## Purpose

The virtual printer (IEC device 4/5) accepts Commodore MPS or Epson FX-80
protocol bytes from the C64, rasterizes them into a page bitmap, and saves a
PNG (or ASCII/RAW) file when the page is ejected. This harness drives that
path end to end from a dedicated 6510 program, so it exercises the real IEC
timing and buffering behaviour that a BASIC-only test would miss.

## Hardware/firmware prerequisites

- A real Ultimate 64 or Ultimate 64 Elite (I or II) or Ultimate-II+, reachable
  by hostname/IP, with the REST API and FTP server enabled (both are on by
  default).
- [64tass](https://sourceforge.net/projects/tass64/) on `PATH` (or pass
  `--assembler /path/to/64tass`).
- Python 3.9+. Core functionality (reproduction, structural PNG verification,
  full-page bitmap coverage checks) needs no third-party packages. OCR-based
  text verification is an optional enhancement - see below.

## How it works

- **REST** (`http.client`, matching the style of
  `tools/io/temp-auto-cleanup/temp-auto-cleanup-perf-test.py`): reads/writes
  `Printer Settings` config, pokes a parameter block and polls a status block
  via `machine:writemem` / `machine:readmem`, uploads and runs the assembled
  PRG via `POST /v1/runners:run_prg`, and drives the on-device Tasks menu
  (`machine:menu_button` + `machine:input`) to trigger **Printer >
  Flush/Eject** — the same interactive action a user takes, and the one that
  actually calls into the PNG save path (`IecPrinter::flush()` →
  `MpsPrinter::FormFeed()`).
- **FTP** (`ftplib`, user `user` / password `password` by default, matching
  the same reference script): downloads the resulting `-NNN.png` files to
  verify they exist and are structurally valid PNGs, creates the output
  directory if it doesn't exist yet (the printer's `fopen()` does not create
  missing directories), and is the source of truth for output verification -
  REST's `GET /v1/files/{path}:info` was observed to return stale/missing
  results for files that FTP can see immediately.
- **Settings are restored**: original `Printer Settings` are captured before
  the run and restored after, unless `--no-config-change` is passed. Nothing
  is ever written to flash (`save_flash` is never called).

## The assembly workload (`printer-e2e.asm`)

A single parametrized PRG, assembled once per run. Runtime parameters are
written to `$C010` before the PRG is started:

| Address | Meaning                                                        |
|---------|----------------------------------------------------------------|
| `$C010` | emulation: 1 = Epson, 2 = Commodore                             |
| `$C011` | mode: 1 = bitmap, 2 = text                                      |
| `$C012` | rows per page (1-255)                                           |
| `$C013` | pages (1-9)                                                     |
| `$C014` | IEC bus id (4 or 5)                                             |
| `$C015` | bitmap mode only: seed-pattern repeats per row (1-255, 16 bytes/repeat) |

It maintains a pollable status block at `$C000` (magic, phase, current
page/row, last `READST`, final status, heartbeat) so the harness can tell the
difference between "still running", "finished", and "the device stopped
responding entirely" without guessing at timing.

The program deliberately keeps its own state (loop counters, string-print
pointer) off zero page entirely — see the comment at the top of the `.asm`
file. A page/row counter placed in ostensibly-free zero page was corrupted
partway through a long multi-page `CHROUT` session during development,
silently truncating later pages; moving everything to plain static storage
(and using self-modifying code instead of zero-page indirect addressing for
string printing) fixed it.

Text rows are deterministic: `U64PRN <EPSON|COMMODORE> TEXT PAGE=NNN
ROW=NNN ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789`, printed in NLQ + bold +
double-strike (not the default draft-quality dot font, which is too sparse
for OCR to read back reliably even after image preprocessing). Bitmap rows
repeat the deterministic 16-byte seed patterns from the printer spec
(Commodore BIM / Epson `ESC Z` quadruple-density) `$C015` times to form a
visible horizontal band (or, with a large repeat count, a full-width row).

## Running the smallest reproducer

```sh
./printer-e2e.py -H u64 --preset crash-epson-png --rows 1 --pages 1 \
    --output-base /Usb0/printer/e2e-smoke --verify-output
```

If your unit has no USB stick mounted, point `--output-base` at a path under
a filesystem that does exist instead, e.g. `/Temp/printer/e2e-smoke` — the
harness creates the `printer` directory over FTP if it isn't there yet.

## Forcing the issue #717 scan/overflow shape

```sh
./printer-e2e.py -H u64 --preset issue-717-overflow
```

This preset forces the two conditions the original branch did **not** verify
together:

- the `Output file` base is a unique `/Temp/printer-*` prefix, so
  `calcPageNum()` scans a parent directory that also contains a sibling entry
  with that exact basename as a directory
- the harness pre-seeds colliding prefix entries (`printer/`,
  `printer-old.png`, `printer-7.png`, `printer-abc.png`, `printer-123.txt`)
  plus a real `printer-007.png`, then sends one continuous bitmap print long
  enough to overflow naturally onto a second page before the final
  Flush/Eject

Verification therefore expects **two** new PNGs starting at the seeded next
page number (`...-008.png`, `...-009.png`), and fails if page numbering is
thrown off by the directory collision or if the overflow path wedges the
machine.

## Exact issue #717 workload shape

```sh
./printer-e2e.py -H u64 --preset issue-717-basic --output-base /Usb0/printer \
    --output-type "PNG B&W" --page-top-margin 1 --page-height 66 --verify-output
```

The harness tokenizes and runs the reporter's BASIC program verbatim as BASIC
V2. It expects one PNG page for that exact program at the reported 66-line
page height.

## Running the full required matrix

```sh
./printer-e2e.py -H u64 --stage matrix --emulation both --mode both \
    --output-type "PNG B&W" --rows 4 --pages 1 --verify-output

./printer-e2e.py -H u64 --stage matrix --emulation both --mode both \
    --output-type "PNG B&W" --rows 4 --pages 2 --verify-output

./printer-e2e.py -H u64 --preset full-matrix --page-top-margin 1 \
    --page-height 66 --verify-output
```

`--stage matrix` (or `--emulation both --mode both`) runs all four
combinations (Epson/Commodore x bitmap/text). When more than one combination
runs in the same invocation, each gets a distinct `Output file` automatically
(a `-eb`/`-et`/`-cb`/`-ct` suffix) so page numbering from one combination
never gets misread as another's output.

**Output file length**: the firmware's `Output file` setting silently
truncates beyond 31 characters (`CFG_PRINTER_FILENAME` in `iec_printer.cc`).
The harness raises an explicit error if a computed path would exceed that,
rather than let verification fail confusingly against a truncated filename.

**Natural overflow verification**: `--verify-output` normally expects the same
number of PNGs as `--pages`, because each requested page is explicitly ejected
by the harness. For a single continuous job that *naturally* overruns onto an
extra page before the final eject (the issue #717 shape), set
`--expected-output-pages` higher than `--pages`; the `issue-717-overflow`
preset does this automatically.

## Full-page bitmap coverage test (opt-in, slow)

```sh
./printer-e2e.py -H u64 --emulation both --full-page-bitmap \
    --output-base /Temp/printer/e2e-full --page-top-margin 1 --page-height 66
```

`--full-page-bitmap` prints a bitmap that fills the *entire* configured page
(both width and height), not just a small band, computing the row count and
seed-pattern repeat count needed to cover the page without overrunning it
into an extra automatic page break. It's disabled by default: a full page is
tens of thousands of IEC bytes and can take a couple of minutes per
emulation. It forces bitmap-only mode and 1 page, and always verifies
(regardless of `--verify-output`) that the resulting PNG's ink actually
covers most of the page — width, height, and ink density within the covered
area — using the bundled pure-stdlib PNG decoder (`png_lite.py`), not just
that a plausible-looking file was produced.

## OCR text verification (optional enhancement)

```sh
pip install pytesseract   # plus the tesseract-ocr binary via your OS package manager
```

When `pytesseract` and Pillow are importable, `--verify-output` on a
text-mode combination also OCRs the printed page and checks that the
recognized text contains the expected emulation tag (`EPSON`/`COMMODORE`),
the `TEXT` mode tag, and a `PAGE=NNN`/`ROW=NNN` marker for every row - i.e.
it verifies the *actual dynamic content* the assembly program's page/row
loop and decimal-formatting routine produced, not just that some ink exists.
If the OCR packages aren't installed, this step is skipped with a clear
message and the run still completes (structural PNG verification is
unaffected). The fixed decorative `A-Z 0-9` suffix on each row is
intentionally not required to match character-for-character: several glyphs
(e.g. `Q`, `X`, `Z`) are genuinely ambiguous to OCR at this font/resolution,
and matching them isn't necessary to prove the printed content is correct.

## Output verification

`--verify-output` downloads every expected `<base>-NNN.png` over FTP and
checks: non-zero size, valid PNG signature/chunk structure/CRCs, readable
`IHDR` dimensions, and (text mode, if OCR is available) the recognized text
content. Run structural verification standalone against files left over from
an earlier run (or produced interactively) with:

```sh
./verify-printer-output.py -H u64 --output-base /Usb0/printer/e2e-smoke --pages 1
./verify-printer-output.py -H u64 --path /Temp/some-page-001.png
```

## Crash classification

- `PASS` / `PASS_NO_VERIFY` - program completed, page(s) ejected via
  Flush/Eject, output verified (or verification skipped).
- `FAIL_VERIFICATION` - completed and files exist, but they aren't
  well-formed PNGs (or, for text mode, OCR didn't find the expected content).
- `FAIL_IEC` - the PRG reported a KERNAL `OPEN`/`CHKOUT` failure (device not
  present, wrong bus id, etc).
- `FAIL_TIMEOUT` - the on-device program didn't finish within
  `--timeout-seconds`, but REST is still responsive.
- `FAIL_CRASH_HARD` - REST *and* ICMP both stopped responding. This is the
  reported bug's signature: the screen goes off and the whole SoC wedges;
  a C64/machine reset cannot recover it.
- `FAIL_HARNESS` - the harness itself hit an unexpected error (menu
  navigation, assembly, etc), unrelated to the firmware under test.

## After `FAIL_CRASH_HARD`

REST and even ICMP are dead — there is no software-only recovery. Redeploy
the already-built firmware over JTAG from the repo root.

This takes about 30-40 seconds (it's a direct `nios2-download`, not an FPGA
rebuild) and brings the device back without needing a physical power cycle,
provided the JTAG cable stays connected. 

## Known limitations

- **Text mode, many rows, multiple pages**: printing more than ~4-7 NLQ text
  rows per page across 2+ pages in one continuous job was observed to
  occasionally save fewer PNG pages than requested (a page's content
  missing or merged into a neighbour), independent of the zero-page fix
  described above. Bitmap mode at the same row/page counts, and text mode
  at ≤4 rows/page or a single page at any row count, were not affected. This
  looks like a timing/backpressure interaction between the firmware's
  CPU-heavier NLQ character rendering and the harness's back-to-back IEC
  transmission, not a data-correctness bug: every combination this harness
  actually validates (see "Running the full required matrix" above) stays
  well inside the range that was verified reliable. It's called out here
  rather than worked around blindly, since it did not reproduce the crash
  this harness exists to catch and would need separate, dedicated
  investigation to root-cause.
- The default `Output file` root is `/Usb0/printer/e2e-<run-id>` per the
  reported bug's configuration, but this repo's test unit has no USB stick
  mounted; the examples above use `/Temp/printer` explicitly for that reason.
- `--seed-count`, `--test-count`, `--duration`, `--host-output-dir`,
  `--keep-output`, `--limit` are accepted for CLI-shape compatibility with
  `temp-auto-cleanup-perf-test.py` but are no-ops here — this harness is a
  single deterministic print-and-verify per combination, not a
  throughput/soak benchmark.
