# Device validation status

The revised owner/provider implementation has not been installed or tested on the
physical Commodore. Host, cross-build and ESP32 emulator results must be reported
separately from device results.

The [prototype report at its fixed revision](https://github.com/araxis/1541ultimate/blob/fc1498932ac0c01b16786660262ce2181fa3c80e/doc/https-device-validation.md)
preserves the earlier measurements and limitations. They do not apply to this
replacement implementation. HTTP-only checks from the merged prerequisite PRs
likewise do not establish revised HTTPS behavior.

The installation and hardware gates are listed in
[release readiness](https-release-readiness.md). New device evidence must identify
both installed images, FPGA/device scope, endpoint certificates, complete attempts,
raw logs, peer observations and cleanup before claiming a pass.
