.section ".rodata"
.align 4 # which either means 4 or 2**4 depending on arch!

.global _u64_rbf_start
.type _u64_rbf_start, @object
_u64_rbf_start:
.incbin "u64.swp"
.global _u64_rbf_end
_u64_rbf_end:

.align 4

.global _ultimate_app_start
.type _ultimate_app_start, @object
_ultimate_app_start:
.incbin "ultimate.app"
.global _ultimate_app_end
_ultimate_app_end:

.align 4
.global _rom_pack_start
.type _rom_pack_start, @object
_rom_pack_start:
.incbin "ar5pal.bin"
.incbin "ar6pal.bin"
.incbin "final3.bin"
.incbin "rr38pal.bin"
.incbin "ss5pal.bin"
.incbin "tar_pal.bin"
.incbin "kcs.bin"
.incbin "epyx.bin"

#.incbin "characters.901225-01.bin"
#.incbin "kernal.901227-03.bin"
#.incbin "basic.901226-01.bin"

.global _rom_pack_end
_rom_pack_end:


#.align 4
#
#.global _recovery_app_start
#.type _recovery_app_start, @object
#_recovery_app_start:
#.incbin "recovery.app"
#.global _recovery_app_end
#_recovery_app_end:
#
#.align 4
#
#.global _ultimate_recovery_rbf_start
#.type _ultimate_recovery_rbf_start, @object
#_ultimate_recovery_rbf_start:
#.incbin "ultimate_recovery.swp"
#.global _ultimate_recovery_rbf_end
#_ultimate_recovery_rbf_end:
