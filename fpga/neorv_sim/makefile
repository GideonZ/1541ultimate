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

SHELL := /bin/bash
## GLOBAL_INCS should point to the make include files directory. This value
## should only be overridden when it is not detected automatically.
#export GLOBAL_INCS = $(strip $(shell pwd)/<PATH_TO_INCLUDE_FILES>)

default : all
.SUFFIXES:		# delete all known suffixes

###############################################################################
#### Common settings
###############################################################################
#ENVIRONMENTS = modelsim/ase/10.1b
ENVIRONMENTS = aldec/riviera
GLOBAL_INCS  = ../../global_makefiles

# This makefile(s) containing the testcases
CONFIG_FILES 	= makefile

LIBS		= # is automatically generated from the source include files

VSIM_SOURCES 	= # is automatically generated from the source include files or autodep search

WORK_DIR	= work

# NOTE: The *.sinc files must have Linux line endings!!
SOURCE_INCLUDES = neorv32.sinc 

# AUTO_DEP_PATHS
#
# A space separated list of paths. These paths are searched for source
# dependencies.
#
# All vhdl source filenames in these paths should be unique.
# Furthermore, all the filenames of the sources in the auto
# dependency search paths should comply with the Technolution PLDG styleguide rules.
#
# NOTE: all vhdl files found by the autodependency search are compiled into the
#       library work

AUTO_DEP_PATHS =  testbench
AUTO_DEP_PATHS += ../../neorv32/rtl/core
AUTO_DEP_PATHS += ../../neorv32/rtl/core/mem
AUTO_DEP_PATHS += ../../neorv32/rtl/processor_templates
AUTO_DEP_PATHS += ../../fpga/ip/busses/vhdl_source
AUTO_DEP_PATHS += ../../fpga/ip/busses/vhdl_bfm
AUTO_DEP_PATHS += ../../target/simulation/packages/vhdl_source
AUTO_DEP_PATHS += ../../fpga/cpu_unit/vhdl_source
AUTO_DEP_PATHS += ../../fpga/ip/clock/vhdl_source
AUTO_DEP_PATHS += ../../fpga/io/itu/vhdl_source
AUTO_DEP_PATHS += ../../fpga/io/uart_lite/vhdl_source
AUTO_DEP_PATHS += ../../fpga/ip/srl_fifo/vhdl_source
AUTO_DEP_PATHS += ../../fpga/ip/sync_fifo/vhdl_source

MODELSIM_NO_WARNINGS := set NumericStdNoWarnings 1

###############################################################################
#### Testbenches
###############################################################################
#### Testbench targets execute a GUI and start the simulation in the GUI.
#### Using the $(TESTBENCH)_DO_GUI_BEFORE a wave window can be loaded. Multiple
#### testbench targets are allowed (use different names).
###############################################################################
#### The variables that can be set for a testbench are described below.
####
#### TESTBENCH 				: Testbench target name
#### TESTBENCHES			: do not change this line
#### $(TESTBENCH)_SRC			: source file containing the testbench
#### $(TESTBENCH)_ENTITY		: testbench entity name
#### $(TESTBENCH)_TIME			: maximum simulation time
#### $(TESTBENCH)_DO_GUI_BEFORE		: command that is executed before the simulation is started
#### $(TESTBENCH)_DO_GUI_AFTER		: command that is executed after the simulation is stopped
#### $(TESTBENCH)_DO_GUI_OVERRIDE	: user specified command. When non empty this is the
####					  command that is executed in vsim. The previous variables
####					  TIME, DO_GUI_BEFORE and DO_GUI_AFTER will have no effect.
####
#### NOTE: spaces after the variable values will introduce errors!!
###############################################################################

# NOTE: Only create testbench targets if there really is a testbench that is not
# the same as a testcase. You can use "make testcase.gui" to run the testcases
# in the graphical environment, instead of adding a separate testbench target here.

#### tb1 #####

TESTBENCH                       := neorv32_boot_tb
TESTBENCHES                     := $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC                := $(TESTBENCH).vhd
$(TESTBENCH)_ENTITY             := $(TESTBENCH)
$(TESTBENCH)_TIME               := 100 us
$(TESTBENCH)_DO_GUI_BEFORE      :=
$(TESTBENCH)_DO_GUI_AFTER       :=
$(TESTBENCH)_DO_GUI_OVERRIDE    :=

TESTBENCH                       := neorv32_boot_appl
TESTBENCHES                     := $(TESTBENCHES) $(TESTBENCH)
$(TESTBENCH)_SRC                := $(TESTBENCH).vhd
$(TESTBENCH)_ENTITY             := $(TESTBENCH)
$(TESTBENCH)_TIME               := 100 us
$(TESTBENCH)_DO_GUI_BEFORE      :=
$(TESTBENCH)_DO_GUI_AFTER       :=
$(TESTBENCH)_DO_GUI_OVERRIDE    :=

###############################################################################
#### Testcases
###############################################################################
#### A testcase target executes an simulation that gives a pass or fail after
#### execution. The testcase is normaly started in commandline mode, but can
#### also be started in the GUI. The testcase should generate a report file.
#### The name of the report file that the testcase should generate is passed by
#### setting the generic g_report_file_name. The report file should contain
#### the string "** Simulation verdict : SUCCESSFUL" when the testcase is
#### successful. The tl_sctb_pkg package can be used for generating these reports.
####
#### Multiple testcase targets are allowed (use different names).
###############################################################################
#### The variables that can be set for a testcase are described below.
####
#### TESTCASE 			: Testcase target name
#### TESTCASES			: do not change this line
#### $(TESTCASE)_SRC		: source file containing the testbench
#### $(TESTCASE)_ENTITY		: entity containing the test script
#### $(TESTCASE)_HARNESS_SRC	: source file of the harness (specify only when not instantiated in the testcase)
#### $(TESTCASE)_HARNESS_ENTITY	: entity name of the harness (specify only when not instantiated in the testcase)
#### $(TESTCASE)_TIME		: maximal simulation time
#### $(TESTCASE)_DO_BEFORE	: command that is executed before simulation starts (command line mode)
#### $(TESTCASE)_DO_AFTER	: command that is executed after the simulation is stopped (command line mode)
#### $(TESTCASE)_DO_OVERRIDE	: see $(TESTCASE)_DO_GUI_OVERRIDE (command line mode)
#### $(TESTCASE)_DO_GUI_BEFORE	: command that is executed before the simulation is started (GUI mode)
#### $(TESTCASE)_DO_GUI_AFTER	: command that is executed after the simulation is stopped (GUI mode)
#### $(TESTCASE)_DO_GUI_OVERRIDE: user specified command. When non empty this is the
#### 				  command that is executed in vsim. The previous variables
#### 				  TIME, DO_GUI_BEFORE and DO_GUI_AFTER will have no effect.
####
#### NOTE: spaces after the variable values will introduce errors!!
###############################################################################

#DURATION				?= 285 ms

TESTCASE 			    := 
TESTCASES			    := $(TESTCASES) $(TESTCASE)
$(TESTCASE)_SRC			:= $(TESTCASE).vhd
$(TESTCASE)_ENTITY		:= $(TESTCASE)
$(TESTCASE)_TIME		:= 1100 us;
$(TESTCASE)_DO_BEFORE     := 
$(TESTCASE)_DO_GUI_BEFORE := 
$(TESTCASE)_VSIM_OPTS     := 
$(TESTCASE)_DO_GUI_AFTER  := 

###############################################################################
#### Regression tests
###############################################################################
#### Regression tests consists of a collection of testcases. These testcases
#### are always executed in command_line mode.
####
#### Multiple regression test targets are allowed (use different names).
###############################################################################
#### The variables that can be set for a testcase are described below.
####
#### REGTEST 			: Regression test target name
#### REGTESTS			: do not change this line
#### $(REGTEST)_TESTCASES	: a space separated list with the testcase target
####				  names (all testcases by default)
####
#### NOTE: spaces after the variable values will introduce errors!!
###############################################################################

#### rt1 #####
REGTEST 		:= rt
REGTESTS		:= $(REGTESTS) $(REGTEST)
$(REGTEST)_TESTCASES	:= $(TESTCASES)

#### rt2 #####
##...

###############################################################################
#### Default rules
###############################################################################

# compile all testcases & testbenches and run all regression tests
#all:    $(TESTBENCHES:%=%.vc) $(TESTCASES:%=%.vc) $(REGTESTS)
all:    $(REGTESTS)

clean:	vsim-clean

info: 	vsim-info

#nano.vc : RAMB16_S9_S18-model.vc
#usb_host_nano.vc : unisim_VCOMP.vc unisim_VPKG.vc RAMB16BWE_S36_S9.vc RAMB16_S9_S18.vc
#tl_stream_bus_sync_buffer.vc : tl_sync_fifo_ft-plain.vc

neorv32_top : neorv32_dmem.default neorv32_imem.default
uart_peripheral_io.vc : srl_fifo-generic.vc

###############################################################################
#### WARNING!!
###############################################################################
#### Do not touch the settings below (these statements should be placed at the
#### end of the makefile)
###############################################################################

# The following variable contains the actual rule file
RULES_INCLUDE := vsim_tools.inc

###############################################################################
#### Automatically find make_includes directory
###############################################################################

CUR_LEV := $(subst /, , $(subst ' ',_, $(CURDIR)))
LEVELS := $(wordlist 3, $(words $(CUR_LEV)),$(CUR_LEV))
$(foreach level, $(LEVELS), $(eval LE := $(LE)../) $(eval MAKE_INCLUDE_SEARCH_PATH := $(MAKE_INCLUDE_SEARCH_PATH) $(LE)make_includes))
GLOBAL_INCS_REL := $(firstword $(wildcard $(MAKE_INCLUDE_SEARCH_PATH)))
GLOBAL_INCS ?= $(CURDIR)/$(GLOBAL_INCS_REL)

export GLOBAL_INCS

###############################################################################
#### Include simulation rules
###############################################################################

RULES_INCLUDE_EXISTS := $(shell [[ -e $(GLOBAL_INCS)/$(RULES_INCLUDE) ]] && echo "ok")

ifneq ($(RULES_INCLUDE_EXISTS),ok)
$(error $(RULES_INCLUDE) include file not found. Please enter path to the make_includes directory manually. Now $(GLOBAL_INCS)/$(RULES_INCLUDE))
endif

include $(GLOBAL_INCS)/$(RULES_INCLUDE)
