onerror {resume}
quietly WaveActivateNextPane {} 0
add wave -noupdate -format Logic /tb_sid_from_file/sid/clock
add wave -noupdate -format Logic /tb_sid_from_file/sid/reset
add wave -noupdate -format Literal /tb_sid_from_file/sid/addr
add wave -noupdate -format Logic /tb_sid_from_file/sid/wren
add wave -noupdate -format Literal /tb_sid_from_file/sid/wdata
add wave -noupdate -format Literal /tb_sid_from_file/sid/rdata
add wave -noupdate -format Logic /tb_sid_from_file/sid/start_iter
add wave -noupdate -format Analog-Step -height 80 -max 65535.0 -min -65535.0 -radix decimal /tb_sid_from_file/sid/sample_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/voice_osc
add wave -noupdate -format Literal /tb_sid_from_file/sid/voice_wave
add wave -noupdate -format Literal /tb_sid_from_file/sid/voice_mul
add wave -noupdate -format Logic /tb_sid_from_file/sid/enable_osc
add wave -noupdate -format Logic /tb_sid_from_file/sid/enable_wave
add wave -noupdate -format Logic /tb_sid_from_file/sid/enable_mul
add wave -noupdate -format Literal /tb_sid_from_file/sid/freq
add wave -noupdate -format Logic /tb_sid_from_file/sid/test
add wave -noupdate -format Logic /tb_sid_from_file/sid/sync
add wave -noupdate -format Logic /tb_sid_from_file/sid/ring_mod
add wave -noupdate -format Literal /tb_sid_from_file/sid/wave_sel
add wave -noupdate -format Literal /tb_sid_from_file/sid/sq_width
add wave -noupdate -format Logic /tb_sid_from_file/sid/gate
add wave -noupdate -format Literal /tb_sid_from_file/sid/attack
add wave -noupdate -format Literal /tb_sid_from_file/sid/decay
add wave -noupdate -format Literal /tb_sid_from_file/sid/sustain
add wave -noupdate -format Literal /tb_sid_from_file/sid/release
add wave -noupdate -format Literal /tb_sid_from_file/sid/volume
add wave -noupdate -format Literal /tb_sid_from_file/sid/filter_co
add wave -noupdate -format Literal /tb_sid_from_file/sid/filter_res
add wave -noupdate -format Logic /tb_sid_from_file/sid/filter_en
add wave -noupdate -format Logic /tb_sid_from_file/sid/filter_ex
add wave -noupdate -format Logic /tb_sid_from_file/sid/filter_hp
add wave -noupdate -format Logic /tb_sid_from_file/sid/filter_bp
add wave -noupdate -format Logic /tb_sid_from_file/sid/filter_lp
add wave -noupdate -format Logic /tb_sid_from_file/sid/voice3_off
add wave -noupdate -format Literal /tb_sid_from_file/sid/osc3
add wave -noupdate -format Analog-Step -height 50 -max 255.0 -radix unsigned /tb_sid_from_file/sid/env3
add wave -noupdate -format Literal /tb_sid_from_file/sid/osc_val
add wave -noupdate -format Logic /tb_sid_from_file/sid/carry_20
add wave -noupdate -format Literal /tb_sid_from_file/sid/enveloppe
add wave -noupdate -format Literal /tb_sid_from_file/sid/waveform
add wave -noupdate -format Logic /tb_sid_from_file/sid/msb_other
add wave -noupdate -format Logic /tb_sid_from_file/sid/car_other
add wave -noupdate -format Logic /tb_sid_from_file/sid/osc_carry
add wave -noupdate -format Logic /tb_sid_from_file/sid/valid_sum
add wave -noupdate -format Logic /tb_sid_from_file/sid/valid_filt
add wave -noupdate -format Logic /tb_sid_from_file/sid/valid_mix
add wave -noupdate -format Literal /tb_sid_from_file/sid/high_pass
add wave -noupdate -format Literal /tb_sid_from_file/sid/band_pass
add wave -noupdate -format Literal /tb_sid_from_file/sid/low_pass
add wave -noupdate -format Literal /tb_sid_from_file/sid/f
add wave -noupdate -format Literal /tb_sid_from_file/sid/q_from_table
add wave -noupdate -format Literal /tb_sid_from_file/sid/q
add wave -noupdate -format Analog-Step -height 50 -max 65535.0 -min -65535.0 /tb_sid_from_file/sid/filter_out
add wave -noupdate -format Analog-Step -height 75 -max 65535.0 -min -65535.0 /tb_sid_from_file/sid/direct_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/filt_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/filt1_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/filt2_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/mixed_out
add wave -noupdate -format Logic /tb_sid_from_file/sid/adsr/clock
add wave -noupdate -format Logic /tb_sid_from_file/sid/adsr/reset
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/voice_i
add wave -noupdate -format Logic /tb_sid_from_file/sid/adsr/enable_i
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/voice_o
add wave -noupdate -format Logic /tb_sid_from_file/sid/adsr/enable_o
add wave -noupdate -format Logic /tb_sid_from_file/sid/adsr/gate
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/attack
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/decay
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/sustain
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/release
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/env_state
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/env_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/enveloppe
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/state
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/state_array
add wave -noupdate -format Logic /tb_sid_from_file/sid/clock
add wave -noupdate -format Logic /tb_sid_from_file/sid/reset
add wave -noupdate -format Literal /tb_sid_from_file/sid/addr
add wave -noupdate -format Logic /tb_sid_from_file/sid/wren
add wave -noupdate -format Literal /tb_sid_from_file/sid/wdata
add wave -noupdate -format Literal /tb_sid_from_file/sid/rdata
add wave -noupdate -format Logic /tb_sid_from_file/sid/start_iter
add wave -noupdate -format Literal /tb_sid_from_file/sid/sample_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/voice_osc
add wave -noupdate -format Literal /tb_sid_from_file/sid/voice_wave
add wave -noupdate -format Literal /tb_sid_from_file/sid/voice_mul
add wave -noupdate -format Logic /tb_sid_from_file/sid/enable_osc
add wave -noupdate -format Logic /tb_sid_from_file/sid/enable_wave
add wave -noupdate -format Logic /tb_sid_from_file/sid/enable_mul
add wave -noupdate -format Literal /tb_sid_from_file/sid/freq
add wave -noupdate -format Logic /tb_sid_from_file/sid/test
add wave -noupdate -format Logic /tb_sid_from_file/sid/sync
add wave -noupdate -format Logic /tb_sid_from_file/sid/ring_mod
add wave -noupdate -format Literal /tb_sid_from_file/sid/wave_sel
add wave -noupdate -format Literal /tb_sid_from_file/sid/sq_width
add wave -noupdate -format Logic /tb_sid_from_file/sid/gate
add wave -noupdate -format Literal /tb_sid_from_file/sid/attack
add wave -noupdate -format Literal /tb_sid_from_file/sid/decay
add wave -noupdate -format Literal /tb_sid_from_file/sid/sustain
add wave -noupdate -format Literal /tb_sid_from_file/sid/release
add wave -noupdate -format Literal /tb_sid_from_file/sid/volume
add wave -noupdate -format Literal /tb_sid_from_file/sid/filter_co
add wave -noupdate -format Literal /tb_sid_from_file/sid/filter_res
add wave -noupdate -format Logic /tb_sid_from_file/sid/filter_en
add wave -noupdate -format Logic /tb_sid_from_file/sid/filter_ex
add wave -noupdate -format Logic /tb_sid_from_file/sid/filter_hp
add wave -noupdate -format Logic /tb_sid_from_file/sid/filter_bp
add wave -noupdate -format Logic /tb_sid_from_file/sid/filter_lp
add wave -noupdate -format Logic /tb_sid_from_file/sid/voice3_off
add wave -noupdate -format Literal /tb_sid_from_file/sid/osc3
add wave -noupdate -format Analog-Step -height 50 -max 255.0 -radix unsigned /tb_sid_from_file/sid/env3
add wave -noupdate -format Literal /tb_sid_from_file/sid/osc_val
add wave -noupdate -format Logic /tb_sid_from_file/sid/carry_20
add wave -noupdate -format Literal /tb_sid_from_file/sid/enveloppe
add wave -noupdate -format Literal /tb_sid_from_file/sid/waveform
add wave -noupdate -format Logic /tb_sid_from_file/sid/msb_other
add wave -noupdate -format Logic /tb_sid_from_file/sid/car_other
add wave -noupdate -format Logic /tb_sid_from_file/sid/osc_carry
add wave -noupdate -format Logic /tb_sid_from_file/sid/valid_sum
add wave -noupdate -format Logic /tb_sid_from_file/sid/valid_filt
add wave -noupdate -format Logic /tb_sid_from_file/sid/valid_mix
add wave -noupdate -format Literal /tb_sid_from_file/sid/high_pass
add wave -noupdate -format Literal /tb_sid_from_file/sid/band_pass
add wave -noupdate -format Literal /tb_sid_from_file/sid/low_pass
add wave -noupdate -format Literal /tb_sid_from_file/sid/f
add wave -noupdate -format Literal /tb_sid_from_file/sid/q_from_table
add wave -noupdate -format Literal /tb_sid_from_file/sid/q
add wave -noupdate -format Analog-Step -height 50 -max 65535.0 -min -65535.0 /tb_sid_from_file/sid/filter_out
add wave -noupdate -format Analog-Step -height 75 -max 65535.0 -min -65535.0 /tb_sid_from_file/sid/direct_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/filt_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/filt1_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/filt2_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/mixed_out
add wave -noupdate -format Logic /tb_sid_from_file/sid/adsr/clock
add wave -noupdate -format Logic /tb_sid_from_file/sid/adsr/reset
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/voice_i
add wave -noupdate -format Logic /tb_sid_from_file/sid/adsr/enable_i
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/voice_o
add wave -noupdate -format Logic /tb_sid_from_file/sid/adsr/enable_o
add wave -noupdate -format Logic /tb_sid_from_file/sid/adsr/gate
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/attack
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/decay
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/sustain
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/release
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/env_state
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/env_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/enveloppe
add wave -noupdate -format Literal /tb_sid_from_file/sid/adsr/state
add wave -noupdate -format Literal -expand /tb_sid_from_file/sid/adsr/state_array
add wave -noupdate -format Logic /tb_sid_from_file/sid/sum/clock
add wave -noupdate -format Logic /tb_sid_from_file/sid/sum/reset
add wave -noupdate -format Literal /tb_sid_from_file/sid/sum/voice_i
add wave -noupdate -format Logic /tb_sid_from_file/sid/sum/enable_i
add wave -noupdate -format Logic /tb_sid_from_file/sid/sum/filter_en
add wave -noupdate -format Literal /tb_sid_from_file/sid/sum/enveloppe
add wave -noupdate -format Literal /tb_sid_from_file/sid/sum/waveform
add wave -noupdate -format Logic /tb_sid_from_file/sid/sum/valid_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/sum/direct_out
add wave -noupdate -format Literal /tb_sid_from_file/sid/sum/filter_out
add wave -noupdate -format Logic /tb_sid_from_file/sid/sum/filter_m
add wave -noupdate -format Literal /tb_sid_from_file/sid/sum/voice_m
add wave -noupdate -format Literal /tb_sid_from_file/sid/sum/mult_m
add wave -noupdate -format Literal /tb_sid_from_file/sid/sum/accu_f
add wave -noupdate -format Literal /tb_sid_from_file/sid/sum/accu_u
add wave -noupdate -format Logic /tb_sid_from_file/clock
add wave -noupdate -format Logic /tb_sid_from_file/reset
add wave -noupdate -format Literal /tb_sid_from_file/addr
add wave -noupdate -format Logic /tb_sid_from_file/wren
add wave -noupdate -format Literal /tb_sid_from_file/wdata
add wave -noupdate -format Literal /tb_sid_from_file/rdata
add wave -noupdate -format Logic /tb_sid_from_file/start_iter
add wave -noupdate -format Literal /tb_sid_from_file/sample_out
add wave -noupdate -format Logic /tb_sid_from_file/stop_clock
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/clock
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/reset
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/addr
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/wren
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/wdata
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/rdata
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/voice_osc
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/voice_wave
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/voice_adsr
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/voice_mul
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/freq
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/test
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/sync
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/ring_mod
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/wave_sel
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/sq_width
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/gate
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/attack
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/decay
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/sustain
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/release
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/filter_en
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/volume
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/filter_co
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/filter_res
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/filter_ex
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/filter_hp
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/filter_bp
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/filter_lp
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/voice3_off
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/osc3
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/env3
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/freq_lo
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/freq_hi
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/phase_lo
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/phase_hi
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/control
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/att_dec
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/sust_rel
add wave -noupdate -format Logic /tb_sid_from_file/sid/i_regs/do_write
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/wdata_d
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/filt_en_i
add wave -noupdate -format Literal /tb_sid_from_file/sid/i_regs/address
TreeUpdate [SetDefaultTree]
WaveRestoreCursors {{Cursor 1} {47174150000 ps} 0}
configure wave -namecolwidth 150
configure wave -valuecolwidth 100
configure wave -justifyvalue left
configure wave -signalnamewidth 2
configure wave -snapdistance 10
configure wave -datasetprefix 0
configure wave -rowmargin 4
configure wave -childrowmargin 2
configure wave -gridoffset 0
configure wave -gridperiod 1
configure wave -griddelta 40
configure wave -timeline 0
configure wave -timelineunits ps
update
WaveRestoreZoom {0 ps} {63 ms}
