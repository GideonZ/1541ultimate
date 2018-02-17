################################################################################
##
## (C) COPYRIGHT 2004-2013 TECHNOLUTION BV, GOUDA NL
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
################################################################################
## VSIM Simulation include file
################################################################################

THIS_MAKE_DEP := $(lastword $(MAKEFILE_LIST))

################################################################################
## Global variables
################################################################################
WORK_DIR	?= work
VWORK_DIR	?= $(WORK_DIR)
VWORK_DIR_DEP	?= $(VWORK_DIR)/_dir
TL_ENV ?= true

VPATH		+= $(PROJ_ROOT)
VPATH		+= $(VWORK_DIR)

PATH		:= $(PATH):$(GLOBAL_INCS)

SCRIPT_PATH := $(GLOBAL_INCS)/scripts

DEFAULT_SIM_LIB	= work	#NOTE: it is not possible to change this lib at the moment

# Prevent spaces to mess-up the makefile
GLOBAL_INCS := $(strip $(GLOBAL_INCS))

# Add sinc sources to VSIM_SOURCES
VSIM_SOURCES += $(notdir $(foreach inc,$(SOURCE_INCLUDES),\
		$(shell  awk --field-separator=\, '{if(NF>0) print "$(dir $(inc))" $$3}'\
		$(inc) | tr "\n" " ")))

# Add auto dep sources to VSIM_SOURCES
VSIM_SOURCES += $(notdir $(wildcard $(addsuffix /*.vhd,$(subst :, ,$(AUTO_DEP_PATHS)))))

VSIM_SOURCES := $(sort $(VSIM_SOURCES)) # remove duplicates


#VSIM_SOURCE_PATHS += $(if $(SOURCE_INCLUDES),$(shell awk -f $(SCRIPT_PATH)/vsinc_paths.awk $(SOURCE_INCLUDES)))
	# NOTE/WOI: Do not add the SOURCE_INCLUDES paths to the VPATH. This breaks
	# the makefile when adding sinc files from other modules, since make will
	# automatically find the "work" target in the other directory as well, so it
	# will not create a work directory in the current directory.
VSIM_SOURCE_PATHS += $(AUTO_DEP_PATHS)
VSIM_SOURCE_PATHS := $(sort $(VSIM_SOURCE_PATHS)) # remove duplicates

VPATH += $(VSIM_SOURCE_PATHS)

LIBS	= $(foreach inc,$(SOURCE_INCLUDES),\
		$(shell  awk --field-separator=\, '{if(NF>0) print $$2}'\
		$(inc) | tr "\n" " "))

LIBS	:= $(sort $(DEFAULT_SIM_LIB) $(LIBS))

################################################################################
## includes
################################################################################
include $(GLOBAL_INCS)/shell.inc

################################################################################
## Toolchain version
################################################################################

THIS_MAKE_DEP := $(THIS_MAKE_DEP) $(lastword $(MAKEFILE_LIST))

# if no location of the tools are defined use the current active Modelsim version in
# the shell

VCOM	= ${TL_ENV} $(ENVIRONMENTS) && vcom
VLOG    = ${TL_ENV} $(ENVIRONMENTS) && vlog
VSIM	= ${TL_ENV} $(ENVIRONMENTS) && vsim
VLIB	= ${TL_ENV} $(ENVIRONMENTS) && vlib
VMAP	= ${TL_ENV} $(ENVIRONMENTS) && vmap
VDEPENDS	?= $(GLOBAL_INCS)/scripts/vdepends

VCOM_FLAGS ?= -93 -quiet -relax
VLOG_FLAGS = +define+COCOTB_SIM -timescale 1ps/1ps -mfcu +acc=rmb -sv -quiet


################################################################################
#### Environment setup
################################################################################


    ############################################################################
    ## Rules
    ############################################################################

$(VWORK_DIR_DEP)	:
	@$(call print_cmd_info,"MK WORK DIR",$(dir $@))
	@$(MKDIR) -p $(dir $@)
	@touch $@

################################################################################
#### Common toolchain
################################################################################

    ############################################################################
    ## Includes
    ############################################################################

ifneq ($(MAKECMDGOALS),vsim-info)
ifneq ($(MAKECMDGOALS),vsim-clean)
ifneq ($(MAKECMDGOALS),vsim-targets)
ifneq ($(MAKECMDGOALS),vsim-drtr)
ifneq ($(MAKECMDGOALS),clean)
  ifneq ($(words $(VSIM_SOURCES)),0)
    -include $(VSIM_SOURCES:%.vhd=$(VWORK_DIR)/%.vc.depend.make)
  endif
  ifneq ($(words $(TESTCASES)),0)
    -include $(TESTCASES:%=$(VWORK_DIR)/%.tc.depend.make)
  endif
  ifneq ($(words $(TESTBENCHES)),0)
    -include $(TESTBENCHES:%=$(VWORK_DIR)/%.tb.depend.make)
  endif
  ifneq ($(words $(REGTESTS)),0)
    -include $(REGTESTS:%=$(VWORK_DIR)/%.rt.depend.make)
  endif
endif
endif
endif
endif
endif

    ############################################################################
    ## Settings
    ############################################################################

.PRECIOUS: %.depend.make %.tc.depend.make


    ############################################################################
    ## Functions
    ############################################################################

# function findstringex
# 	Searches for an exact match of a number of strings
#
#	$(1) search key
#	$(2) strings

findstringex		= $(findstring -$(1)-,$(patsubst %,-%-,$(2)))


# function IS_TC_OR_TB
# 	This function checks if a test case or test bench is registered. It
#	generates an error that stops make when not.
#
#	$(1) test case name

IS_TC_OR_TB		= $(if $(call findstringex,$(1),$(TESTCASES) $(TESTBENCHES)),,\
			  echo -e "\n\nTestcase '$(1)' is not found.\
			  Does it exist?\n";\
			  false)


# function IS_TESTCASE
# 	This function checks if a test case is registered. It generates an error
#	that stops make when not.
#
#	$(1) test case name

IS_TESTCASE		= $(if $(call findstringex,$(1),$(TESTCASES)),,\
			  echo -e "\n\nTestcase '$(1)' is not found.\
			  Does it exist?\n";\
			  false)


# function IS_REGTEST
# 	This function checks if a regression test is registered. It generates
#	an error that stops make when not.
#
#	$(1) regression test name

IS_REGTEST		= $(if $(call findstringex,$(1),$(REGTESTS)),,\
			  echo -e "\n\nRegression test '$(1)' is not found.\
			  Does it exist?\n";\
			  false)


# function TCR_CHECK_CMD
# 	This function returns a command expression that will echo
#	"successful" or "failed" when TCR has passed or failed.
#
#	$(1) test case name

TCR_CHECK_CMD		= ( grep "** Simulation verdict : SUCCESSFUL" \
			  $(VWORK_DIR)/$(1).tcr \
			  > /dev/null && echo "successful" || (echo "failed"; echo $(1) > $(VWORK_DIR)/failed_tc) )

# function TCR_CHECK
# 	This function evaluates the report of a test case and returns a string
# 	"succesful" on test case success or "failed" on test case failure
#
#	$(1) test case name

TCR_CHECK		= $(shell  $(call TCR_CHECK_CMD,$(1)))

# function RTR_CHECK_CMD
# 	This function returns a command expression that evaluates the report
#	of a regression test and echoes "succesful" or "failed"
#
#	$(1) regression test name

RTR_CHECK_CMD		= ( grep "REGRESSION TEST ENDED: SUCCESSFUL" \
			  $(VWORK_DIR)/$(1).rtr \
			  > /dev/null && echo "successful" || echo "failed" )

# function RTR_CHECK
# 	This function evaluates the report of a regression test and echoes
# 	and returns the string "succesful" or "failed"
#
#	$(1) regression test name

RTR_CHECK		= $(shell $(call RTR_CHECK_CMD,$(1)) )

# function RT_PASS_FAIL
# 	This function verifies if the regression test has succeeded.
#	Returns the string "successful" when all test cases have passed, returns
#	an empty string if one or more test cases failed.
#
#	$(1) regression test name

RT_PASS_FAIL		= $(shell expr $(words $(foreach testcase, $($(1)_TESTCASES),\
				     $(findstring $(call TCR_CHECK,$(testcase)),"successful")\
    		                )) = $(words $($(1)_TESTCASES))\
    		                  > /dev/null && echo "successful" )

# function EXTRACT_VHDL_LIB
# Extracts the vhdl library for a vhdl file from the sinc files
#
#	$(1) vhdl file to obtain lib from
#
# returns an empty string if no library is found.

EXTRACT_VHDL_LIB	= $(shell awk --field-separator=, '{if($$3 == "$(1)") printf("%s\n",$$2)}'\
                 		$(VWORK_DIR)/sources.sinc)

# function GET_VHDL_LIB
# This function extracts the library that the vhdl file needs to be compiled
# in.
#
#	$(1) vhdl file to obtain lib from
#
# returns the default library when there is no lib specified in an sinc file

GET_VHDL_LIB		= $(if $(call EXTRACT_VHDL_LIB,$(1)),\
							$(call EXTRACT_VHDL_LIB,$(1)),\
							$(DEFAULT_SIM_LIB)\
					  )



    ############################################################################
    ## Rules
    ############################################################################

$(CONFIG_FILES):
	$(error "The config file(s) '$(CONFIG_FILES)' configured in variable 'CONFIG_FILES' does not exist. Make sure the variable config files points to existing config files (makefiles). Ensure also the casing is correct.")
	
	
    ############################################################################
    ## Source include file generation
    ############################################################################
    ## Generate the dependencies in the source include files
    ############################################################################

$(VWORK_DIR)/sources.sinc: $(VWORK_DIR_DEP) $(SOURCE_INCLUDES)
	@$(call print_cmd_info,"SOURCE INC",$@)
	@$(call purge, $(VWORK_DIR)/sources.sinc)
	@$(if $(SOURCE_INCLUDES),\
	   awk -f $(SCRIPT_PATH)/vsinc_strip_path.awk $(SOURCE_INCLUDES) > $(VWORK_DIR)/sources.sinc)
	@touch $@


    ############################################################################
    ## Library generation
    ############################################################################
    ## Generates the libraries
    ############################################################################

%/_lib: $(VWORK_DIR_DEP)
	@$(call print_cmd_info,"VLIB",$*)
	@$(VLIB) $(VWORK_DIR)/$*
	@$(VMAP) $* $(VWORK_DIR)/$* > /dev/null
	@touch $(VWORK_DIR)/$*/_lib

    ############################################################################
    ## Compilation
    ############################################################################
    ## Compiles the vhd files
    ############################################################################

$(VWORK_DIR)/%.vc.depend.make: %.v $(VWORK_DIR_DEP) $(VWORK_DIR)/sources.sinc $(CONFIG_FILES)
	@$(call print_cmd_info_nonl,"VERILOG DEP",$<)
	@$(if $(shell awk --field-separator=, '{if($$3 == "$(<F)") printf("%s\n",$$3)}'\
                 $(VWORK_DIR)/sources.sinc), \
			echo " -> (sinc)";\
			echo $(patsubst %.v,%.vc,\
			    $(shell awk --field-separator=, '{if($$3 == "$(<F)") \
			    printf("%s : %s %s/_lib\n",$$3,$$4,$$2)}' \
				$(VWORK_DIR)/sources.sinc)) > $(VWORK_DIR)/$(@F),\
			echo " -> (auto dep)";\
			$(VDEPENDS) $< $(basename $(VSIM_SOURCES)) > $(VWORK_DIR)/$(@F)\
		)
	@echo VERILOG_LIB_$(*F) = $(call VERILOG_LIB_,$(<F)) >> $(VWORK_DIR)/$(@F)

$(VWORK_DIR)/%.vc.depend.make: %.vhd $(VWORK_DIR_DEP) $(VWORK_DIR)/sources.sinc $(CONFIG_FILES)
	@$(call print_cmd_info_nonl,"VHD DEP",$<)
	@$(if $(shell awk --field-separator=, '{if($$3 == "$(<F)") printf("%s\n",$$3)}'\
                 $(VWORK_DIR)/sources.sinc), \
			echo " -> (sinc)";\
			echo $(patsubst %.vhd,%.vc,\
			    $(shell awk --field-separator=, '{if($$3 == "$(<F)") \
			    printf("%s : %s %s/_lib\n",$$3,$$4,$$2)}' \
				$(VWORK_DIR)/sources.sinc)) > $(VWORK_DIR)/$(@F),\
			echo " -> (auto dep)";\
			$(VDEPENDS) $< $(basename $(VSIM_SOURCES)) > $(VWORK_DIR)/$(@F)\
		)
	@echo VHDL_LIB_$(*F) = $(call GET_VHDL_LIB,$(<F)) >> $(VWORK_DIR)/$(@F)

%.vc: %.v
	@$(call print_cmd_info,"VLOG","$< - ($(VERILOG_LIB_$(*F)))")
	@$(VLOG) $(VLOG_FLAGS) -work $(VERILOG_LIB_$(*F)) $< || (rm -f $(VWORK_DIR)/$(@F); false)
	@touch $(VWORK_DIR)/$(@F)

%.vc: %.vhd
	@$(call print_cmd_info,"VCOM","$< - ($(VHDL_LIB_$(*F)))")
	@$(VCOM) $(VCOM_FLAGS) -work $(VHDL_LIB_$(*F)) $< || (rm -f $(VWORK_DIR)/$(@F); false)
	@touch $(VWORK_DIR)/$(@F)

################################################################################
#### PHONY targets
################################################################################
#### PHONY targets are used to trigger toolchain and execute additional commands
################################################################################

    ############################################################################
    ## vtest - Makefile debugging only
    ############################################################################
vtest::
	echo $(VSIM_SOURCES)
	echo $(VPATH)
	
	


    ############################################################################
    ## Clean
    ############################################################################
    ## Commands to clean the environment
    ############################################################################

.PHONY: vsim-clean
vsim-clean::
	 @$(call print_cmd_info,"CLEANUP",$(VWORK_DIR))
	-@$(RMDIR) $(VWORK_DIR)
	-@$(RM) modelsim.ini transcript vsim.wlf
	-@$(RM) dataset.asdb library.cfg
