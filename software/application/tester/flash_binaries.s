.section ".rodata"
.align 4 # which either means 4 or 2**4 depending on arch!

.global _testexec_rbf_start
.type _testexec_rbf_start, @object
_testexec_rbf_start:
.incbin "testexec.swp"
.global _testexec_rbf_end
_testexec_rbf_end:

.align 4

.global _test_loader_app_start
.type _test_loader_app_start, @object
_test_loader_app_start:
.incbin "test_loader.app"
.global _test_loader_app_end
_test_loader_app_end:

