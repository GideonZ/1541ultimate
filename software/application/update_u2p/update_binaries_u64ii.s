.section ".rodata"
.align 4 # which either means 4 or 2**4 depending on arch!

# UPDATE_FPGA_TYPE comes from the Makefile: 0 carries both images, 2 only the
# XC7A50T one and 3 only the XC7A100T one.
.if UPDATE_FPGA_TYPE != 3
.global _u64e2_50t_swp_start
.type _u64e2_50t_swp_start, @object
_u64e2_50t_swp_start:
.incbin "u64e2_50t.swp"
.global _u64e2_50t_swp_end
_u64e2_50t_swp_end:
.endif

.align 4

.if UPDATE_FPGA_TYPE != 2
.global _u64e2_100t_swp_start
.type _u64e2_100t_swp_start, @object
_u64e2_100t_swp_start:
.incbin "u64e2_100t.swp"
.global _u64e2_100t_swp_end
_u64e2_100t_swp_end:
.endif

.align 4
.global _ultimate_app_start
.type _ultimate_app_start, @object
_ultimate_app_start:
.incbin "ultimate.app"
.global _ultimate_app_end
_ultimate_app_end:
