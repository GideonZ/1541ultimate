# Commodore 64 BASIC and KERNAL Source

This repository contains the Commodore 64 BASIC and KERNAL source in a format that is easy to edit and can be built using modern tools on modern systems. It is derived from the [original sources](https://www.github.com/mist64/cbmsrc), with all original symbols and comments intact.

## Building

* Requires
	* [cc65](https://github.com/cc65/cc65).
	* make, Python, crc32
* Use `make` to build.
* The resulting files are
	* `basic.bin` (`$A000`-`$BFFF`): identical with basic.901226-01.bin
	* `kernal.bin` (`$E000`-`$FFFF`): identical with kernal.901227-03.bin

## Modifying

The major parts of KERNAL reside in their own segments that will always be linked to their original addresses, so if you want to remove tape or RS232 support, for example, the other sections will still remain where they should be in the image.

## Checksums

Commodore built all ROMs so that the 8 bit checksum matches the upper 8 bits of the location in the address space, e.g. BASIC is located at `$A000`, so its checksum has to be `$A0`. There is one "checksum adjust byte" at a dedicated location in every ROM that is updated after the build to cause the correct checksum. In BASIC, this is at `$BF52` (`CKSMA0`) and in KERNAL, it's at `$E4AC`.

The algorithm looks like this:

	10 A=0:C=0
	20 FOR I=16384 TO 24575
	30 B=PEEK(I)
	40 A=A+B+C:C=0
	50 IF A>255 THEN A=A-256:C=1
	60 NEXT
	70 PRINTA

Starting in 1983 though, they started using a slightly different algorithm (that adds the final carry) with _most_ ROMs:

	65 A=A+C

The only version of C64 BASIC is from 1982 and uses the old checksum. The -03 version of the KERNAL is from 1983 and uses the new checksum.

Any ROMs after 1983 (so also all ROMs we create today) should be checksummed using the new algorithm. The Makefile will use the old algorithm on BASIC if it's unchanged from the 901226-01 version, otherwise it will use the new algorithm. The KERNAL checksum will always use the new algorithm.

## Credits

This version is maintained by Michael Steil <mist64@mac.com>, [www.pagetable.com](https://www.pagetable.com/)
