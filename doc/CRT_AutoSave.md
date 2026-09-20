# Cartridge Auto-Save

Status: builds for U64, U64-II and U2+, host test green, carried end to end through an emulator
(§7), and the EasyFlash case run on a U64 (§6).

## 1. Purpose

A game that saves to its cartridge (EasyFlash via EAPI, GMod2 via EEPROM) only changes the
image in memory. The CRT file on disk keeps the old state until the user runs *Save Cartridge*
by hand, types a file name, and confirms the overwrite. Forgetting that loses the save.

This feature writes a changed cartridge back to the file it was loaded from, at the point where
the firmware gets control back: when the menu opens.

## 2. Baseline

| Item | Where | Behaviour |
|---|---|---|
| Save Cartridge | `c64_subsys.cc`, `MENU_C64_SAVE_CARTRIDGE` | Asks for a name, writes into the browser's current folder. |
| Load | `c64_crt.cc`, `load_crt()` | Keeps the file name, drops the path. The source is not recoverable. |
| Save | `c64_crt.cc`, `save_crt()` | Restores the original EAPI in cartridge memory, writes, patches again. |
| EEPROM dirty | `c64.cc`, `get_eeprom_dirty()` | Reading the flag clears it in hardware. |
| Cartridge image | `c64.h`, `get_cartridge_rom_addr()` | 4 MiB at `0x03C00000`, plain CPU access, arbitrated against the cart port. |

This sits on top of the CRT work that went in after v3.15 — bank mirroring up to the whole
cartridge memory (#899), the Ocean 16K and Comal 80 fixes, and ignoring data behind the last chip
packet. `auto_mirror()` now fills all 4 MiB, while `regenerate_easyflash_chunks()` still describes
64 banks, so the hash in §4.4 covers 1 MiB for EasyFlash, not the mirrored copies above it.

## 3. Defects found in the baseline

**D1. Save from the overlay modifies a running cartridge.**
The overlay menu does not stop the C64 (`Overlay::take_ownership()`); REST does not either. `save_crt()` then
swaps 768 bytes of EAPI code in live cartridge memory. A game calling
EAPI during the save executes the original EasyFlash 3 EAPI, which drives flash hardware the
emulation does not have. Each save also leaks 768 bytes: `patch_easyflash_eapi()` allocates a new
copy of the original without freeing the old one.

**D2. System Info discards the GMod2 save state (bug-091).**
System Info (F4) calls `get_eeprom_dirty()` (`system_info.cc`), which clears the flag. A later
*Save Cartridge* sees a clean EEPROM, skips `get_eeprom_data()`, and writes the
EEPROM contents from load time.

**D3. The source of a cartridge is not kept.** Required for any write-back; also upstream issue #52.

## 4. Design

### 4.1 Source tracking (D3)

`C64_CRT` keeps one full pathname, `source`:

- set by `load_crt()` on success: `path + "/" + filename`, or `filename` alone when `path` is
  empty (REST passes a full pathname that way, `route_runners.cc`);
- replaced by a successful *Save Cartridge*, so the cartridge follows the file it was last saved
  to (save-as semantics);
- cleared by `initialize()`, i.e. on every load, on failure, and by `clear_crt()`.

### 4.2 Save without touching memory (D1)

`save_crt()` no longer calls `unpatch_easyflash_eapi()` / `patch_easyflash_eapi()`. The chunk that
contains the EAPI (`cart_memory + 0x3800`, bank 0 ROMH) is written in three parts: memory before
the EAPI, the 768 original bytes from `original_eapi`, memory after it. Condition:
`original_eapi != NULL && local_type == CART_EASYFLASH` — `original_eapi` survives until the next
`cleanup()`, so the type check is required.

*Save Cartridge* runs inside `begin_stopped_session()` / `end_stopped_session()`. From the C64
menu the machine is already frozen and this is a no-op. From the overlay it stops the C64 for the
duration of the write, so the file is one consistent image.

### 4.3 EEPROM dirty latch (D2)

A file-static flag in `c64.cc` holds the dirty state until the data is actually taken:

| Call | Hardware flag | Latch |
|---|---|---|
| `get_eeprom_dirty()` | cleared only if it read as set | set if hardware was set; returned |
| `get_eeprom_data()` | cleared before the copy | cleared |
| `set_eeprom_data()` | cleared after the copy | cleared |

Reading the flag becomes side-effect free for callers. The hardware flag is cleared only when it
was seen set, so a C64 write between read and clear is no longer lost. `get_eeprom_data()` clears
before copying: a write during the copy sets the flag again and is caught next time.

### 4.4 Change detection

32-bit hash over every chunk's data, exactly the bytes `save_crt()` would write:

    h = 0x811C9DC5
    for each 32-bit word w:  h = rotl(h ^ w, 5) + 0x9E3779B9

Each step is a bijection in `h` for a fixed `w`, so two images that differ in a single word
always hash differently. No multiply: the U64 Nios II is the `tiny` core
(`nios_appl_bsp/system.h`), without hardware multiplier and without data cache. The absence of
a data cache also means CPU reads see every FPGA write to cartridge memory.

Before hashing, a set EEPROM latch is resolved by copying the EEPROM into its chunk buffer, so the
EEPROM is covered by the same hash.

All cartridge types are hashed. In `all_carts_v5.vhd` only EasyFlash writes into the ROM
image (`ef_write`, Ultimax, `$8000`/`$E000`); every other `allow_write` targets the separate cart
RAM or GeoRAM. The U64 core sources are not part of this repository, so this is unconfirmed for U64 hardware.
Hashing everything costs time proportional to the image size and needs no per-type knowledge.
The duration is logged.

The baseline is taken at the end of `read_crt()`, after EAPI patch, mirroring and chunk
regeneration — the state the C64 starts from. It is renewed after every successful save and after
the user declines one.

### 4.5 Trigger

`UserInterface` gets one static hook, called in `run_once()` after the menu has appeared:

    static void set_menu_enter_hook(void (*hook)(UserInterface *));

`c64_subsys.cc` registers its handler there and owns everything that needs the user interface: the
check, the dialog and the file juggling. `c64_crt.cc` keeps only what belongs to the cartridge
image — source path, hash, `save_crt()` — and therefore still builds in the host test under
`software/io/c64/tests`, which compiles that file on its own. `userinterface.cc` gains no
dependency on the C64 code, so the updater build (`RECOVERYAPP`) is unaffected.

The hash runs without stopping the C64. In overlay mode a game writing at that moment can yield
a torn read. Two outcomes: a false "changed" (the save that follows runs stopped and is correct),
or a missed change (caught at the next menu open). The save itself runs stopped.

### 4.6 Modes

Config item **Save Changed Cartridge** (`CFG_C64_CRT_AUTOSAVE`, id `0x76`), group *Memory
Configuration*, after *Cartridge*. Values `Off`, `Ask`, `Auto`; default `Ask`.

| Condition at menu open | Off | Ask | Auto |
|---|---|---|---|
| No CRT loaded, or unchanged | — | — | — |
| Changed, source writable | — | *Save changes to NAME?* Yes: save. No: new baseline. | Save, progress bar only |
| Changed, source not writable | — | One notice, new baseline | One notice, new baseline |
| Save failed | — | Error popup, baseline kept | Error popup, baseline kept |

"Not writable" covers: no source, `/Temp` (REST uploads), `/Flash` (internal flash; too small for
large images, and not to be worn by periodic writes), and any path `is_path_writable()` rejects.
The notice points to *Save Cartridge*; the new baseline stops it from repeating until the next
change. A failed save keeps the old baseline and reports again at the next menu open.

### 4.7 Write procedure

    NAME.tmp  <- save_crt()                 fail: delete NAME.tmp, report
    NAME.bak absent:  NAME -> NAME.bak      once, keeps the file as it was first loaded
    NAME.bak present: delete NAME
    NAME.tmp -> NAME

`.bak` is appended rather than replacing `.crt`, so the backup does not show up as a loadable
cartridge. If the last rename fails, the new state stays in `NAME.tmp` and the error says so.

## 5. Known gaps

- REST `run_crt` and anything else that loads a cartridge without opening the menu replaces a
  changed image without a check.
- The telnet menu (`run_remote()`) has no hook.
- A save that is in progress on the C64 when the menu opens is captured half-written. Real
  hardware has the same exposure to power-off.
- Cartridge types other than EasyFlash and GMod2 are hashed on the assumption in 4.4.

## 6. Hardware test

| # | Setup | Action | Expected |
|---|---|---|---|
| 1 | EF game with EAPI save, mode Ask | Save in game, open menu | Prompt; Yes writes `NAME`, creates `NAME.bak` |
| 2 | as 1 | Save again, open menu, Yes | `NAME` updated, `NAME.bak` unchanged |
| 3 | as 1 | Open menu without saving in game | No prompt |
| 4 | as 1 | Prompt, No; open menu again | No second prompt |
| 5 | GMod2 game, mode Ask | Save in game, F4 System Info, then open menu | Prompt appears (D2 fixed) |
| 6 | EF game, overlay on HDMI | Save in game, open overlay, Yes | Saved; game keeps running after close |
| 7 | CRT via REST upload | Save in game, open menu | Notice once, no write |
| 8 | Mode Auto | Save in game, open menu | Progress bar, no prompt |
| 9 | Manual *Save Cartridge* as NEW | Change, open menu | Prompt names NEW |

Log lines to check (syslog): `[CRT] hash`, `[CRT] saved`, `[CRT] save failed`.

Row 1 has been run on a U64 (2026-09-20): an EasyFlash game that saves through EAPI, mode Ask. The
prompt appeared after the game had saved, the file was written back and `NAME.crt.bak` was created.
The other rows are still open.

## 7. Emulator run, 2026-09-20

ue2emu with the U64-II firmware of this branch, C64 ROMs loaded, the cartridge on a USB stick.
Wasteland (EasyFlash, 1 MiB, EAPI save), started from the file browser.

| Step | Observed |
|---|---|
| Run Cart on /USB0/wasteland_ef_v1.0.1.crt | EasyFlash, 1 MiB read, EAPI patched |
| Intro SPACE, menu S, game reaches "Enter new location (Y/N)?" | the game writes its save to flash |
| Menu button | `[CRT] hash 62D526E6, baseline D05CA873, 120 ms` |
| — | popup "Cartridge changed. Save it to wasteland_ef_v1.0.1.crt?" |
| Yes | `[CRT] saved /USB0/wasteland_ef_v1.0.1.crt`, file 553 KB, `…crt.bak` 1 MiB |
| Load the saved file, menu button | `[CRT] hash 62D526E6, baseline 62D526E6` — no popup |
| Start the game from it | after Start the bar offers "Continue Load Restart" instead of a fresh game |

The file is smaller than the original because `save_crt()` drops EasyFlash banks that are all
`$FF`; `read_crt()` clears the region to `$FF` first, so the image in memory is identical, which is
what the equal hashes show.

120 ms for 1 MiB on the U64-II RISC-V. The U64 Nios II has no cache and no multiplier, so expect
more there; the log line carries the number.
