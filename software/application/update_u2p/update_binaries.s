.section ".rodata"
.align 4 # which either means 4 or 2**4 depending on arch!

.global _ultimate_run_rbf_start
.type _ultimate_run_rbf_start, @object
_ultimate_run_rbf_start:
.incbin "ultimate_run.swp"
.global _ultimate_run_rbf_end
_ultimate_run_rbf_end:

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
.incbin "rr38ntsc.bin"
.incbin "tar_pal.bin"
.incbin "tar_ntsc.bin"
.incbin "ss5pal.bin"
.incbin "ss5ntsc.bin"
.incbin "ar5ntsc.bin"
.incbin "kcs.bin"
.incbin "epyx.bin"
.global _rom_pack_end
_rom_pack_end:
