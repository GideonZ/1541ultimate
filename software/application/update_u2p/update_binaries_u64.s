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
.incbin "kcs.bin"
.incbin "epyx.bin"
.incbin "kernal.901227-03.bin"
.incbin "basic.901226-01.bin"
.incbin "characters.901225-01.bin"


.global _rom_pack_end
_rom_pack_end:


#    { FLASH_ID_AR5PAL,     0x00, 0x400000, 0x400000, 0x08000 },
#    { FLASH_ID_AR6PAL,     0x00, 0x408000, 0x408000, 0x08000 },
#    { FLASH_ID_FINAL3,     0x00, 0x410000, 0x410000, 0x10000 },
#    { FLASH_ID_RR38PAL,    0x00, 0x420000, 0x420000, 0x10000 },
#    { FLASH_ID_SS5PAL,     0x00, 0x430000, 0x430000, 0x10000 },
#    { FLASH_ID_KCS,        0x00, 0x440000, 0x440000, 0x04000 },
#    { FLASH_ID_EPYX,       0x00, 0x444000, 0x444000, 0x02000 },
#    { FLASH_ID_ORIG_KERNAL,0x00, 0x446000, 0x446000, 0x02000 },
#    { FLASH_ID_KERNAL_ROM, 0x00, 0x448000, 0x448000, 0x02000 },
#    { FLASH_ID_CUSTOM_DRV, 0x00, 0x450000, 0x450000, 0x08000 },
#    { FLASH_ID_CUSTOM_ROM, 0x00, 0x458000, 0x458000, 0x28000 }, // max size: 128K
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
