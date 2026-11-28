# Root list for u64ctrl's network recovery and the raw_u64 module's TLS

`cacert.pem` is curl's copy of Mozilla's root list of 2026-11-05 (112 roots) without the roots in `REMOVED`
(see below); its sha256 is pinned in `tests/lib/u64ctrl_ca_test.py` (`CACERT_SHA256`). u64ctrl's recovery trusts it
instead of ESP-IDF 5.3.1's list, which still holds `emSign ECC Root CA - G3` (removed after the eMudhra key
compromise) and `emSign Root CA - C1` and `emSign ECC Root CA - C3` (removed in the same NSS release).
`emSign Root CA - G1` stays, because Mozilla still ships it.

## Roots curl still ships

curl builds its file from Mozilla's release branch, which lags NSS master. `REMOVED` in the test also names roots
that master has deleted (Buypass Class 2 and 3), no longer trusts for websites (QuoVadis Root CA 1 G3 and 3 G3,
Certigna Root CA) or distrusts after a date that has made all their certificates expire (Izenpe.com, whose
certificates issued before 2026-04-15 are all expired, because TLS certificates are capped at 200 days since
2026-03-15). `refresh.py` cuts them out of curl's file, and everything else stays curl's, byte for byte. Delete a
line of `REMOVED` once curl's file no longer holds the root.

`DISTRUST_AFTER` lists the roots Mozilla trusts only for certificates issued before a date (Firmaprofesional,
emSign Root CA - G1, DigiCert Trusted Root G4). mbedTLS cannot apply the date, so these also accept later
certificates. They stay while a certificate issued before the date can still be valid, which is the date plus 200
days (`MAX_CERT_DAYS`); the test fails after that, and the line moves to `REMOVED`.

## Chains

The list has no ISRG Root YE or YR. Both built-in servers send the cross-signed ISRG Root X2 (and X1) after
Let's Encrypt YE1 or YE2 and Root YE, and that is the only reason they verify: mbedTLS builds no path, so the last
certificate a server sends must be issued by a root in this list. A server that stops sending the
cross-certificates is refused.

## Refresh

Run `python3 software/u64ctrl/ca/refresh.py` (standard library and OpenSSL 3 or later's `openssl` command, not the
LibreSSL in macOS's `/usr/bin`; it needs the network). It downloads curl's file and its published sha256 (https only;
the hash shows the download is intact, not that curl.se is honest, so review the printed roots), and changes nothing
unless the hash matches, the file is not older than the committed one, the file holds at least `MIN_ROOTS` roots
including ISRG Root X1 and X2 and none in `REMOVED`, and both recovery servers verify against it. Then it replaces
`cacert.pem`, updates `CACERT_SHA256` and the date above, and prints the roots added and removed. Review that output,
add the removed roots to `REMOVED` in `tests/lib/u64ctrl_ca_test.py`, run that test (again after a firmware build),
and bump `IDENT_MINOR` in `main/rpc_dispatch.h` and in `software/wifi/raw_u64/main/rpc_dispatch.h`, which builds
its bundle from the same file. The test also fails when the list is more than a year old
(`MAX_AGE_DAYS`), without needing the network.

If the test fails only because the list is older than `MAX_AGE_DAYS` and `refresh.py` says "no change", Mozilla's data
has not changed for that long: confirm that with `refresh.py` (curl's copy is the committed one), then raise
`MAX_AGE_DAYS` in the test.

The module checks no revocation (no OCSP, CRL or CRLite) and no certificate dates (`CONFIG_MBEDTLS_HAVE_TIME_DATE` is
unset), so a root Mozilla removes stays trusted until a firmware with a newer list is flashed.
