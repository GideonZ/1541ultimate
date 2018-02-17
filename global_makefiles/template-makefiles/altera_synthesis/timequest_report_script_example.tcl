###############################################################################
##
## (C) COPYRIGHT 2009-2011 TECHNOLUTION B.V., GOUDA NL
## | =======          I                   ==          I    =
## |    I             I                    I          I
## |    I   ===   === I ===  I ===   ===   I  I    I ====  I   ===  I ===
## |    I  /   \ I    I/   I I/   I I   I  I  I    I  I    I  I   I I/   I
## |    I  ===== I    I    I I    I I   I  I  I    I  I    I  I   I I    I
## |    I  \     I    I    I I    I I   I  I  I   /I  \    I  I   I I    I
## |    I   ===   === I    I I    I  ===  ===  === I   ==  I   ===  I    I
## |                 +---------------------------------------------------+
## +----+            |  +++++++++++++++++++++++++++++++++++++++++++++++++|
##      |            |             ++++++++++++++++++++++++++++++++++++++|
##      +------------+                          +++++++++++++++++++++++++|
##                                                         ++++++++++++++|
##                                                                  +++++|
##
###############################################################################
## Description: Generic TimeQuest Report Script
## Author     : Wouter van Oijen (wouter.van.oijen@technolution.nl)
###############################################################################
## This is a generic TimeQuest Report Script. It reports all common summaries,
## such as clocks, clock transfers, unconstrained paths, the top failing paths
## and all I/O timings.
## Project specific timing reports can be added at the end of the file.
##
## To use this script, define the TimeQuest Report File setting in the project.
##
## NOTE: Make sure to run the timing analysis for multiple corners instead of
## only for the slow timing model.
##
## The following assignments can be used in the Quartus II Settings File
## (project.qsf):
##
##  set_global_assignment -name OPTIMIZE_MULTI_CORNER_TIMING ON
##  set_global_assignment -name TIMEQUEST_DO_REPORT_TIMING ON
##  set_global_assignment -name TIMEQUEST_REPORT_SCRIPT \
##      timequest_report_script_example.tcl
##
###############################################################################


###############################################################################
## General timing reports
###############################################################################

# Clocks
report_clocks -panel_name "Clocks"

# Report All Summaries
qsta_utility::generate_all_summary_tables

# Advanced I/O Timing
report_advanced_io_timing -panel_name "Advanced I/O Timing"

# Clock Transfers
report_clock_transfers -panel_name "Clock Transfers"

# Report TCCS
report_tccs -panel_name "Report TCCS" -stdout -quiet

# Report RSKM
report_rskm -panel_name "Report RSKM" -stdout -quiet

# Unconstrained Paths
report_ucp -panel_name "Unconstrained Paths"

# Report Top Failing Paths
qsta_utility::generate_top_failures_per_clock "Top Failing Paths" 200

# Report All I/O Timings
qsta_utility::generate_all_io_timing_reports "Report Timing (I/O)" 1000

# Report maximum skew constraints
# NOTE: Supported since v9.0. Uncomment this for older versions.
report_max_skew -panel_name "Max skew constraints"


###############################################################################
## Project specific timing reports
###############################################################################

# Add project specific timing reports here.
