"""Apply the reviewed response-framing patch to the public HTTP submodule pin."""
import hashlib
import json
from pathlib import Path
import subprocess


def prepare(root):
    root = Path(root).resolve()
    directory = root / "software/httpd"
    patch = root / "patches/http-response-framing.patch"
    manifest = json.loads((patch.with_suffix(".json")).read_text())
    digest = lambda data: hashlib.sha256(data).hexdigest()
    if digest(patch.read_bytes()) != manifest["patch_sha256"]:
        raise RuntimeError("HTTP parser patch has changed")
    actual = {name: digest((directory / name).read_bytes().replace(b"\r\n", b"\n"))
              if (directory / name).exists() else None for name in manifest["before"]}
    if actual == manifest["after"]:
        print("HTTP response-framing patch already verified")
        return
    # A reused submodule checkout restores tracked files but can leave files
    # created by our patch. Keep only additions with the exact reviewed content;
    # every tracked input must still match the public pin before applying it.
    retained = []
    for name, expected in manifest["before"].items():
        if actual[name] == expected:
            continue
        if expected is None and actual[name] == manifest["after"][name]:
            retained.append("--exclude=software/httpd/" + name)
            continue
        raise RuntimeError("HTTP parser differs from the public pin at " + name +
                           "; preserve and inspect local changes")
    # Git for Windows may check these text files out as CRLF. Normalize only
    # after their complete contents have matched the reviewed source hashes.
    for name, value in manifest["before"].items():
        if value is not None:
            path = directory / name
            path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n"))
    # Run from the firmware root: Git otherwise interprets paths differently
    # when this directory has no submodule metadata (for example a source export).
    command = ["git", "apply", "--directory=software/httpd", *retained, str(patch)]
    subprocess.run(command + ["--check"], cwd=root, check=True)
    subprocess.run(command, cwd=root, check=True)
    if any(digest((directory / name).read_bytes().replace(b"\r\n", b"\n")) != value
           for name, value in manifest["after"].items()):
        raise RuntimeError("HTTP parser result hash mismatch")
    print("HTTP response-framing patch applied and verified")


if __name__ == "__main__":
    prepare(Path(__file__).resolve().parents[1])
