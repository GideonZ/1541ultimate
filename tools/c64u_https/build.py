#!/usr/bin/env python3
"""Build and test a prepared C64 Ultimate snapshot, entirely offline.

Run in Linux/WSL after sourcing ESP-IDF 5.3.6/export.sh. Output files are
experimental review candidates; this script cannot upload or install them.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

from prepare import BASELINE
from verify import verify_architecture, verify_embedded, verify_recovery


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(tree, label, args, cwd=None, env=None):
    log = tree / "build-logs" / (label + ".log")
    log.parent.mkdir(exist_ok=True)
    print(label, flush=True)
    with log.open("w") as output:
        result = subprocess.run(args, cwd=cwd or tree, stdout=output,
                                stderr=subprocess.STDOUT, env=env, check=False)
    if result.returncode:
        print("\n".join(log.read_text(errors="replace").splitlines()[-35:]), flush=True)
        raise RuntimeError(f"{label} failed; see {log}")


def verify_sources(tree, manifest):
    if manifest["baseline"] != BASELINE or manifest["product"] != "Commodore 64 Ultimate":
        raise ValueError("Wrong retail baseline/product")
    for name, expected in manifest["files"].items():
        if digest(tree / name) != expected:
            raise ValueError(f"Prepared source changed: {name}; prepare a new snapshot")


def build(tree, cross, mbedtls_source, mbedtls_build, recovery):
    tree = tree.resolve()
    manifest_path = tree / "c64u-https-source.json"
    source = json.loads(manifest_path.read_text())
    verify_sources(tree, source)
    recovery_data = recovery.read_bytes()
    version = subprocess.check_output([cross + "gcc", "-dumpfullversion"], text=True).strip()
    if version != "11.3.0":
        raise ValueError("Use the retail xPack RISC-V GCC 11.3.0 toolchain")
    sdk = Path(os.environ["IDF_PATH"])
    sdk_version = subprocess.check_output(["git", "-C", str(sdk), "describe", "--tags", "--exact-match"], text=True).strip()
    if sdk_version != "v5.3.6":
        raise ValueError("Use reviewed ESP-IDF v5.3.6")
    # Keep IDF's sdkconfig migration outside the prepared source manifest.
    config = tree / "controller-sdkconfig"
    shutil.copyfile(tree / "software/u64ctrl/sdkconfig", config)
    run(tree, "controller", ["idf.py", "-DSDKCONFIG=" + str(config), "build"], tree / "software/u64ctrl")
    run(tree, "tools", ["make", "-C", "tools", "-j4"])
    for name in ("libs/riscv/lwip", "u64ii/riscv/ultimate", "u64ii/riscv/update"):
        for part in ("output", "result"):
            (tree / "target" / name / part).mkdir(parents=True, exist_ok=True)
    run(tree, "lwip", ["make", "-C", "target/libs/riscv/lwip", "-j4", "CROSS=" + cross])
    run(tree, "management", ["make", "-C", "target/u64ii/riscv/ultimate", "-j4", "CROSS=" + cross])
    run(tree, "portable-tests", ["make", "-C", "software/network/https/tests", "test", "sanitize"])
    run(tree, "uart-rx-tests", ["python3", "software/io/uart/tests/check_rx_rearm.py"])
    run(tree, "menu-state-tests", ["python3", "tools/c64u_https/check_menu_state.py", "--tree", str(tree)])
    run(tree, "menu-page-tests", ["python3", "tools/c64u_https/check_menu_pages.py", "--tree", str(tree)])
    run(tree, "menu-window-tests", ["python3", "tools/c64u_https/check_menu_windows.py", "--tree", str(tree)])
    run(tree, "tcp-deadline-tests", ["python3", "software/network/https/tests/check_tcp_deadlines.py"])
    run(tree, "framing-tests", ["make", "-C", "software/network/https/tests", "framing"])
    run(tree, "uci-tls-tests", ["make", "-C", "target/pc/linux/test_http_target", "test-real-tls",
                              "MBEDTLS_SOURCE=" + str(mbedtls_source), "MBEDTLS_BUILD=" + str(mbedtls_build)])
    run(tree, "uci-removal-tests", ["make", "-C", "target/pc/linux/test_http_target", "test-body-removal"])
    run(tree, "uci-integer-tests", ["make", "-C", "target/pc/linux/test_http_target", "test-integer-widths"])
    run(tree, "uci-routing-tests", ["make", "-C", "target/pc/linux/test_http_target", "test-secure-exchange"])
    run(tree, "updater", ["make", "-C", "target/u64ii/riscv/update", "-j4", "CROSS=" + cross])
    for target in ("ultimate", "update"):
        elf = f"target/u64ii/riscv/{target}/result/{target}.elf"
        run(tree, target + "-architecture", [cross + "readelf", "-A", elf])
        run(tree, target + "-instructions", [cross + "objdump", "-d", "--no-show-raw-insn", elf])
        verify_architecture((tree / f"build-logs/{target}-architecture.log").read_text(),
                            (tree / f"build-logs/{target}-instructions.log").read_text())
    images = ("target/u64ii/riscv/ultimate/result/ultimate.app",
              "target/u64ii/riscv/update/result/update.app",
              "external/u64_mk2_artix.bit", "software/u64ctrl/build/u64ctrl.bin",
              "software/u64ctrl/build/bootloader/bootloader.bin",
              "software/u64ctrl/build/partition_table/partition-table.bin")
    app = tree / images[0]
    if app.stat().st_size > 0x1E0000:
        raise ValueError("Management exceeds retail flash allocation")
    package = tree / images[1]
    data = package.read_bytes()
    # Validate that the actual updater contains exactly the reviewed build
    # inputs, not stale files from another build or the 100T board target.
    verify_embedded(data, {name: (tree / name).read_bytes() for name in (images[0], *images[2:])})
    verify_recovery(recovery_data, (tree / images[2]).read_bytes(), (tree / images[5]).read_bytes())
    if (tree / images[3]).stat().st_size > 0x1F0000:
        raise ValueError("ESP application exceeds retail ROM driver's address span")
    output = tree / "candidate"
    output.mkdir(exist_ok=True)
    shutil.copyfile(package, output / "c64u-1.1.0-https-test.ue2")
    report = {"product": source["product"], "baseline": BASELINE,
              "source_manifest_sha256": digest(manifest_path),
              "compiler": version, "idf": sdk_version,
              "recovery_sha256": digest(recovery), "retail_fpga_matches_recovery": True,
              "partition_table_matches_recovery": True, "rv32i_instruction_check_passed": True,
              "images": {name: {"sha256": digest(tree / name), "bytes": (tree / name).stat().st_size} for name in images},
              "package_sha256": digest(output / "c64u-1.1.0-https-test.ue2"),
              "hardware_tested": False, "installed": False,
              "replaces": ["management", "retail FPGA", "ESP application/bootloader/partition table", "bundled flash files"],
              "configuration_may_reset": True,
              "tests": {p.name: digest(p) for p in sorted((tree / "build-logs").glob("*tests.log"))}}
    (output / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Built review candidate: {output}. No installation or hardware validation performed.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, required=True)
    parser.add_argument("--cross", required=True)
    parser.add_argument("--mbedtls-source", type=Path, required=True)
    parser.add_argument("--mbedtls-build", type=Path, required=True)
    parser.add_argument("--recovery-updater", type=Path, required=True)
    args = parser.parse_args()
    build(args.tree, args.cross, args.mbedtls_source, args.mbedtls_build, args.recovery_updater)


if __name__ == "__main__":
    main()
