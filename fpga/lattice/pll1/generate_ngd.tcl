#!/usr/local/bin/wish

proc GetPlatform {} {
	global tcl_platform

	set cpu  $tcl_platform(machine)

	switch $cpu {
		intel -
		i*86* {
			set cpu ix86
		}
		x86_64 {
			if {$tcl_platform(wordSize) == 4} {
				set cpu ix86
			}
		}
	}

	switch $tcl_platform(platform) {
		windows {
			if {$cpu == "amd64"} {
				# Do not check wordSize, win32-x64 is an IL32P64 platform.
				set cpu x86_64
			}
			if {$cpu == "x86_64"} {
				return "nt64"
			} else {
				return "nt"
			}
		}
		unix {
			if {$tcl_platform(os) == "Linux"}  {
				if {$cpu == "x86_64"} {
					return "lin64"
				} else {
					return "lin"
				}
			} else  {
				return "sol"
			}
		}
	}
	return "nt"
}

set platformpath [GetPlatform]
set Para(sbp_path) [file dirname [info script]]
set Para(install_dir) $env(TOOLRTF)
set Para(FPGAPath) "[file join $Para(install_dir) ispfpga bin $platformpath]"
set Para(bin_dir) "[file join $Para(install_dir) bin $platformpath]"

set Para(ModuleName) "pll1"
set Para(Module) "PLL"
set Para(libname) ecp5u
set Para(arch_name) sa5p00
set Para(PartType) "LFE5U-25F"

set Para(tech_syn) ecp5u
set Para(tech_cae) ecp5u
set Para(Package) "CABGA256"
set Para(SpeedGrade) "6"
set Para(FMax) "100"
set fdcfile "$Para(sbp_path)/$Para(ModuleName).fdc"

#create response file(*.cmd) for Synpwrap
proc CreateCmdFile {} {
	global Para

	file mkdir "$Para(sbp_path)/syn_results"
	if [catch {open $Para(ModuleName).cmd w} rspFile] {
		puts "Cannot create response file $Para(ModuleName).cmd."
		exit -1
	} else {
		puts $rspFile "PROJECT: $Para(ModuleName)
		working_path: \"$Para(sbp_path)/syn_results\"
		module: $Para(ModuleName)
		verilog_file_list: \"$Para(sbp_path)/$Para(ModuleName).vhd\"
		vlog_std_v2001: true
		constraint_file_name: \"$Para(sbp_path)/$Para(ModuleName).fdc\"
		suffix_name: edn
		output_file_name: $Para(ModuleName)
		write_prf: true
		disable_io_insertion: true
		force_gsr: false
		frequency: $Para(FMax)
		fanout_limit: 50
		retiming: false
		pipe: false
		part: $Para(PartType)
		speed_grade: $Para(SpeedGrade)
		"
		close $rspFile
	}
}

#synpwrap
CreateCmdFile
set synpwrap "$Para(bin_dir)/synpwrap"
if {[file exists $fdcfile] == 0} {
	set Para(result) [catch {eval exec $synpwrap -rem -e $Para(ModuleName) -target $Para(tech_syn)} msg]
} else {
	set Para(result) [catch {eval exec $synpwrap -rem -e $Para(ModuleName) -target $Para(tech_syn) -fdc $fdcfile} msg]
}
#puts $msg

#edif2ngd
set edif2ngd "$Para(FPGAPath)/edif2ngd"
set Para(result) [catch {eval exec $edif2ngd -l $Para(libname) -d $Para(PartType) -nopropwarn \"syn_results/$Para(ModuleName).edn\" $Para(ModuleName).ngo} msg]
#puts $msg

#ngdbuild
set ngdbuild "$Para(FPGAPath)/ngdbuild"
set Para(result) [catch {eval exec $ngdbuild -addiobuf -dt -a $Para(arch_name) $Para(ModuleName).ngo $Para(ModuleName).ngd} msg]
#puts $msg
