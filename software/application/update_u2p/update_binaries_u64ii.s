.section ".rodata"
.align 4 # which either means 4 or 2**4 depending on arch!

.global _u64e2_50t_swp_start
.type _u64e2_50t_swp_start, @object
_u64e2_50t_swp_start:
.incbin "u64e2_50t.swp"
.global _u64e2_50t_swp_end
_u64e2_50t_swp_end:

.align 4

.global _u64e2_100t_swp_start
.type _u64e2_100t_swp_start, @object
_u64e2_100t_swp_start:
.incbin "u64e2_100t.swp"
.global _u64e2_100t_swp_end
_u64e2_100t_swp_end:

.align 4
.global _ultimate_app_start
.type _ultimate_app_start, @object
_ultimate_app_start:
.incbin "ultimate.app"
.global _ultimate_app_end
_ultimate_app_end:
