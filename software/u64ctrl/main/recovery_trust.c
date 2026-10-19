/*
 * recovery_trust.c
 *
 * What a recovery trusts: the servers it asks after the one set from the menu,
 * and the keys a manifest must be signed with. A manifest signed by no key in
 * this list is refused, whichever server sent it.
 */

#include <stddef.h>
#include "recovery.h"

/* Tried in this order, after the server set from the menu. Each is the base
   that /v1/recovery is appended to, e.g. "https://recovery.example.org". */
const char *const recovery_builtin_servers[] = {
    "https://ultimate64.ddns.net",
    NULL
};

/* A phone hotspot the Wi-Fi connector joins during a recovery, after the
   stored access points, so a machine with none in reach can still recover.
   Joining an impostor gains it nothing: whatever it serves must pass the
   signature check like any other manifest. */
const char *const recovery_hotspot_ssid = "my_hotspot";
const char *const recovery_hotspot_password = "my_ultimate";

/* Public keys made with the recovery server's manifest.py keygen, pasted as
   `manifest.py c-key NAME.pub.pem` prints them; a spare next to each main key.
   Test keys from the repository must never appear here: their private halves
   are public. */
const char *const recovery_trusted_keys[] = {
    // Signing key, generated on blue
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAElCd1fKZwiUF1JfyVLz8y5Fx1Dif3\n"
    "PzRc4zbUGZflm0CliyoxVcVatZlo/cl6AaECEcEXgXoyUgrQjzq66Oc9+A==\n"
    "-----END PUBLIC KEY-----\n",

    NULL
};
