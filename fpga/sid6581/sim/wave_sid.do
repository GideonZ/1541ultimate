onerror {resume}
quietly WaveActivateNextPane {} 0
add wave -noupdate -format Logic /tb_sid/sid/clock
add wave -noupdate -format Logic /tb_sid/sid/reset
add wave -noupdate -format Literal /tb_sid/sid/addr
add wave -noupdate -format Logic /tb_sid/sid/wren
add wave -noupdate -format Literal /tb_sid/sid/wdata
add wave -noupdate -format Literal /tb_sid/sid/rdata
add wave -noupdate -format Logic /tb_sid/sid/start_iter
add wave -noupdate -format Analog-Step -height 80 -max 65535.0 -min -65535.0 -radix decimal /tb_sid/sid/sample_out
add wave -noupdate -format Literal /tb_sid/sid/voice_osc
add wave -noupdate -format Literal /tb_sid/sid/voice_wave
add wave -noupdate -format Literal /tb_sid/sid/voice_mul
add wave -noupdate -format Logic /tb_sid/sid/enable_osc
add wave -noupdate -format Logic /tb_sid/sid/enable_wave
add wave -noupdate -format Logic /tb_sid/sid/enable_mul
add wave -noupdate -format Literal /tb_sid/sid/freq
add wave -noupdate -format Logic /tb_sid/sid/test
add wave -noupdate -format Logic /tb_sid/sid/sync
add wave -noupdate -format Logic /tb_sid/sid/ring_mod
add wave -noupdate -format Literal /tb_sid/sid/wave_sel
add wave -noupdate -format Literal /tb_sid/sid/sq_width
add wave -noupdate -format Logic /tb_sid/sid/gate
add wave -noupdate -format Literal /tb_sid/sid/attack
add wave -noupdate -format Literal /tb_sid/sid/decay
add wave -noupdate -format Literal /tb_sid/sid/sustain
add wave -noupdate -format Literal /tb_sid/sid/release
add wave -noupdate -format Literal /tb_sid/sid/volume
add wave -noupdate -format Literal /tb_sid/sid/filter_co
add wave -noupdate -format Literal /tb_sid/sid/filter_res
add wave -noupdate -format Logic /tb_sid/sid/filter_en
add wave -noupdate -format Logic /tb_sid/sid/filter_ex
add wave -noupdate -format Logic /tb_sid/sid/filter_hp
add wave -noupdate -format Logic /tb_sid/sid/filter_bp
add wave -noupdate -format Logic /tb_sid/sid/filter_lp
add wave -noupdate -format Logic /tb_sid/sid/voice3_off
add wave -noupdate -format Literal /tb_sid/sid/osc3
add wave -noupdate -format Analog-Step -height 50 -max 255.0 -radix unsigned /tb_sid/sid/env3
add wave -noupdate -format Literal /tb_sid/sid/osc_val
add wave -noupdate -format Logic /tb_sid/sid/carry_20
add wave -noupdate -format Literal /tb_sid/sid/enveloppe
add wave -noupdate -format Literal /tb_sid/sid/waveform
add wave -noupdate -format Logic /tb_sid/sid/msb_other
add wave -noupdate -format Logic /tb_sid/sid/car_other
add wave -noupdate -format Logic /tb_sid/sid/osc_carry
add wave -noupdate -format Logic /tb_sid/sid/valid_sum
add wave -noupdate -format Logic /tb_sid/sid/valid_filt
add wave -noupdate -format Logic /tb_sid/sid/valid_mix
add wave -noupdate -format Literal /tb_sid/sid/high_pass
add wave -noupdate -format Literal /tb_sid/sid/band_pass
add wave -noupdate -format Literal /tb_sid/sid/low_pass
add wave -noupdate -format Literal /tb_sid/sid/f
add wave -noupdate -format Literal /tb_sid/sid/q_from_table
add wave -noupdate -format Literal /tb_sid/sid/q
add wave -noupdate -format Analog-Step -height 50 -max 65535.0 -min -65535.0 /tb_sid/sid/filter_out
add wave -noupdate -format Analog-Step -height 75 -max 65535.0 -min -65535.0 /tb_sid/sid/direct_out
add wave -noupdate -format Literal /tb_sid/sid/filt_out
add wave -noupdate -format Literal /tb_sid/sid/filt1_out
add wave -noupdate -format Literal /tb_sid/sid/filt2_out
add wave -noupdate -format Literal /tb_sid/sid/mixed_out
add wave -noupdate -format Logic /tb_sid/sid/adsr/clock
add wave -noupdate -format Logic /tb_sid/sid/adsr/reset
add wave -noupdate -format Literal /tb_sid/sid/adsr/voice_i
add wave -noupdate -format Logic /tb_sid/sid/adsr/enable_i
add wave -noupdate -format Literal /tb_sid/sid/adsr/voice_o
add wave -noupdate -format Logic /tb_sid/sid/adsr/enable_o
add wave -noupdate -format Logic /tb_sid/sid/adsr/gate
add wave -noupdate -format Literal /tb_sid/sid/adsr/attack
add wave -noupdate -format Literal /tb_sid/sid/adsr/decay
add wave -noupdate -format Literal /tb_sid/sid/adsr/sustain
add wave -noupdate -format Literal /tb_sid/sid/adsr/release
add wave -noupdate -format Literal /tb_sid/sid/adsr/env_state
add wave -noupdate -format Literal /tb_sid/sid/adsr/env_out
add wave -noupdate -format Literal /tb_sid/sid/adsr/enveloppe
add wave -noupdate -format Literal /tb_sid/sid/adsr/state
add wave -noupdate -format Literal /tb_sid/sid/adsr/state_array
add wave -noupdate -format Logic /tb_sid/sid/clock
add wave -noupdate -format Logic /tb_sid/sid/reset
add wave -noupdate -format Literal /tb_sid/sid/addr
add wave -noupdate -format Logic /tb_sid/sid/wren
add wave -noupdate -format Literal /tb_sid/sid/wdata
add wave -noupdate -format Literal /tb_sid/sid/rdata
add wave -noupdate -format Logic /tb_sid/sid/start_iter
add wave -noupdate -format Literal /tb_sid/sid/sample_out
add wave -noupdate -format Literal /tb_sid/sid/voice_osc
add wave -noupdate -format Literal /tb_sid/sid/voice_wave
add wave -noupdate -format Literal /tb_sid/sid/voice_mul
add wave -noupdate -format Logic /tb_sid/sid/enable_osc
add wave -noupdate -format Logic /tb_sid/sid/enable_wave
add wave -noupdate -format Logic /tb_sid/sid/enable_mul
add wave -noupdate -format Literal /tb_sid/sid/freq
add wave -noupdate -format Logic /tb_sid/sid/test
add wave -noupdate -format Logic /tb_sid/sid/sync
add wave -noupdate -format Logic /tb_sid/sid/ring_mod
add wave -noupdate -format Literal /tb_sid/sid/wave_sel
add wave -noupdate -format Literal /tb_sid/sid/sq_width
add wave -noupdate -format Logic /tb_sid/sid/gate
add wave -noupdate -format Literal /tb_sid/sid/attack
add wave -noupdate -format Literal /tb_sid/sid/decay
add wave -noupdate -format Literal /tb_sid/sid/sustain
add wave -noupdate -format Literal /tb_sid/sid/release
add wave -noupdate -format Literal /tb_sid/sid/volume
add wave -noupdate -format Literal /tb_sid/sid/filter_co
add wave -noupdate -format Literal /tb_sid/sid/filter_res
add wave -noupdate -format Logic /tb_sid/sid/filter_en
add wave -noupdate -format Logic /tb_sid/sid/filter_ex
add wave -noupdate -format Logic /tb_sid/sid/filter_hp
add wave -noupdate -format Logic /tb_sid/sid/filter_bp
add wave -noupdate -format Logic /tb_sid/sid/filter_lp
add wave -noupdate -format Logic /tb_sid/sid/voice3_off
add wave -noupdate -format Literal /tb_sid/sid/osc3
add wave -noupdate -format Analog-Step -height 50 -max 255.0 -radix unsigned /tb_sid/sid/env3
add wave -noupdate -format Literal /tb_sid/sid/osc_val
add wave -noupdate -format Logic /tb_sid/sid/carry_20
add wave -noupdate -format Literal /tb_sid/sid/enveloppe
add wave -noupdate -format Literal /tb_sid/sid/waveform
add wave -noupdate -format Logic /tb_sid/sid/msb_other
add wave -noupdate -format Logic /tb_sid/sid/car_other
add wave -noupdate -format Logic /tb_sid/sid/osc_carry
add wave -noupdate -format Logic /tb_sid/sid/valid_sum
add wave -noupdate -format Logic /tb_sid/sid/valid_filt
add wave -noupdate -format Logic /tb_sid/sid/valid_mix
add wave -noupdate -format Literal /tb_sid/sid/high_pass
add wave -noupdate -format Literal /tb_sid/sid/band_pass
add wave -noupdate -format Literal /tb_sid/sid/low_pass
add wave -noupdate -format Literal /tb_sid/sid/f
add wave -noupdate -format Literal /tb_sid/sid/q_from_table
add wave -noupdate -format Literal /tb_sid/sid/q
add wave -noupdate -format Analog-Step -height 50 -max 65535.0 -min -65535.0 /tb_sid/sid/filter_out
add wave -noupdate -format Analog-Step -height 75 -max 65535.0 -min -65535.0 /tb_sid/sid/direct_out
add wave -noupdate -format Literal /tb_sid/sid/filt_out
add wave -noupdate -format Literal /tb_sid/sid/filt1_out
add wave -noupdate -format Literal /tb_sid/sid/filt2_out
add wave -noupdate -format Literal /tb_sid/sid/mixed_out
add wave -noupdate -format Logic /tb_sid/sid/adsr/clock
add wave -noupdate -format Logic /tb_sid/sid/adsr/reset
add wave -noupdate -format Literal /tb_sid/sid/adsr/voice_i
add wave -noupdate -format Logic /tb_sid/sid/adsr/enable_i
add wave -noupdate -format Literal /tb_sid/sid/adsr/voice_o
add wave -noupdate -format Logic /tb_sid/sid/adsr/enable_o
add wave -noupdate -format Logic /tb_sid/sid/adsr/gate
add wave -noupdate -format Literal /tb_sid/sid/adsr/attack
add wave -noupdate -format Literal /tb_sid/sid/adsr/decay
add wave -noupdate -format Literal /tb_sid/sid/adsr/sustain
add wave -noupdate -format Literal /tb_sid/sid/adsr/release
add wave -noupdate -format Literal /tb_sid/sid/adsr/env_state
add wave -noupdate -format Literal /tb_sid/sid/adsr/env_out
add wave -noupdate -format Literal /tb_sid/sid/adsr/enveloppe
add wave -noupdate -format Literal /tb_sid/sid/adsr/state
add wave -noupdate -format Literal -expand /tb_sid/sid/adsr/state_array
add wave -noupdate -format Logic /tb_sid/sid/sum/clock
add wave -noupdate -format Logic /tb_sid/sid/sum/reset
add wave -noupdate -format Literal /tb_sid/sid/sum/voice_i
add wave -noupdate -format Logic /tb_sid/sid/sum/enable_i
add wave -noupdate -format Logic /tb_sid/sid/sum/filter_en
add wave -noupdate -format Literal /tb_sid/sid/sum/enveloppe
add wave -noupdate -format Literal /tb_sid/sid/sum/waveform
add wave -noupdate -format Logic /tb_sid/sid/sum/valid_out
add wave -noupdate -format Literal /tb_sid/sid/sum/direct_out
add wave -noupdate -format Literal /tb_sid/sid/sum/filter_out
add wave -noupdate -format Logic /tb_sid/sid/sum/filter_m
add wave -noupdate -format Literal /tb_sid/sid/sum/voice_m
add wave -noupdate -format Literal /tb_sid/sid/sum/mult_m
add wave -noupdate -format Literal /tb_sid/sid/sum/accu_f
add wave -noupdate -format Literal /tb_sid/sid/sum/accu_u
add wave -noupdate -format Logic /tb_sid/clock
add wave -noupdate -format Logic /tb_sid/reset
add wave -noupdate -format Literal /tb_sid/addr
add wave -noupdate -format Logic /tb_sid/wren
add wave -noupdate -format Literal /tb_sid/wdata
add wave -noupdate -format Literal /tb_sid/rdata
add wave -noupdate -format Logic /tb_sid/start_iter
add wave -noupdate -format Literal /tb_sid/sample_out
add wave -noupdate -format Logic /tb_sid/stop_clock
add wave -noupdate -format Logic /tb_sid/sid/i_regs/clock
add wave -noupdate -format Logic /tb_sid/sid/i_regs/reset
add wave -noupdate -format Literal /tb_sid/sid/i_regs/addr
add wave -noupdate -format Logic /tb_sid/sid/i_regs/wren
add wave -noupdate -format Literal /tb_sid/sid/i_regs/wdata
add wave -noupdate -format Literal /tb_sid/sid/i_regs/rdata
add wave -noupdate -format Literal /tb_sid/sid/i_regs/voice_osc
add wave -noupdate -format Literal /tb_sid/sid/i_regs/voice_wave
add wave -noupdate -format Literal /tb_sid/sid/i_regs/voice_adsr
add wave -noupdate -format Literal /tb_sid/sid/i_regs/voice_mul
add wave -noupdate -format Literal /tb_sid/sid/i_regs/freq
add wave -noupdate -format Logic /tb_sid/sid/i_regs/test
add wave -noupdate -format Logic /tb_sid/sid/i_regs/sync
add wave -noupdate -format Logic /tb_sid/sid/i_regs/ring_mod
add wave -noupdate -format Literal /tb_sid/sid/i_regs/wave_sel
add wave -noupdate -format Literal /tb_sid/sid/i_regs/sq_width
add wave -noupdate -format Logic /tb_sid/sid/i_regs/gate
add wave -noupdate -format Literal /tb_sid/sid/i_regs/attack
add wave -noupdate -format Literal /tb_sid/sid/i_regs/decay
add wave -noupdate -format Literal /tb_sid/sid/i_regs/sustain
add wave -noupdate -format Literal /tb_sid/sid/i_regs/release
add wave -noupdate -format Logic /tb_sid/sid/i_regs/filter_en
add wave -noupdate -format Literal /tb_sid/sid/i_regs/volume
add wave -noupdate -format Literal /tb_sid/sid/i_regs/filter_co
add wave -noupdate -format Literal /tb_sid/sid/i_regs/filter_res
add wave -noupdate -format Logic /tb_sid/sid/i_regs/filter_ex
add wave -noupdate -format Logic /tb_sid/sid/i_regs/filter_hp
add wave -noupdate -format Logic /tb_sid/sid/i_regs/filter_bp
add wave -noupdate -format Logic /tb_sid/sid/i_regs/filter_lp
add wave -noupdate -format Logic /tb_sid/sid/i_regs/voice3_off
add wave -noupdate -format Literal /tb_sid/sid/i_regs/osc3
add wave -noupdate -format Literal /tb_sid/sid/i_regs/env3
add wave -noupdate -format Literal /tb_sid/sid/i_regs/freq_lo
add wave -noupdate -format Literal /tb_sid/sid/i_regs/freq_hi
add wave -noupdate -format Literal /tb_sid/sid/i_regs/phase_lo
add wave -noupdate -format Literal /tb_sid/sid/i_regs/phase_hi
add wave -noupdate -format Literal /tb_sid/sid/i_regs/control
add wave -noupdate -format Literal /tb_sid/sid/i_regs/att_dec
add wave -noupdate -format Literal /tb_sid/sid/i_regs/sust_rel
add wave -noupdate -format Logic /tb_sid/sid/i_regs/do_write
add wave -noupdate -format Literal /tb_sid/sid/i_regs/wdata_d
add wave -noupdate -format Literal /tb_sid/sid/i_regs/filt_en_i
add wave -noupdate -format Literal /tb_sid/sid/i_regs/address
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
