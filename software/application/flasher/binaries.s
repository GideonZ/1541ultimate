.section ".rodata"
.align 4 # which either means 4 or 2**4 depending on arch!

.global _ultimate_recovery_rbf_start
.type _ultimate_recovery_rbf_start, @object
_ultimate_recovery_rbf_start:
.incbin "ultimate_recovery.rbf"
.global _ultimate_recovery_rbf_end
_ultimate_recovery_rbf_end:

.align 4

.global _ultimate_run_rbf_start
.type _ultimate_run_rbf_start, @object
_ultimate_run_rbf_start:
.incbin "ultimate_run.rbf"
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

.global _recovery_app_start
.type _recovery_app_start, @object
_recovery_app_start:
.incbin "recovery.app"
.global _recovery_app_end
_recovery_app_end:

