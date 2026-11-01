#ifndef HTTPS_INSTALLATION_H
#define HTTPS_INSTALLATION_H
class UserInterface;
class Flash;
enum class HttpsInstallationResult { Installed, Cancelled, Unavailable };
// Serialized operator action. Does not erase storage, activate HTTPS or reboot.
HttpsInstallationResult install_https_identity(UserInterface *, Flash *);
#endif
