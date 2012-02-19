###############################################################################
##
## (C) COPYRIGHT 2010 - Gideon's Logic Architectures
##
###############################################################################
## This is a simulation/regression test makefile
###############################################################################
## NOTE: The following restrictions apply when using this make file:
##		- All filenames of the files that need to be compiled should be
##		  unique. Even when they are placed in different directories.
##		- The automatic dependency search has a number of limitations.
##		  In general should the auto search be able to handle all files that
##		  comply to the style guide. All non-compliant files should be added
##		  using '.sinc' files.
##		- Libraries other then work are only supported when using '.sinc'
##		  files.
##		- file and directory names should be lowercase and may not contain
##		  spaces!
##		- target names must be lowercase
###############################################################################

# GLOBAL_INCS should point to the make include files directory
export GLOBAL_INCS = $(strip $(shell pwd)/../../../global_makefiles)

.SUFFIXES:		#delete all known suffixes

###############################################################################
#### Common settings
###############################################################################

#This makefile(s) containing the testcases
CONFIG_FILES = makefile

LIBS			= # is automatically generated from the source include files

VSIM_SOURCES 	= # is automatically generated from the source include files or autodep search

WORK_DIR		= work

SOURCE_INCLUDES = secretblaze.sinc

# AUTO_DEP_PATHS
#
# A space separated list of paths. These paths are searched for source
# dependencies.
#
# All vhdl source filenames in these paths should be unique.
#
# NOTE: all vhdl files found by the autodependency search are compiled into the
#       library work

AUTO_DEP_PATHS  = $(wildcard ../vhdl_bfm/)
AUTO_DEP_PATHS += $(wildcard ../vhdl_sim/)
AUTO_DEP_PATHS += $(wildcard ../packages/vhdl_source/)
AUTO_DEP_PATHS += $(wildcard ../../../fpga/*/vhdl_source/)
AUTO_DEP_PATHS += $(wildcard ../../../fpga/*/*/vhdl_source/)
AUTO_DEP_PATHS += $(wildcard ../../../fpga/*/*/*/vhdl_source/)
AUTO_DEP_PATHS += $(wildcard ../../../fpga/*/vhdl_sim/)
AUTO_DEP_PATHS += $(wildcard ../../../fpga/*/*/vhdl_sim/)
AUTO_DEP_PATHS += $(wildcard ../../../fpga/*/*/*/vhdl_sim/)
AUTO_DEP_PATHS += $(wildcard ../../../fpga/*/vhdl_bfm/)
AUTO_DEP_PATHS += $(wildcard ../../../fpga/*/*/vhdl_bfm/)
AUTO_DEP_PATHS += $(wildcard ../../../fpga/*/*/*/vhdl_bfm/)


###############################################################################
#### Testbenches
###############################################################################
#### Testbench targets execute an GUI and start the simulation in the GUI.
#### Using the $(TESTBENCH)_DO_GUI_BEFORE a wave window can be loaded. Multiple
#### testbench targets are allowed (use different names).
###############################################################################

#### tb1 #####

TESTBENCH 						:= tb_ulpi_bus
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

#### tb2 #####
TESTBENCH 						:= tb_ulpi_tx
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

#### tb3 #####
TESTBENCH 						:= tb_ulpi_host
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

#### tb4 #####
TESTBENCH 						:= tb_usb_host
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

#### tb5 #####
TESTBENCH 						:= ext_mem_ctrl_v5_sdr_tb
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

#### tb6 #####
TESTBENCH 						:= ext_mem_ctrl_v6_tb
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

#### tb7 #####
TESTBENCH 						:= ext_mem_test_v6_tb
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

#### tb8 #####
TESTBENCH 						:= ext_mem_test_32_tb
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

#### tb9 #####
TESTBENCH 						:= ext_mem_ctrl_v7_tb
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

#### tb10 #####
TESTBENCH 						:= sb_cpu
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

#### tb11 #####
TESTBENCH 						:= iec_processor_tb
TESTBENCHES						:= $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC				:= 
$(TESTBENCH)_ENTITY				:= 
$(TESTBENCH)_TIME				:= 
$(TESTBENCH)_DO_GUI_BEFORE		:= 
$(TESTBENCH)_DO_GUI_AFTER		:= 
$(TESTBENCH)_DO_GUI_OVERRIDE	:= 

###############################################################################
#### Testcases
###############################################################################
#### A testcase target executes an simulation that gives a pass or fail after
#### execution. The testcase is normaly started in commandline mode, but can
#### also be started in the GUI. The testcase should generate a report file.
#### The name of the report file that the testcase should generate is passed by
#### setting the generic g_report_file_name. The report file should contain
#### the string "** SIMULATION ENDED: SUCCESSFUL" when the testcase is
#### successful. The tl_sctb_pkg package can be used for generating these reports.
####
#### Multiple testcase targets are allowed (use different names).
###############################################################################

### tc1 #####
TESTCASE 					:= harness_v2_mk1
TESTCASES					:= $(TESTCASES) $(TESTCASE)
$(TESTCASE)_SRC				:= $(TESTCASE).vhd
$(TESTCASE)_ENTITY			:= $(TESTCASE)
$(TESTCASE)_TIME			:= 20 us;
$(TESTCASE)_DO_GUI_BEFORE	:= 

TESTCASE 					:= harness_v4
TESTCASES					:= $(TESTCASES) $(TESTCASE)
$(TESTCASE)_SRC				:= $(TESTCASE).vhd
$(TESTCASE)_ENTITY			:= $(TESTCASE)
$(TESTCASE)_TIME			:= 20 us;
$(TESTCASE)_DO_GUI_BEFORE	:= 

TESTCASE 					:= harness_v5
TESTCASES					:= $(TESTCASES) $(TESTCASE)
$(TESTCASE)_SRC				:= $(TESTCASE).vhd
$(TESTCASE)_ENTITY			:= $(TESTCASE)
$(TESTCASE)_TIME			:= 20 us;
$(TESTCASE)_DO_GUI_BEFORE	:= 

TESTCASE 					:= reu_tc_1
TESTCASES					:= $(TESTCASES) $(TESTCASE)
$(TESTCASE)_SRC				:= $(TESTCASE).vhd
$(TESTCASE)_ENTITY			:= $(TESTCASE)
$(TESTCASE)_TIME			:= 40 us;
$(TESTCASE)_DO_GUI_BEFORE	:= 

TESTCASE 					:= cache_tc_1
TESTCASES					:= $(TESTCASES) $(TESTCASE)
$(TESTCASE)_SRC				:= $(TESTCASE).vhd
$(TESTCASE)_ENTITY			:= $(TESTCASE)
$(TESTCASE)_TIME			:= 40 us;
$(TESTCASE)_DO_GUI_BEFORE	:= 

###############################################################################
#### Regression tests
###############################################################################
#### Regression tests  consists of a collection of testcases. These testcases
#### are always executed in command_line mode.
####
#### Multiple regression test targets are allowed (use different names).
###############################################################################

#### rt1 #####
REGTEST 				:= reg_tests
REGTESTS				:= $(REGTESTS) $(REGTEST)
$(REGTEST)_TESTCASES	:= $(TESTCASES)

###############################################################################
#### Default rules
###############################################################################


all:	$(REGTESTS)												# default target may be changed


info: vsim-info


###############################################################################
#### WARNING!!
###############################################################################
#### Do not touch the settings below (these statements should be placed at the
#### end of the makefile)
###############################################################################

###############################################################################
#### Include simulation rules
###############################################################################

include $(GLOBAL_INCS)/vsim_tools.inc

test:
	echo $(VSIM_SOURCES)
