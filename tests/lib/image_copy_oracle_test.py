#!/usr/bin/env python3
"""The image-copy E2E oracle must reject corrupt directories, chains and BAMs."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e" / "filemanager"))
from image_copy_test import Failure, PAYLOADS, image_files, sector_offset, source_image
from report import suite_fail, suite_ok


class ImageCopyOracleTest(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(image_files(source_image()), PAYLOADS)

    def test_corruption(self):
        directory = sector_offset(18, 1)
        first = sector_offset(17, 0)
        bam = sector_offset(18, 0) + 17 * 4
        corruptions = {
            "directory cycle": (directory, bytes([18, 1])),
            "directory overlaps file": (directory, bytes([17, 0])),
            "duplicate name": (directory + 32 + 5, b"DREHER".ljust(16, b"\xa0")),
            "cross-linked files": (directory + 32 + 3, bytes([17, 0])),
            "file cycle": (first, bytes([17, 0])),
            "track zero sector count": (first, bytes([0, 0])),
            "track out of range": (directory + 3, bytes([36, 0])),
            "sector out of range": (directory + 3, bytes([17, 21])),
            "incorrect BAM count": (bam, bytes([21])),
            "allocated sector marked free": (bam + 1, bytes([0xff])),
        }
        for label, (offset, data) in corruptions.items():
            with self.subTest(label=label):
                image = bytearray(source_image())
                image[offset:offset + len(data)] = data
                with self.assertRaises(Failure):
                    image_files(image)

    def test_allocated_sectors_marked_free_with_consistent_counts(self):
        for track, sector in ((17, 0), (18, 1)):
            with self.subTest(track=track, sector=sector):
                image = bytearray(source_image())
                bam = sector_offset(18, 0) + track * 4
                image[bam] += 1
                image[bam + 1 + sector // 8] |= 1 << (sector % 8)
                with self.assertRaisesRegex(Failure, "marked free in BAM"):
                    image_files(image)

    def test_truncated(self):
        with self.assertRaises(Failure):
            image_files(source_image()[:-1])


if __name__ == "__main__":
    result = unittest.main(exit=False).result
    if result.wasSuccessful():
        suite_ok("image_copy_oracle_test")
    else:
        suite_fail("image_copy_oracle_test", "verifier regressions failed")
    raise SystemExit(not result.wasSuccessful())
