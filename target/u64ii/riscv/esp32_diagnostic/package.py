"""Single RAM segment for the installed C64U update-file loader; no flash payload."""
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
assert 0 < len(data) < 65536
pathlib.Path(sys.argv[2]).write_bytes(struct.pack('<III', 0x03000000, len(data), 0x03000000) + data)
