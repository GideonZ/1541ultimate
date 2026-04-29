#ifndef MONITOR_FILE_IO_H
#define MONITOR_FILE_IO_H

#include <stdint.h>

class UserInterface;
class MemoryBackend;

// Glue layer between the machine code monitor and shared infrastructure
// (TreeBrowser, FileManager, SubsysCommand). These helpers exist so that
// monitor-specific wiring does not bloat the UserInterface class. Host-test
// builds provide stub implementations.
namespace monitor_io {

// Open the standard TreeBrowser in pick mode and return the user's selection.
// `save_mode` switches between LOAD pick semantics (RETURN/RIGHT picks a file)
// and SAVE semantics (additionally F5 picks the current directory only,
// returning name_out empty so the caller can prompt for a filename).
// Returns true if the user picked something, false on ESC / cancel.
bool pick_file(UserInterface *ui, const char *title,
               char *path_out, int path_max,
               char *name_out, int name_max,
               bool save_mode);

// Read the file at <path>/<name> into the C64 address space via the supplied
// memory backend. When `use_prg_addr` is true the first two bytes of the file
// are consumed as the load address and override `start_addr`. `offset` and
// `length` (in bytes; `length_auto` means "until end of file") describe the
// range of the file to load. When non-NULL, `out_start_addr` and `out_bytes`
// receive the actual destination start address (after PRG-header resolution)
// and the number of bytes that were written into memory — used by the caller
// to render a confirmation overlay. Returns NULL on success, or a static
// error string suitable for popup display.
const char *load_into_memory(const char *path, const char *name,
                             MemoryBackend *backend, uint16_t start_addr,
                             bool use_prg_addr, uint32_t offset,
                             bool length_auto, uint32_t length,
                             uint16_t *out_start_addr, uint32_t *out_bytes);

// Save the C64 memory range [start..end] to <path>/<name>. The shared overwrite
// helper (`create_file_ask_if_exists`) is used, hence the UI parameter.
// Returns NULL on success, otherwise a static error string.
const char *save_from_memory(UserInterface *ui, const char *path, const char *name,
                             MemoryBackend *backend,
                             uint16_t start, uint16_t end);

// Instruct the FPGA C64 to commence execution at `address` once the monitor
// (and freezer) is left. Reuses the boot-cart DMA-jump pathway so existing
// unfreeze / cartridge sequencing is preserved.
void jump_to(uint16_t address);

} // namespace monitor_io

#endif
