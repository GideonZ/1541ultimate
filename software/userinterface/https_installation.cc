#include "https_installation.h"
#ifdef ULTIMATE_PROVIDER_JOURNAL
#include <userinterface.h>
#include "provider_native_startup.h"
#endif

HttpsInstallationResult install_https_identity(UserInterface *ui,Flash *flash)
{
#ifdef ULTIMATE_PROVIDER_JOURNAL
    if (!ui || !flash) return HttpsInstallationResult::Unavailable;
    char domain[17]={};
    if (ui->string_box("Fresh ID (16 hex digits)",domain,16)<=0)
        return HttpsInstallationResult::Cancelled;
    if (ui->popup("New unique ID? Install and reboot?",BUTTON_YES | BUTTON_NO)!=BUTTON_YES)
        return HttpsInstallationResult::Cancelled;
    if (!provider_journal_install_after_boot(*flash,domain)) {
        ui->popup("Not installed. Check ID and reserved storage.",BUTTON_OK);
        return HttpsInstallationResult::Unavailable;
    }
    ui->popup("Identity installed. Cold boot required.",BUTTON_OK);
    return HttpsInstallationResult::Installed;
#else
    (void)ui; (void)flash;
    return HttpsInstallationResult::Unavailable;
#endif
}
