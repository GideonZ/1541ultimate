################################################################################
##
## (C) COPYRIGHT 2010 Gideon's Logic Architectures'
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


VSIM_SOURCE_PATHS += $(if $(SOURCE_INCLUDES),$(shell awk -f $(SCRIPT_PATH)/vsinc_paths.awk $(SOURCE_INCLUDES)))
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

VCOM	?= vcom
VSIM	?= vsim
VLIB	?= vlib
VMAP	?= vmap
VDEPENDS	?= $(GLOBAL_INCS)/scripts/vdepends

VCOM_FLAGS = -93 -quiet


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
# 	This function checks if a testcase or testbench is registered. It
#	generates an error that stops make when not.
#
#	$(1) testcase name

IS_TC_OR_TB		= $(if $(call findstringex,$(1),$(TESTCASES) $(TESTBENCHES)),,\
			  echo -e "\n\nTestcase '$(1)' is not found.\
			  Does it exist?\n";\
			  false)


# function IS_TESTCASE
# 	This function checks if a testcase is registered. It generates an error
#	that stops make when not.
#
#	$(1) testcase name

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
#	$(1) testcase name

TCR_CHECK_CMD		= ( grep "** Simulation verdict : SUCCESSFUL" \
			  $(VWORK_DIR)/$(1).tcr \
			  > /dev/null && echo "successful" || echo "failed" )

# function TCR_CHECK
# 	This function evaluates the report of a testcase and returns a string
# 	"succesful" on testcase success or "failed" on testcase failure
#
#	$(1) testcase name

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
#	Returns the string "successful" when all testcases have passed, returns
#	an empty string if one or more testcases failed.
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


$(VWORK_DIR)/%.vc.depend.make: %.vhd $(VWORK_DIR_DEP) $(VWORK_DIR)/sources.sinc $(CONFIG_FILES)
	@$(call print_cmd_info_nonl,"VHD DEP",$<)
	@$(if $(shell awk --field-separator=, '{if($$3 == "$(<F)") printf("%s\n",$$3)}'\
                 $(VWORK_DIR)/sources.sinc), \
			echo "-> (sinc)";\
			echo $(patsubst %.vhd,%.vc,\
			    $(shell awk --field-separator=, '{if($$3 == "$(<F)") \
			    printf("%s : %s %s/_lib\n",$$3,$$4,$$2)}' \
				$(VWORK_DIR)/sources.sinc)) > $(VWORK_DIR)/$(@F),\
			echo "-> (auto dep)";\
			$(VDEPENDS) $< $(basename $(VSIM_SOURCES)) > $(VWORK_DIR)/$(@F)\
		)
	@echo VHDL_LIB_$(*F) = $(call GET_VHDL_LIB,$(<F)) >> $(VWORK_DIR)/$(@F)

%.vc: %.vhd
	@$(call print_cmd_info,"VCOM","$< - ($(VHDL_LIB_$(*F)))")
	$(VCOM) $(VCOM_FLAGS) -work $(VHDL_LIB_$(*F)) $< || (rm -f $(VWORK_DIR)/$(@F); false)
	@touch $(VWORK_DIR)/$(@F)

    ############################################################################
    ## TC report gen
    ############################################################################
    ## Simulates a testcase and generates the test case reports (assuming the
    ## sctb package is used).
    ##
    ## Note that the dependencies are generated and included separately
    ## (%.tc.depend.make)
    ############################################################################

$(VWORK_DIR)/%.tb.depend.make: $(VWORK_DIR_DEP) $(CONFIG_FILES)
	@$(call print_cmd_info,"TB DEP",$@)
	@echo $*.gui: $(notdir $($(*F)_SRC:.vhd=.vc)) > $@



    ## add default rule for testbench names
$(foreach testbench,$(TESTBENCHES),$(eval $(testbench): $(testbench).gui))

    ############################################################################
    ## TC report gen
    ############################################################################
    ## Simulates a testcase and generates the test case reports (assuming the
    ## sctb package is used).
    ##
    ## Note that the dependencies are generated and included separately
    ## (%.tc.depend.make)
    ############################################################################

$(VWORK_DIR)/%.tc.depend.make: $(VWORK_DIR_DEP) $(CONFIG_FILES)
	@$(call print_cmd_info,"TC DEP",$@)
	@echo $*.tcr: $(notdir $($(*F)_SRC:.vhd=.vc)) \
	       $(notdir $($(*F)_HARNESS_SRC:.vhd=.vc))\
	       " $(CONFIG_FILES)"\
	       > $@
	@echo $*.gui: $(notdir $($(*F)_SRC:.vhd=.vc)) \
	       $(notdir $($(*F)_HARNESS_SRC:.vhd=.vc)) \
	       " $(CONFIG_FILES)"\
	       >> $@

.PRECIOUS: %.tcr.do

%.tcr.do: $(CONFIG_FILES)
	@$(call print_cmd_info,"VSIM TCR DO GEN",$(@))
	$(if $($(*F)_DO_OVERRIDE),\
		@echo $($(*F)_DO_OVERRIDE) > $(VWORK_DIR)/$(@F),\
		@echo "onerror {resume}; onbreak {resume}; $($(*F)_DO_BEFORE); \
		run $($(*F)_TIME); $($(*F)_DO_AFTER); exit;"\
		> $(VWORK_DIR)/$(@F)\
	 )


%.tcr: %.tcr.do $(CONFIG_FILES)
	@$(call print_cmd_info_nonl,"TCR VSIM",$(*F))
	@$(call IS_TESTCASE,$(*F))
	@$(VSIM) -do $(VWORK_DIR)/$(*F).tcr.do $($(*F)_VSIM_OPTS) \
	-gg_report_file_name=$(VWORK_DIR)/$(@F) \
	-c $($(*F)_HARNESS_ENTITY) $($(*F)_ENTITY) > $(VWORK_DIR)/$(@F).log && \
	echo -n "-> "; $(call TCR_CHECK_CMD,$(*F)) || echo "-> failed";

    ############################################################################
    ## TC check
    ############################################################################
    ## Checks the result of all testcase reports. Rebuilds the reports if
    ## required.
    ############################################################################

.PHONY: %.tc
%.tc: %.tcr  $(CONFIG_FILES)
	@$(call print_cmd_info_nonl,"TC CHECK",$(*F) )
	@echo "-> $(call TCR_CHECK,$(*F))"


    ## add default rule for testcase name
$(foreach testcase,$(TESTCASES),$(eval $(testcase): $(testcase).tc))


    ############################################################################
    ## Regression test
    ############################################################################
    ## Executes all testcases for a regression test
    ##
    ## Note that the dependencies are generated and included separately
    ## (%.rt.depend.make)
    ############################################################################

$(VWORK_DIR)/%.rt.depend.make: $(VWORK_DIR_DEP) $(CONFIG_FILES)
	@$(call print_cmd_info,"RT DEP",$(*F))
	@echo $*.rtr: $($(*F)_TESTCASES:=.tcr) \
		" $(CONFIG_FILES)"\
	       > $@


%.rtr:	 $(CONFIG_FILES)
	@$(call print_cmd_info_nonl,"RT REP",$(*F))
	@$(call IS_REGTEST,$(*F))
	@$(call purge,$(VWORK_DIR)/$(@F))
	@$(foreach testcase, $($(*F)_TESTCASES), echo \
	"$(testcase) -> " $(call TCR_CHECK,$(testcase)) >> $(VWORK_DIR)/$(@F);)
	@$(if $(call RT_PASS_FAIL,$(*F)),\
		echo -e "\nREGRESSION TEST ENDED: SUCCESSFUL" >> $(VWORK_DIR)/$(@F),\
		echo -e "\nREGRESSION TEST ENDED: FAILED" >> $(VWORK_DIR)/$(@F)\
	)
	@echo -n "-> "; $(call RTR_CHECK_CMD,$(*F))

    ############################################################################
    ## RT check
    ############################################################################
    ## Checks the result of a regression test
    ############################################################################

.PHONY: %.rt
%.rt: %.rtr
	@$(call print_cmd_info_nonl,"RT CHECK",$(*F) )
	@echo "-> $(call RTR_CHECK,$(*F))"

$(foreach regtest,$(REGTESTS),$(eval $(regtest): $(regtest).rt))

################################################################################
#### PHONY targets
################################################################################
#### PHONY targets are used to trigger toolchain and execute additional commands
################################################################################

    ############################################################################
    ## vtest - Makefile debugging only
    ############################################################################
vtest::
	echo vtest
	env


    ############################################################################
    ## Clean
    ############################################################################
    ## Commands to clean the environment
    ############################################################################

.PHONY: vsim-clean
vsim-clean::
	 @$(call print_cmd_info,"CLEANUP",$(VWORK_DIR))
	-@$(RMDIR) $(VWORK_DIR)

    ############################################################################
    ## Dump reports
    ############################################################################
    ## Dumps the testcase report (does not perform a rebuild if files are
    ## modified)
    ############################################################################

.PHONY: %.dtcr %.drtr

%.dtcr: %.tcr  $(CONFIG_FILES)
	@$(call print_cmd_info,"VTCR","Dump of '$(VWORK_DIR)/$(*F).tcr'")
	@$(call IS_TESTCASE,$(*F))
	@cat $(VWORK_DIR)/$(*F).tcr

%.drtr: %.rtr  $(CONFIG_FILES)
	@$(call print_cmd_info,"VTCR","Dump of '$(VWORK_DIR)/$(*F).tcr'")
	@$(call IS_REGTEST,$(*F))
	@cat $(VWORK_DIR)/$(*F).rtr

    ############################################################################
    ## VSIM GUI
    ############################################################################
    ## Runs a testcase or testbench in the VSIM GUI. This process is started in
    ## the background.
    ############################################################################

VSIM_GUI_CMD = 	$(VSIM) -gui $($(*F)_VSIM_OPTS) -do $(VWORK_DIR)/$(*F).gui.do\
		-gg_report_file_name=$(VWORK_DIR)/$(*F).gui.tcr \
		$($(*F)_HARNESS_ENTITY) $($(*F)_ENTITY) -gui


.PRECIOUS: %.gui.do
%.gui.do:  $(CONFIG_FILES)
	@$(call print_cmd_info,"VSIM GUI DO GEN",$(@))
	$(if $($(*F)_DO_GUI_OVERRIDE),\
		@echo $($(*F)_DO_GUI_OVERRIDE) > $(VWORK_DIR)/$(@F),\
		@echo "onerror {resume}; onbreak {resume}; \
                proc remake {} {exec make $($(*F)_SRC:.vhd=.vc)}; \
                proc rerun {}  {exec make $($(*F)_SRC:.vhd=.vc); restart -f; run $($(*F)_TIME)}; \
                $($(*F)_DO_GUI_BEFORE); \
		run $($(*F)_TIME); $($(*F)_DO_GUI_AFTER)"\
		> $(VWORK_DIR)/$(@F)\
	 )

.PHONY: %.gui
%.gui: %.gui.do $(CONFIG_FILES)
	@$(call print_cmd_info,"VSIM GUI (BG)","Running simulation '$*' in GUI as background process")
	@$(call IS_TC_OR_TB,$(*F))
	@$(if $(MODELSIM),\
                echo "This make command can not be run from the modelsim shell.",\
                $(VSIM_GUI_CMD) > $(VWORK_DIR)/$(@F).log &\
        )

    ############################################################################
    ## Build information
    ############################################################################
    ## Displays all possible build targets
    ############################################################################

.PHONY: vsim-info vsim-targets
vsim-info::
	@$(ECHO) "*****************************************************************************************"
	@$(ECHO) "*** VSIM include					   "
	@$(ECHO) "*****************************************************************************************"
	@$(ECHO) "*** File targets"
	@$(ECHO) "***   <file>.vc                  : compiles <file>.vhd"
	@$(ECHO) "*** Virtual targets"
	@$(ECHO) "***   vsim-clean                 : cleans all files that are generated"
	@$(ECHO) "***   vsim-info                  : this info"
	@$(ECHO) "***   vsim-targets               : Shows all available testbenches,testcases and "
	@$(ECHO) "***                                regression tests"
	@$(ECHO) "***   <testcase/regression_test> : gives the result of an testcase/regression test. "
	@$(ECHO) "***                                Executes a new simulation when required."
	@$(ECHO) "***   <testcase/testbench>.gui   : starts a vsim gui with the given testcase or testbench"
	@$(ECHO) "***   <testcase>.dtcr            : dumps the testcase report"
	@$(ECHO) "***   <regression_test>.drtr     : dumps the regression test report"
	@$(ECHO) "*****************************************************************************************"

vsim-targets::
	@$(ECHO) "*****************************************************************************************"
	@$(ECHO) "*** Available project targets"
	@$(ECHO) "*****************************************************************************************"
	@$(ECHO) "*** Testbenches      :"
	@$(foreach testbench,$(TESTBENCHES),$(ECHO) "***   $(testbench)";)
	@$(ECHO) "*** "
	@$(ECHO) "*** Testcases        :"
	@$(foreach testcase,$(TESTCASES),$(ECHO) "***   $(testcase)";)
	@$(ECHO) "*** "
	@$(ECHO) "*** Regression tests :"
	@$(foreach regtest,$(REGTESTS),$(ECHO) "***   $(regtest)";)
	@$(ECHO) "*****************************************************************************************"
