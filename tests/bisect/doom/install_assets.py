#!/usr/bin/env python3
"""Download the published Doom C64U release and put game.reu on the device.

The bisection harness reloads the REU image on every launch, and doing that
from a file already on the device is far quicker than uploading 8 MB each time,
so the image is placed once over FTP. launcher.prg stays local: doom_run.py
uploads it with the request that starts it.

    ./install_assets.py --host u64 --dir /USB2/doom
"""
import argparse
import json
import os
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "lib"))
import ftp as ftp_lib                                              # noqa: E402
from rest import retrying_urlopen                                  # noqa: E402

RELEASE_API = "https://api.github.com/repos/slesinger/doom/releases/latest"
ASSETS = ("game.reu", "launcher.prg")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="u64")
    ap.add_argument("-p", "--password", default=os.environ.get("U64_PASS", ""),
                    help="device FTP password; the shared default is used when "
                         "this is empty")
    ap.add_argument("--dir", default="/USB2/doom",
                    help="device directory for game.reu; the USB volume is "
                         "named differently on different machines, so check a "
                         "listing first")
    ap.add_argument("--cache", default=os.path.expanduser("~/.cache/doom-bisect/assets"))
    ap.add_argument("--timeout", type=float, default=60.0)
    a = ap.parse_args()

    with retrying_urlopen(urllib.request.Request(RELEASE_API), a.timeout,
                          idempotent=True) as resp:
        release = json.load(resp)
    tag = release.get("tag_name", "unknown")
    urls = {x["name"]: x["browser_download_url"] for x in release.get("assets", [])}
    missing = [n for n in ASSETS if n not in urls]
    if missing:
        print(f"release {tag} has no {', '.join(missing)}", file=sys.stderr)
        return 1

    cache = Path(a.cache)
    cache.mkdir(parents=True, exist_ok=True)
    local = {}
    for name in ASSETS:
        path = cache / name
        if not path.exists() or path.stat().st_size == 0:
            with retrying_urlopen(urllib.request.Request(urls[name]), a.timeout,
                                  idempotent=True) as resp:
                path.write_bytes(resp.read())
        local[name] = path
        print(f"{tag}: {name} {path.stat().st_size} bytes -> {path}")

    # tests/lib/ftp.py already resolves the target's FTP port, knows the shared
    # credentials, and closes the session on any path: an 8 MB upload that fails
    # would otherwise hold one of the device's four FTP slots for 300 seconds.
    with ftp_lib.session(a.host, a.password or None, timeout=a.timeout) as client:
        ftp_lib.make_dir(client, a.dir)
        ftp_lib.store(client, f"{a.dir}/game.reu", local["game.reu"].read_bytes())
    print(f"uploaded game.reu to {a.dir} on {a.host}")
    print(f"launcher.prg stays local: pass --prg {local['launcher.prg']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
