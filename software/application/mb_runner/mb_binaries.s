.section ".rodata"
.align 4 # which either means 4 or 2**4 depending on arch!

.global _ultimate_bin_start
.type _ultimate_bin_start, @object
_ultimate_bin_start:
.incbin "ultimate.bin"
.global _ultimate_bin_end
_ultimate_bin_end:

.align 4

.global _jumper_bin_start
.type _jumper_bin_start, @object
_jumper_bin_start:
.incbin "mb_boot.bin"
.global _jumper_bin_end
_jumper_bin_end:

