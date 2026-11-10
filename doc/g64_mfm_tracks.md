# MFM tracks in G64 and G71 images

This describes how the Ultimate firmware stores MFM tracks inside a G64 or G71 image, as it does today, so that other programs can read and write such images. It is not an official extension of the G64 format: VICE and most other G64 readers do not know it and reject or misread these tracks.

Two implementations use it:

- the Ultimate firmware (1541 Ultimate II+, II+L, Ultimate 64), since the 1571's WD177x was added in June 2021: `software/drive/disk_image.cc`, `c1541.cc`, `mfmdisk.cc`, `wd177x.cc`;
- Denise, as "G64 (U-II+/U64 compatibility mode)": `emulation/libc64/disk/structure/gxx.cpp` (`parseMfm`, `writeMfm`).

An MFM track holds decoded sectors, not an MFM bitstream: sector IDs and sector data, without gaps, sync marks or CRCs. A reader that needs the bitstream has to generate it, as Denise does.

The firmware behaviour below was checked on an Ultimate II+L running 3.15a, with the 1571's own burst commands (format, inquire, read) on crafted images.

## Container

The image is a normal G64:

| Offset | Size | Content |
|---|---|---|
| 0 | 8 | `GCR-1541` (one side, 84 table entries) or `GCR-1571` (two sides, 168 entries) |
| 8 | 1 | version, 0 |
| 9 | 1 | number of table entries: 84 for `GCR-1541`, 168 for `GCR-1571` |
| 10 | 2 | maximum track length, little-endian |
| 12 | 4 × entries | track offsets, little-endian, 0 = track absent |
| 12 + 4 × entries | 4 × entries | speed zones |

Both programs write version 0, 84 or 168 entries, and 7928 (`$1EF8`) as the maximum track length. Denise rejects a version other than 0 or an entry count above 84 or 168, skips tracks longer than the header's maximum, and refuses to write one. The firmware takes the entry count from the signature and accepts no track longer than 7928 bytes, whatever the header says. It reads at most 635,932 bytes of the file; tracks behind that are treated as absent.

**Table entries.** Each entry is a head position in half-track steps, the position the drive's stepper has reached: entry `p` (0-83) is side 0, entry `84 + p` is side 1. Both programs index the table this way. It is not the track number in the sector IDs, which can be anything. The 1571 DOS formats MFM cylinder `c` at the position of GCR track `c + 1`, so a disk formatted by it has cylinder `c` in entry `2c` on side 0 and `84 + 2c` on side 1; other software can format at other positions. A G64 holds side 0 only; MFM tracks on side 1 need a G71.

**Track placement.** Both programs find a track through its offset only, and both drop a track unless the file contains all of its bytes. They differ in how they lay tracks out:

- The firmware writes a track back in place. When it saves a whole image, it packs the tracks back to back, each directly behind the previous one's last byte.
- Denise gives every track a slot of 2 + maximum track length bytes. Writing a track, it fills the rest of that slot with zeros, and it appends a new track at the end of the file.

So Denise writing into a packed image overwrites the start of the track behind a short one. Writers that want both programs to handle their images lay tracks out `2 + maximum track length` bytes apart.

GCR and MFM tracks can be mixed in one image. A track becomes MFM when the drive formats it with the WD177x, and GCR again when the drive writes it in GCR.

## Track

At the track offset, as for any G64 track, comes a 16-bit little-endian length word, then the track bytes.

**Length word.** Bit 15 set marks the track as MFM. Bits 0-14 are the track length, read as one 15-bit number by both programs (firmware releases up to 3.15a masked it with `$3FFF`; GideonZ/1541ultimate#840 changed that to `$7FFF`).

- Both programs write back the length they loaded, with bit 15 set: a track loaded with length 7000 and formatted as MFM is written as `$9B58`.
- For a track the image did not contain, the firmware uses the GCR length of that track's speed zone, 6250 to 7692 bytes (cylinder 1, for example, comes out as `$9E0C`).
- Denise uses the length as the size of one revolution: it generates the MFM bitstream into that many bytes. 6250 bytes is one revolution at 250 kbit/s and 300 rpm; a shorter length leaves less room for sectors (see below), a longer one gives a slower disk.
- The firmware needs the sector data to fit into the length minus 162 bytes.

**Speed zone.** Not used for an MFM track. Writers leave whatever the entry had; readers ignore it.

**Track bytes:**

| Offset | Size | Content |
|---|---|---|
| 0 | 1 | number of sectors `n` (readers use at most 32) |
| 1 | 1 | version, 0 |
| 2 | 32 × 5 | sector list: the first `n` entries are used |
| 162 | | sector data |

Each entry of the sector list is the sector's ID field, as the WD177x reads it, plus one byte:

| Byte | Content |
|---|---|
| 0 | track (C) |
| 1 | side (H) |
| 2 | sector (R) |
| 3 | size code (N): 0 = 128, 1 = 256, 2 = 512, 3 = 1024 bytes |
| 4 | error byte, see below |

Both programs write version 0 and ignore it when reading. Entries behind the first `n` are ignored by both; the firmware writes them as zeros, Denise leaves them as they were.

The list is in physical order: the order in which the sectors pass the head, starting at the index hole. The 1571 DOS lays sectors out with a step of interleave + 1, so its burst format with interleave 1 and nine sectors gives `1,6,2,7,3,8,4,9,5`, and interleave 0 gives `1` to `9`.

The sector data starts at offset 162, always, whatever `n` is. The sectors follow one another in list order, each `128 << N` bytes long, without gaps, except for a sector without a data field (error byte bit 2, below). The bytes behind the last sector, up to the end of the track, have no meaning; the firmware leaves them as they were.

## Error byte

| Bit | Meaning in Denise |
|---|---|
| 0 | the ID field has a CRC error |
| 1 | the data field has a CRC error |
| 2 | the sector has no data field; no data bytes are stored for it |
| 4 | the data field has a deleted data mark (`$F8`, or `$F9` when Denise reads it from a bitstream) |

The firmware writes 0 and does not look at the byte when it reads.

This matters for bit 2. Denise stores no data for such a sector, so the data of all following sectors moves up. The firmware counts `128 << N` bytes for every entry, so it reads the sectors after one with bit 2 set from the wrong place; it also reads the sector with bit 2 itself as if it had data. A writer that wants the firmware to read its images should not produce sectors without a data field.

## What each implementation does beyond the format

These are properties of the two programs, not of the format, but they decide whether an image works.

**Firmware, formatting (WD177x Write Track):**

- It records only sectors with a normal data mark (`$FB`), and at most 5120 data bytes per track. Sectors beyond that are dropped silently: a 1571 format of 11 × 512 bytes ends with drive error 24, but the track is stored with the first 10.
- It can store an MFM track only where the image has a track, or where it keeps a blank one: even entries 0-78 (cylinders 0-39), on side 1 only if the image is already double-sided. A format anywhere else is lost.
- A track the file contained is written back in place. A track the file did not contain stays in memory until the whole image is saved (*Save as GCR*), which packs all tracks and, with *GCR Save Align Tracks* on, tries to rotate every track, MFM ones included, to start at a GCR sector 0.
- A GCR write marks the track as GCR again, as a 1571 `N:` format after an MFM format shows.

**Firmware, reading:**

- The sector list is read when the image is mounted.
- A sector is found by its track and sector number (C and R); the side byte H is not compared, and the first matching entry wins.
- A size code above 3 ends the list at that entry: that sector and all after it are not found.

**Denise, reading:**

- It generates the bitstream with standard gaps into the first 6250 bytes of the track, or fewer if the length is shorter. At the first sector that does not fit, it stops: that sector and all after it are missing. A sector with bit 2 takes the space of its data anyway.
- It uses size codes modulo 4.

**Denise, writing:**

- It decodes the sectors back from its bitstream, scanning the first 6250 bytes from the index, and sets the error bits from what it finds. It stores the size code as found, so a size code above 3 can end up in an image.

## Example

A track formatted on an Ultimate II+L by the 1571's own burst `FORMAT MFM`, for PC 360K (both sides, nine 512-byte sectors starting at 1, fill `$E5`, interleave 1), cylinder 0 side 0, in entry 0 of an image whose track lengths were 7928:

```
length word   F8 9E                 bit 15 + 7928
offset   0    09 00                 9 sectors, version 0
offset   2    00 00 01 02 00        C=0 H=0 R=1 N=2 (512), no error
offset   7    00 00 06 02 00        R=6
...           (sectors 2, 7, 3, 8, 4, 9, 5)
offset  47    00 ...                entries 10-32, zero
offset 162    E5 × 4608             data of R=1, R=6, R=2, ... in list order
offset 4770   ...                   rest of the track, as it was before
```
