.section ".rodata"
.align 4 # which either means 4 or 2**4 depending on arch!

.global _u2p_carttester_rbf_start
.type _u2p_carttester_rbf_start, @object
_u2p_carttester_rbf_start:
.incbin "u2p_carttester.swp"
.global _u2p_carttester_rbf_end
_u2p_carttester_rbf_end:

.align 4
