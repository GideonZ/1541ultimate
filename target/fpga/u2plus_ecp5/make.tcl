prj_project open "u2p_ecp5.ldf"
prj_run Synthesis -impl impl1
prj_run Translate -impl impl1
prj_run Map -impl impl1
prj_run PAR -impl impl1
prj_run PAR -impl impl1 -task PARTrace

set twr_file "impl1/u2p_ecp5_impl1.twr"
set failed 0

if {[file exists $twr_file]} {
    set fp [open $twr_file r]
    set file_data [read $fp]
    close $fp
    
    # Matches "1 preference...", "2 preferences...", etc. but NOT "0 preferences..."
    # The regex looks for a digit 1-9 followed by "preference" and "not met"
    if {[regexp {[1-9][0-9]* preference[s]?.*not met} $file_data]} {
        puts "ERROR: Timing violations detected in $twr_file"
        set failed 1
    } else {
        puts "SUCCESS: Timing requirements met."
    }
} else {
    puts "ERROR: Timing report $twr_file not found."
    set failed 1
}

if {$failed} {
    prj_project close
    exit 1 
}

prj_run Export -impl impl1 -task Bitgen
prj_project close

