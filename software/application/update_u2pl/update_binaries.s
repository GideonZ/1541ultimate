.section ".rodata"
.align 4 # which either means 4 or 2**4 depending on arch!

.global _u2p_ecp5_impl1_bit_start
.type _u2p_ecp5_impl1_bit_start, @object
_u2p_ecp5_impl1_bit_start:
.incbin "u2p_ecp5_impl1.bit"
.global _u2p_ecp5_impl1_bit_end
_u2p_ecp5_impl1_bit_end:

.align 4

.global _ultimate_app_start
.type _ultimate_app_start, @object
_ultimate_app_start:
.incbin "ultimate.app"
.global _ultimate_app_end
_ultimate_app_end:
