### Software IEC 

**Introduction**
This directory contains a test case for the emulated IEC disk drive, called SoftwareIEC.
SoftwareIEC implements all of the commands that a commodore diskdrive supports, including
many that the SD2IEC / CMDHD devices specified.

The virtual IEC disk drive access the virtual file system of the Ultimate directly, so it
has access to all file systems and mount points that are defined within the ultimate virtual
file system. This means that it is possible to access CBM type of disk images as well,
when they are mounted within the virtual file system. Internally, the virtual file system
uses a PC-like naming structure, even for files inside of a CBM disk image.
Because CBM filenames are inherently different from filenames in FAT and other PC-like
filesystems, a strict filename conversion strategy is implemented:

Files within a CBM disk image have their filenames converted to "PC" format in the following
manner:

### `petscii_to_fat*` – Filename Conversion (IEC Compatibility)

**Definition / Properties:**

* Maps `(filename, type)` pairs from C64 PETSCII representation to a FAT-compatible string.
* **Injective**: every unique `(filename, type)` produces a unique FAT string. Reversibility is preserved; decoding back to original PETSCII and type is always possible.
* **Escape logic**:

  * Characters outside 32–95, or `: / \ " < >`, or a leading dot, are encoded in `{XX}` hexadecimal notation.
  * Consecutive problem characters are grouped into a single `{…}` sequence.
  * Non-problem characters are kept as-is; uppercase letters are normalized to lowercase, or kept at uppercase.
* **Reserved names**: FAT-reserved names (`CON`, `PRN`, `AUX`, `NUL`, `COM1`…`LPT9`) are prefixed  with `{}` (empty hex word) to avoid conflicts on Windows systems.
* **Type-endings**: common C64 file types (`.prg`, `.seq`, `.usr`, `.rel`) plus `.del` (C# library only) append `{}` (empty hex word) to distinguish identical filenames with different types at the end and therefore preserve injectivity. 
* **Whitespace / dots / NBSP**: encoded for improved visual readability; ensures typical C64 filenames remain clear and distinct.

- - Dots are escaped only if they appear as the first character
- - Spaces are not escaped unless they're the last character of the filename (excluding type)
- - The extension that is based on the type is attached to the converted filename

### Use cases
There are various use cases in which filename conversion will play a role:
1) SoftwareIEC -> FAT 
2) SoftwareIEC -> CBM Disk image
3) FAT -> CBM Disk Image

In the first case, the file name that enters the system is in PETSCII format. In order to access files on a FAT / PC-like file system, the petscii_to_fat function will give unique names to use
within the filesystem. This works, because this conversion is consistent. However, files that are not strictly stored in the converted format will not be accessible. This is a problem. In order
to mitigate this, we may need to implement some heuristics, e.g. accessing the file with or without extension.

In the second case, the file name that enters the system is in PETSCII format. In order to separate concerns, the petscii_to_fat function will also be applied. This is not a problem, because
the CBM file system module calls the same function when reading its directory; the resulting filenames can therefore be compared.

In the third case, when FAT wants to access files inside of a CBM disk image, the file open call of this file system will receive a FAT name. As in the second case, this will work as the
CBM file system reports its files in FAT format. The only 'drawback' is that for FAT access, the filenames will look somewhat 'mangled' due to the conversion. Some people hate it, but I'd say
it is the consequence of a consistent design choice.

**Injectivity vs Accessibility**
While the filename conversion is made fully injective from the PETSCII side of things, there are many valid FAT names that cannot be reached by the forward conversion. For example:
- - Long FAT names that are truncated to 16 chars when shown as IEC names
- - Names that differ only beyond the 16-char IEC display limit
- - FAT names with non-CBM extensions: JOHN.DOE, README.TXT, FILE.BIN
- - Filenames without extensions, e.g. "MYPROGRAM"
- - FAT names that contain Unicode/non-ASCII characters
- - FAT names with {XX} text that looks like an escape: A{41}B.prg.
- - Names where IEC read defaults append .???, causing FOO.BAR to become lookup pattern FOO.BAR.???.
- - Directories with unreachable names, because CD uses the same conversion idea.

Atlhough these filenames will never be created through the interface, these files can already exist on a user's disk. Not being able to access any of these files may create usability issues.
This paragraph defines how this is addressed.

The largest issue is when opening files for reading, for example when loading a program, or accessing a file with a data channel. Obviously for writing files there is no issue, as new compliant
filenames are generated. Replacing files may simply use the new format for the replacement file, if the compliant filename gets priority when the file is reopened after replacing.
For disk commands the need might be less urgent, but we may make the solution as orthogonal as possible. There are two ways to approach this:
1) try more than one conversion method
2) use heuristics.

Note that especially the latter is dangerous for the scratch command, as files may be deleted that were not supposed to be targeted.


### Implementing files
The files that implement the IEC commands and file access are implemented in the files found in the directory /software/io/iec, from the root of the repository.


