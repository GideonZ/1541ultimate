#ifndef PROVIDER_NATIVE_STARTUP_H
#define PROVIDER_NATIVE_STARTUP_H
#include <stdint.h>
class Flash;
class WiFi;
// Enabled only by the explicit native build option. Calls are serialized during
// boot. The installer must supply a fresh domain before configuration startup.
bool provider_journal_install(Flash&, uint64_t fresh_domain);
void provider_journal_prepare(Flash&);
bool provider_journal_reserved_slot(Flash&, int slot);
void provider_journal_start_owner(WiFi&);
void provider_journal_stop_owner();
#endif
