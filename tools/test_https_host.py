"""Run the device-free HTTPS regression with retained logs and bounded processes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tls-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    tls = args.tls_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {"complete": False, "hardware_tested": False, "steps": []}
    record = output / "result.json"

    def save():
        record.write_text(json.dumps(report, indent=2) + "\n")

    def run(name, command, cwd=root, timeout=300):
        log = output / (name + ".log")
        with log.open("wb") as stream:
            try:
                code = subprocess.run([str(arg) for arg in command], cwd=cwd,
                                      stdout=stream, stderr=subprocess.STDOUT,
                                      timeout=timeout, check=False).returncode
            except subprocess.TimeoutExpired:
                code = 124
        raw = log.read_bytes()
        sanitizer = bool(re.search(rb"ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:", raw))
        report["steps"].append({"name": name, "command": list(map(str, command)),
                                "returncode": code, "sanitizer_error": sanitizer,
                                "log_sha256": hashlib.sha256(raw).hexdigest()})
        save()
        print(name, code, flush=True)
        if code or sanitizer:
            print(raw.decode(errors="replace")[-5000:])
            raise RuntimeError(name + " failed; evidence retained")

    save()
    run("parser-prepare", [sys.executable, root / "tools/prepare_http_parser.py"])
    run("wifi-replies", ["make", "-C", root / "target/pc/linux/wifireply", "-j1", "test"])
    run("parser-tests", ["make", "-C", root / "software/httpd/c-version/tests",
                         "CXXFLAGS=-std=c++17 -g -fsanitize=address,undefined -fno-sanitize-recover=all", "all"])
    build = output / "tls-build"
    run("tls-configure", ["cmake", "-S", root / "target/pc/linux/test_http_tls",
                          "-B", build, "-G", "Ninja", "-DTLS_STREAM_ROOT=" + str(tls)])
    run("tls-build", ["cmake", "--build", build, "-j4"])
    binaries = ["command_cancellation", "https_routing", "provider_lifecycle", "provider_boot",
                "provider_owner", "provider_control", "provider_manager", "provider_delivery",
                "uart_loss", "provider_flash", "provider_provisioning", "owner_http",
                "provider_data", "provider_task", "provider_runner"]
    for name in binaries:
        run(name, [build / ("test_" + name)], timeout=40)
    run("provider_boot-zero", [build / "test_provider_boot", "--zero"])
    for mode in ("", "allocation", "setter"):
        run("sources-" + (mode or "normal"), [build / "test_provider_sources"] + ([mode] if mode else []))
    for mode in ("", "install", "existing", "missing", "late", "early", "corrupt", "foreign",
                 "task", "bind", "target", "delayed", "no-tls", "no-identity", "operator-install",
                 "operator-occupied", "operator-damaged", "operator-fault", "operator-stop", "operator-changed"):
        run("startup-" + (mode or "normal"), [build / "test_provider_startup"] + ([mode] if mode else []))
    run("startup-disabled", [build / "test_provider_startup_disabled", "disabled"])
    for kind in ("control", "tls"):
        for mode in ("ok", "boot", "queue", "task"):
            run(kind + "-start-" + mode, [build / ("test_provider_" + kind + "_start"), mode])
    peer = [sys.executable, root / "software/network/tests/run_http_tls.py",
            "--binary", build / "test_http_tls", "--tls-root", tls]
    for mode in ("", "routing", "owner", "worker", "connected", "uart", "bound"):
        for profile in ("rsa", "ecdsa"):
            name = (mode or "adapter") + "-" + profile
            run(name, peer + ["--output", output / name] + (["--" + mode] if mode else [])
                + (["--ecdsa"] if profile == "ecdsa" else []), timeout=180)
    flags = "-fsanitize=address,undefined -fno-sanitize-recover=all"
    target = root / "target/pc/linux/test_http_target"
    run("http-build", ["make", "-j1", "CC=gcc " + flags, "CPP=g++ " + flags, "all"], cwd=target)
    for mode in ("", "framing", "transport", "integer-widths", "body-removal", "connect-cleanup",
                 "response-completion", "response-boundaries", "response-cleanup"):
        run("http-" + (mode or "general"), [target / "result/test_http"] + (["--" + mode] if mode else []), cwd=target, timeout=40)
    model = output / "session-build"
    run("model-configure", ["cmake", "-S", root / "target/pc/linux/test_http_session", "-B", model])
    run("model-build", ["cmake", "--build", model, "-j4"])
    run("model", [model / "test_http_session"])
    run("assembly", ["make", "-C", root / "target/pc/linux/test_assembly", "-j1", "test"])
    report["complete"] = True
    save()


if __name__ == "__main__":
    main()
