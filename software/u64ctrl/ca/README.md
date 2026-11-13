# Root list for u64ctrl's network recovery and the raw_u64 module's TLS

`cacert.pem` is curl's copy of Mozilla's root list of 2026-10-20 (118 roots), unmodified; its sha256 is pinned in
`tests/lib/u64ctrl_ca_test.py` (`CACERT_SHA256`, the value curl publishes next to the file). u64ctrl's recovery trusts it
instead of ESP-IDF 5.3.1's list, which still holds `emSign ECC Root CA - G3` (removed after the eMudhra key
compromise) and `emSign Root CA - C1` and `emSign ECC Root CA - C3` (removed in the same NSS release).
`emSign Root CA - G1` stays, because Mozilla still ships it.

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
