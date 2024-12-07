.section ".rodata"

.align 4 # which either means 4 or 2**4 depending on arch!
.global _sine_stereo_start
.type _sine_stereo_start, @object
_sine_stereo_start:
.incbin "waves_11_7.bin"
.global _sine_stereo_end
_sine_stereo_end:

.align 4 # which either means 4 or 2**4 depending on arch!
.global _speaker_sample
.type _speaker_sample, @object
_speaker_sample:
.incbin "chinese_mono_16.raw"
.global _speaker_sample_end
_speaker_sample_end:
.long 0
.align 4 
.global _speaker_sample_size
_speaker_sample_size:
.long (_speaker_sample_end - _speaker_sample)/4
