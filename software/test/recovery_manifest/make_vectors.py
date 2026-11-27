#!/usr/bin/env python3
"""Writes the vectors that recovery_manifest_test.c checks, with manifest.py from
the recovery server repository, whose test_manifest.py checks the same vectors:
together they prove the board's reader and the publisher's writer agree. Copy
the result into that repository's test_vectors/ as well.

    RECOVERY_SERVER=~/proj/ult64/recovery-server \
        python3 software/test/recovery_manifest/make_vectors.py

Only needed when the format changes. The keys made here are for the tests and
must never be trusted by firmware.
"""

import fnmatch
import os
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, os.path.expanduser(os.environ.get("RECOVERY_SERVER", str(HERE.parents[3] / "recovery-server"))))

import manifest  # noqa: E402

VECTORS = HERE / "vectors"

# A bitstream needs only its IDCODE write and a START near its end for the
# signer; see bitstream_idcode() and check_start_command().
FAKE_BITSTREAM = (b"\xff" * 16 + bytes.fromhex("aa995566") + bytes.fromhex("20000000") +
                  bytes.fromhex("30018001") + bytes.fromhex("0362c093") + b"\x20\x00\x00\x00" * 8 +
                  bytes.fromhex("30008001 00000005") + b"\x20\x00\x00\x00" * 4)
FAKE_IMAGE = b"\x00\x00\x03\x00\x10\x00\x00\x00\x00\x00\x03\x00" + b"\x13\x00\x00\x00" * 4

# (pattern, serial). The expected result is whatever fnmatch.fnmatchcase says,
# which is what the server matches with.
GLOB_CASES = [
    ("*", "U64E2-0012345"), ("*", ""), ("**", "x"),
    ("U64E2-*", "U64E2-0012345"), ("U64E2-*", "C64U-10001"), ("u64e2-*", "U64E2-1"),
    ("U64E2-00*", "U64E2-0012345"), ("NOSERIAL", "NOSERIAL"), ("NOSERIAL", "NOSERIAL2"),
    ("C64U-1????", "C64U-12345"), ("C64U-1????", "C64U-1234"), ("C64U-1????", "C64U-123456"),
    ("C64U-2[0-4]???", "C64U-23999"), ("C64U-2[0-4]???", "C64U-25000"),
    ("?", ""), ("?", "a"), ("a?c", "abc"), ("a?c", "ac"),
    ("[!C]*", "U64E2-1"), ("[!C]*", "C64U-1"),
    ("[]]x", "]x"), ("[!]]x", "ax"), ("[!]]x", "]x"),
    ("[", "["), ("[abc", "[abc"), ("a[", "a["), ("[!]", "[!]"),
    ("[z-a]", "a"), ("[!z-a]", "q"), ("[z-ab]", "b"), ("[z-ab]", "y"),
    ("[a-c-e]", "-"), ("[a-c-e]", "d"), ("[a-c-e]", "e"), ("[a-]", "-"), ("[-a]", "-"),
    ("[^a]", "^"), ("[^a]", "b"), ("\\*", "\\abc"), ("[\\]", "\\"),
    ("*a*b", "xaYb"), ("*a*b", "xaYbc"), ("a*b*c*", "abc"), ("*-*-*", "C64U-CP-1"),
    ("*a*a*a*a*b", "a" * 40),
]


def main():
    VECTORS.mkdir(exist_ok=True)
    for name in ("test_key", "other_key"):
        for suffix in (".pem", ".pub.pem"):
            (VECTORS / (name + suffix)).unlink(missing_ok=True)
        manifest.keygen(str(VECTORS / name), None)
    # Only the public half of the second key is needed: the key that did not sign.
    (VECTORS / "other_key.pem").unlink()

    (VECTORS / "fake.bit").write_bytes(FAKE_BITSTREAM)
    (VECTORS / "fake.ue2").write_bytes(FAKE_IMAGE)
    key = manifest.load_private_key(VECTORS / "test_key.pem")
    for out, serial in (("valid.manifest", "U64E2-1*"), ("any_serial.manifest", None)):
        body = manifest.build("test-1.0", VECTORS / "fake.bit", VECTORS / "fake.ue2", serial)
        (VECTORS / out).write_bytes(manifest.sign(body, key))

    with open(VECTORS / "serial_patterns.txt", "w") as f:
        f.write("# expected<TAB>pattern<TAB>serial, expected from fnmatch.fnmatchcase\n")
        for pattern, serial in GLOB_CASES:
            f.write("%d\t%s\t%s\n" % (fnmatch.fnmatchcase(serial, pattern), pattern, serial))
    print("wrote", VECTORS)


if __name__ == "__main__":
    main()
