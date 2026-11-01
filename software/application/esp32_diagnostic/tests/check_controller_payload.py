"""Cross-check the pinned paired payload's advertised provider version."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[4]
header = (root / "software/u64ctrl/main/rpc_dispatch.h").read_text()
enabled = header.split("#ifdef CONFIG_ULTIMATE_PROVIDER_CONTROL", 1)[1].split("#else", 1)[0]
expected = re.search(r'#define IDENT_STRING "([^"]+)"', enabled).group(1).encode() + b"\0"
payload = Path(sys.argv[1]).read_bytes()
labels = re.findall(rb"ESP32 WiFi Bridge V[0-9]+\.[0-9]+\x00", payload)
if not payload or payload[0] != 0xE9 or labels != [expected]:
    raise SystemExit("Controller payload does not match the selected provider version")
print("Controller version label matches the explicitly selected provider build")
