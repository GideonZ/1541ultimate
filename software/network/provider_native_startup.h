#ifndef PROVIDER_NATIVE_STARTUP_H
#define PROVIDER_NATIVE_STARTUP_H
#include <stdint.h>
class Flash;
class WiFi;
class HttpSecureProvider;
// Enabled only by the explicit native build option. Calls are serialized during
// boot. The installer must supply a fresh domain before configuration startup.
bool provider_journal_install(Flash&, uint64_t fresh_domain);
// Operator flow after an empty boot. It never activates this boot or erases
// storage. A successful installation requires a cold boot before HTTPS use.
bool provider_journal_install_after_boot(Flash&, const char *fresh_domain_hex);
HttpSecureProvider *provider_https_factory(); // Stable for the firmware lifetime.
void provider_journal_prepare(Flash&);
bool provider_journal_reserved_slot(Flash&, int slot);
void provider_journal_start_owner(WiFi&); // Deferred until application transport is ready.
void provider_journal_stop_owner();
#endif
