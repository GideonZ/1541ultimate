.section ".rodata"

.align 4 # which either means 4 or 2**4 depending on arch!
.global _sine_stereo_start
.type _sine_stereo_start, @object
_sine_stereo_start:
.incbin "waves_8_12.bin"
.global _sine_stereo_end
_sine_stereo_end:

.align 4 # which either means 4 or 2**4 depending on arch!
.global _dut_application_start
.type _dut_application_start, @object
_dut_application_start:
.incbin "dut.app"
.global _dut_application_end
_dut_application_end:
