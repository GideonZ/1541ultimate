# HTTPS review and installation gates

The draft implementation can be reviewed independently of device installation.
It must not be described as hardware-qualified or ready to flash.

Before installation:

1. Identify the installed controller and confirm the FPGA variant. The RAM-only
   diagnostic is locally tested, but has not been executed on the device.
2. Close exact controller-image installation/readback verification and equal/newer
   version handling. The updater's transfer acknowledgements are not a hash readback.
3. Review the configuration backup, identity-journal reservation and practical
   recovery procedure. A saved backup is not proof that restore/recovery works.
4. Prepare the trusted endpoint and fixed hardware runner/peer acceptance criteria.
5. Review the coordinated native/controller image hashes and authorize installation.

Then qualify ordinary HTTP and HTTPS on the exact installed revision: valid GET
and HEAD, authentication failures, original deadlines, cancellation, restart
fencing, repeated cleanup and resource measurements. Physical UART timing, actual
entropy, journal persistence/endurance and native stack use remain unqualified.

The selected ordinary SNTP source is unauthenticated. Certificate date checks are
mandatory, but clock bounds do not authenticate the time source. Journal rollback
and cloning cannot be detected by CRC. These limitations remain explicit.

Prior prototype and HTTP-only device results, and standalone TLS duration runs,
are historical evidence. They are not qualification of this connected firmware.
Maintainer agreement on UART command allocation and shared-backend integration is
also pending. Keep default activation disabled and the PR in draft until the
remaining scope and acceptance criteria have been reviewed.
