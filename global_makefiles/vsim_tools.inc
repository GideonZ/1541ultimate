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

include $(GLOBAL_INCS)/vsim_tools_common.inc

    ############################################################################
    ## TC report gen
    ############################################################################
    ## Simulates a test case and generates the test case reports (assuming the
    ## sctb package is used).
    ##
    ## Note that the dependencies are generated and included separately
    ## (%.tc.depend.make)
    ############################################################################

$(VWORK_DIR)/%.tb.depend.make: $(VWORK_DIR_DEP) $(CONFIG_FILES)
	@$(call print_cmd_info,"TB DEP",$@)
	@echo $*.gui: $(notdir $($(*F)_SRC:.vhd=.vc)) > $@



    ## Add default rule for test bench names
$(foreach testbench,$(TESTBENCHES),$(eval $(testbench): $(testbench).gui))

    ############################################################################
    ## TC report gen
    ############################################################################
    ## Simulates a test case and generates the test case reports (assuming the
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
		run 1 ns; restart -f; run $($(*F)_TIME); $($(*F)_DO_AFTER); exit;"\
		> $(VWORK_DIR)/$(@F)\
	 )


%.tcr: %.tcr.do $(CONFIG_FILES)
	@$(call print_cmd_info_nonl,"TCR VSIM",$(*F))
	@$(call IS_TESTCASE,$(*F))
	@rm -f $(VWORK_DIR)/$(@F).log
	@rm -f $(VWORK_DIR)/$(@F)
	@$(VSIM) -do $(VWORK_DIR)/$(*F).tcr.do $($(*F)_VSIM_OPTS) \
	-gg_report_file_name=$(VWORK_DIR)/$(@F) \
	-c $($(*F)_HARNESS_ENTITY) $($(*F)_ENTITY) > $(VWORK_DIR)/$(@F).log && \
	echo -n " -> "; $(call TCR_CHECK_CMD,$(*F)) || echo " -> failed";

    ############################################################################
    ## TC check
    ############################################################################
    ## Checks the result of all test case reports. Rebuilds the reports if
    ## required.
    ############################################################################

.PHONY: %.tc
%.tc: %.tcr  $(CONFIG_FILES)
	@$(call print_cmd_info_nonl,"TC CHECK",$(*F) )
	@echo " -> $(call TCR_CHECK,$(*F))"


    ## add default rule for test case name
$(foreach testcase,$(TESTCASES),$(eval $(testcase): $(testcase).tc))


    ############################################################################
    ## Regression test
    ############################################################################
    ## Executes all test cases for a regression test.
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
	@echo -n " -> "; $(call RTR_CHECK_CMD,$(*F))

    ############################################################################
    ## RT check
    ############################################################################
    ## Checks the result of a regression test
    ############################################################################

.PHONY: %.rt
%.rt: %.rtr
	@$(call print_cmd_info_nonl,"RT CHECK",$(*F) )
	@echo " -> $(call RTR_CHECK,$(*F))"
	@$(if $(call RT_PASS_FAIL,$(*F)),true, cat $(VWORK_DIR)/$*.rtr)
	@$(if $(call RT_PASS_FAIL,$(*F)),true, false)

$(foreach regtest,$(REGTESTS),$(eval $(regtest): $(regtest).rt))



    ############################################################################
    ## Dump reports
    ############################################################################
    ## Dumps the test case report (does not perform a rebuild if files are
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
    ## Runs a test case or test bench in the VSIM GUI. This process is started
    ## in the background.
    ############################################################################

VSIM_GUI_CMD = 	$(VSIM) -gui $($(*F)_VSIM_OPTS) -do $(VWORK_DIR)/$(*F).gui.do\
		-gg_report_file_name=$(VWORK_DIR)/$(*F).gui.tcr \
		$($(*F)_HARNESS_ENTITY) $($(*F)_ENTITY) -gui


.PRECIOUS: %.gui.do
ENVIRONMENTS ?= 
# create different GUI commands for riviera an modelsim
ifneq (,$(findstring riviera,$(ENVIRONMENTS)))
%.gui.do:  $(CONFIG_FILES)
	@$(call print_cmd_info,"VSIM GUI DO GEN",$(@))
	$(if $($(*F)_DO_GUI_OVERRIDE),\
		@echo $($(*F)_DO_GUI_OVERRIDE) > $(VWORK_DIR)/$(@F),\
		@echo 'onerror {break}; onbreak {break}; \
			proc save_range {} {global last_range; set last_range [wv.zoom.dump]; wv.cursors.dump -onlyactive > ./work/activecursor.xml; list}; \
			proc load_range {} {global last_range; set endpoints [split $$last_range "-"]; wv.zoom.range -from [lindex $$endpoints 0] -to [lindex $$endpoints 1]; set cursor_fp [open ./work/activecursor.xml]; set cursor_xml [read $$cursor_fp]; close $$cursor_fp; regexp {value="([^"]*)"} $$cursor_xml cursor_match cursor_position; wv.cursors.add -time $$cursor_position; wv.cursors.removeall; list}; \
			' "proc remake {} {exec make $($(*F)_SRC:.vhd=.vc)}; \
			proc run_once   {} {$($(*F)DO_GUI_BEFORE); run $($(*F)RUN_TIME); $($(*F)DO_GUI_AFTER)}; \
			proc rerun      {} {save_range; remake; restart; run_once; load_range}; \
			run_once"\
		> $(VWORK_DIR)/$(@F)\
	 )
else
%.gui.do:  $(CONFIG_FILES)
	@$(call print_cmd_info,"VSIM GUI DO GEN",$(@))
	$(if $($(*F)_DO_GUI_OVERRIDE),\
		@echo $($(*F)_DO_GUI_OVERRIDE) > $(VWORK_DIR)/$(@F),\
		@echo "onerror {resume}; onbreak {resume}; \
                proc remake {} {exec make $($(*F)_SRC:.vhd=.vc)}; \
                proc rerun {}  {set last_range [wave zoom range]; remake; restart; run $($(*F)_TIME); eval wave\ zoom\ range\ [set last_range]; after 500 focus [view wave]; list }; \
                $($(*F)_DO_GUI_BEFORE); \
		run $($(*F)_TIME); $($(*F)_DO_GUI_AFTER)"\
		> $(VWORK_DIR)/$(@F)\
	 )
endif

.PHONY: %.gui
%.gui: %.gui.do $(CONFIG_FILES)
	@$(call print_cmd_info,"VSIM GUI (BG)","Running simulation '$*' in GUI as background process")
	@$(call IS_TC_OR_TB,$(*F))
	@$(if $(MODELSIM),\
                echo "This make command can not be run from the modelsim shell.",\
                $(VSIM_GUI_CMD) > $(VWORK_DIR)/$(@F).log &\
        )

.PHONY: gui
gui: $(VWORK_DIR)/failed_tc
	$(MAKE) $(shell cat $<).gui

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
	@$(ECHO) "***   <file>.vc                  : Compiles <file>.vhd"
	@$(ECHO) "*** Virtual targets"
	@$(ECHO) "***   vsim-clean                 : Cleans all files that are generated"
	@$(ECHO) "***   vsim-info                  : This info"
	@$(ECHO) "***   vsim-targets               : Shows all available test benches, test cases and"
	@$(ECHO) "***                                regression tests."
	@$(ECHO) "***   vsim-drtr                  : Shows all regression test reports."
	@$(ECHO) "***   <testcase/regression_test> : Gives the result of a test case or regression test."
	@$(ECHO) "***                                Executes a new simulation when required."
	@$(ECHO) "***   <testcase/testbench>.gui   : Starts a Vsim GUI with the given test case or"
	@$(ECHO) "***                                test bench."
	@$(ECHO) "***   <testcase>.dtcr            : Dumps the test case report."
	@$(ECHO) "***   <regression_test>.drtr     : Dumps the regression test report."
	@$(ECHO) "*****************************************************************************************"

vsim-targets::
	@$(ECHO) "*****************************************************************************************"
	@$(ECHO) "*** Available project targets"
	@$(ECHO) "*****************************************************************************************"
	@$(ECHO) "*** Test benches     :"
	@$(foreach testbench,$(TESTBENCHES),$(ECHO) "***   $(testbench)";)
	@$(ECHO) "*** "
	@$(ECHO) "*** Test cases       :"
	@$(foreach testcase,$(TESTCASES),$(ECHO) "***   $(testcase)";)
	@$(ECHO) "*** "
	@$(ECHO) "*** Regression tests :"
	@$(foreach regtest,$(REGTESTS),$(ECHO) "***   $(regtest)";)
	@$(ECHO) "*****************************************************************************************"

vsim-drtr::
	@$(ECHO) "*****************************************************************************************"
	@$(ECHO) "*** Regression tests :"
	@$(foreach regtest,$(REGTESTS), $(ECHO) "***   $(regtest)" ; cat $(VWORK_DIR)/$(regtest).rtr;)
	@$(ECHO) "*****************************************************************************************"
