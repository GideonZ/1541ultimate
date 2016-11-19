.section ".rodata"

.align 4 # which either means 4 or 2**4 depending on arch!
.global _sine_stereo_start
.type _sine_stereo_start, @object
_sine_stereo_start:
.incbin "waves_11_7.bin"
.global _sine_stereo_end
_sine_stereo_end:

.align 4 # which either means 4 or 2**4 depending on arch!
.global _sine_mono_start
.type _sine_mono_start, @object
_sine_mono_start:
.incbin "wave_10.bin"
.global _sine_mono_end
_sine_mono_end:
