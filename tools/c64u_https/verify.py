"""Offline release-specific checks for the retail C64 Ultimate test package."""
import hashlib
import re

OFFICIAL_SHA256 = "487a7d2104d6d32f61f33ca36a16439d947cd1b0eab75d5b1e78248de51bb58d"


def verify_architecture(attributes, disassembly):
    arches = re.findall(r'Tag_RISCV_arch:\s*"([^"]+)"', attributes)
    # GCC 11's rv32i multilib uses the old ISA version, which includes CSRs.
    if arches != ["rv32i2p0"]:
        raise ValueError(f"Not the retail RV32I architecture: {arches}")
    forbidden = re.compile(r"^(?:mul\w*|div\w*|rem\w*|amo.*|lr\..*|sc\..*|c\..*|fence\.i|f(?:add|sub|mul|div|sqrt|ld|lw|sd|sw|cvt).*)$")
    instructions = re.findall(r"^\s*[0-9a-f]+:\s+([\w.]+)", disassembly, re.MULTILINE)
    if not instructions:
        raise ValueError("No executable instructions were checked")
    rejected = sorted(set(filter(forbidden.match, instructions)))
    if rejected:
        raise ValueError(f"Instructions outside the retail target: {rejected}")


def verify_embedded(package, images):
    for name, data in images.items():
        if not data or package.count(data) != 1:
            raise ValueError(f"Updater does not embed exactly one current {name}")


def verify_recovery(official, fpga, partitions):
    if hashlib.sha256(official).hexdigest() != OFFICIAL_SHA256:
        raise ValueError("Recovery updater is not the reviewed official C64U 1.1.0 file")
    verify_embedded(official, {"retail FPGA": fpga, "ESP partition table": partitions})
