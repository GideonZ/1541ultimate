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
.global _rom_pack_end
_rom_pack_end:

#	{ FLASH_ID_AR5PAL,     0x00, 0x380000, 0x380000, 0x08000 },
#	{ FLASH_ID_AR6PAL,     0x00, 0x388000, 0x388000, 0x08000 },
#	{ FLASH_ID_FINAL3,     0x00, 0x390000, 0x390000, 0x10000 },
#	{ FLASH_ID_RR38PAL,    0x00, 0x3A0000, 0x3A0000, 0x10000 },
#	{ FLASH_ID_SS5PAL,     0x00, 0x3B0000, 0x3B0000, 0x10000 },
#	{ FLASH_ID_KCS,        0x00, 0x3C0000, 0x3C0000, 0x04000 },
#	{ FLASH_ID_EPYX,       0x00, 0x3C4000, 0x3C4000, 0x02000 },
#	{ FLASH_ID_KERNAL_ROM, 0x00, 0x3C6000, 0x3C6000, 0x02000 },
#	{ FLASH_ID_CUSTOM_DRV, 0x00, 0x3C8000, 0x3C8000, 0x08000 },
#	{ FLASH_ID_CUSTOM_ROM, 0x00, 0x3D0000, 0x3D0000, 0x20000 }, // max size: 128K
#
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
