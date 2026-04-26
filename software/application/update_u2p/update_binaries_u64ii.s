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
