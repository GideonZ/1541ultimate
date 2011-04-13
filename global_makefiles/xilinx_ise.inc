################################################################################
##
## (C) COPYRIGHT 2010 Gideon's Logic Architectures'
##
################################################################################
## XILINX ISE include file
################################################################################

THIS_MAKE_DEP := $(lastword $(MAKEFILE_LIST))

################################################################################
## Global variables
################################################################################
WORK_DIR	?= work
XWORK_DIR	?= $(WORK_DIR)
XWORK_DIR_DEP	?= $(XWORK_DIR)/_dir

DEP_VAR_LIST	= XST_FLAGS_DEP	NGD_FLAGS_DEP MAP_FLAGS_DEP PAR_FLAGS_DEP \
		  TRCE_FLAGS_DEP BITGEN_FLAGS_DEP PROMGEN_FLAGS_DEP



SYNTH_SOURCES	:= $(shell echo $(SYNTH_PROJ) | tr " " "\n" |\
 				awk --field-separator=\, '{print $$3}' | tr "\n" " ")

IS_CPLD = $(findstring xc95,$(PART))

SCRIPT_PATH = $(GLOBAL_INCS)/scripts

.NOTPARALLEL:

################################################################################
## includes
################################################################################
include $(GLOBAL_INCS)/shell.inc
THIS_MAKE_DEP := $(THIS_MAKE_DEP) $(lastword $(MAKEFILE_LIST))

################################################################################
## Toolchain version
################################################################################

# get the locations of ISE programs from a separate file
-include $(GLOBAL_INCS)/ise_locations.inc 	# project default locations file
-include $(ISE_LOCATIONS_FILE_PATH)			# A user can set an environment variable
											# that points to the global ISE locations
											# file.

ifneq ($(ISE_LOCATIONS_FILE_INCLUDED),yes)
$(warning "WARNING: The file containing the ISE tool locations is not found. \
   			  The environment settings will be used. However, this could be\
           wrong. Create an ISE locations file to get rid of this warning!")
endif

ifneq ($(ISE_LOCATIONS_FILE_USER),yes)
$(warning "WARNING: No USER DEFINED ISE locations file file used!")
endif

THIS_MAKE_DEP := $(THIS_MAKE_DEP) $(lastword $(MAKEFILE_LIST))

# if no location of the tools are defined use the current active ISE version in
# the shell
XST 		?= $(error "XST location not specified. Update ise_locations file")
NGDBUILD 	?= $(error "NGDBUILD location not specified. Update ise_locations file")
MAP			?= $(error "MAP location not specified. Update ise_locations file")
PAR			?= $(error "PAR location not specified. Update ise_locations file")
TRCE		?= $(error "TRCE location not specified. Update ise_locations file")
BITGEN		?= $(error "BITGEN location not specified. Update ise_locations file")
CPLDFIT		?= $(error "CPLDFIT location not specified. Update ise_locations file")
HPREP6		?= $(error "HPREP6 location not specified. Update ise_locations file")
FPGA_EDITOR	?= $(error "FPGA_EDITOR location not specified. Update ise_locations file")
TIMINGAN	?= $(error "TIMINGAN location not specified. Update ise_locations file")
TAENGINE	?= $(error "TAENGINE location not specified. Update ise_locations file")
PROMGEN		?= $(error "PROMGEN location not specified. Update ise_locations file")
TSIM		?= $(error "TSIM location not specified. Update ise_locations file")
NETGEN		?= $(error "NETGEN location not specified. Update ise_locations file")

CPS_INS		?= $(error "Chipscope inserter location not specified. Update ise_locations file")

ifeq ($(ISE_VERSION),9.1)
ISE_FEATURE_SMARTGUIDE := yes
endif

################################################################################
#### Environment setup
################################################################################
.PRECIOUS:	$(XWORK_DIR_DEP) $(XWORK_DIR)/%.sources $(XWORK_DIR)/%.config
.PRECIOUS:	$(XWORK_DIR)/%.mkenv $(XWORK_DIR)/%.bmm $(XWORK_DIR)/%.mkenv
.PRECIOUS:	$(XWORK_DIR)/%.ucf

    ############################################################################
    ## includes
    ############################################################################

# The following include file is used to trigger the regeneration of the
# variable dependency files.

ifneq ($(MAKECMDGOALS),info)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),xilinx-info)
ifneq ($(MAKECMDGOALS),xilinx-test)
ifneq ($(MAKECMDGOALS),xilinx-clean)
-include $(XWORK_DIR)/$(TARGET).vinc
-include $(XWORK_DIR)/$(TARGET).filecheck
endif
endif
endif
endif
endif

    ############################################################################
    ## Functions
    ############################################################################

# function GEN_VAR_DEP_FILE
# 	This function generates a dependency file that changes when the listed
#	flag values change.
#
#	$(1) Dependency variables
#	$(2) Dep file location

GEN_VAR_DEP_FILE	= echo DEP_VARS = $(1) > $(2).new;\
			  $(foreach var,$(1), echo -n "$(var) = " >> $(2).new; \
				$(foreach el,$($(var)), echo -n "$(el) " >> $(2).new;)\
			  	echo "" >> $(2).new; ) \
 			  diff -N $(2).new $(2) > /dev/null || \
		        	{ { [[ -e $(2) ]] && $(call print_cmd_info,"INFO","$(2) out dated"); }; \
				  cp $(2).new $(2); };

    ############################################################################
    ## Variables
    ############################################################################
USED_FILES := $(BMM_FILE) $(UCF_FILES) $(SYNTH_SOURCES)
NON_EXISTING_FILES := $(strip $(foreach file, $(USED_FILES), $(if $(call file_exists,$(file)),,$(file))))


    ############################################################################
    ## Rules
    ############################################################################

$(XWORK_DIR_DEP)	:
	@$(call print_cmd_info,"MK WORK DIR",$(dir $@))
	@$(MKDIR) -p $(dir $@)
	@touch $@

$(XWORK_DIR)/$(TARGET).filecheck: $(XWORK_DIR_DEP) $(XWORK_DIR)/$(TARGET).config $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"CHECK FILES","Checking file locations")
	@$(if $(NON_EXISTING_FILES), echo "ERROR: The following files do not exist:"; echo $(NON_EXISTING_FILES) | tr " " "\n" | cat; false,)
	@touch -t 197001010100 $@ # always execute the file check

$(XWORK_DIR)/$(TARGET).sources	: $(XWORK_DIR_DEP) $(SYNTH_SOURCES) $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"INFO","The sources are newer than the target")
	@touch $(XWORK_DIR)/$(TARGET).sources

$(XWORK_DIR)/$(TARGET).ucf	: $(XWORK_DIR_DEP) $(UCF_FILES) $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"UCF GEN",$@)
	@echo -n "" > $(XWORK_DIR)/$(TARGET).ucf
	@$(foreach ucf,$(UCF_FILES),echo -e "\n\n### Start of $(ucf) ###\n\n" >> \
	   $(XWORK_DIR)/$(TARGET).ucf; cat $(ucf) >> $(XWORK_DIR)/$(TARGET).ucf;)

$(XWORK_DIR)/$(TARGET).bmm	: $(XWORK_DIR_DEP) $(BMM_FILE) $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"BMM GEN",$@)
	@cp	$(BMM_FILE) $(XWORK_DIR)/$(TARGET).bmm

$(XWORK_DIR)/$(TARGET).config	: $(XWORK_DIR_DEP) $(CONFIG) $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"INFO","The configuration is newer than the target")
	@touch $(XWORK_DIR)/$(TARGET).config

$(XWORK_DIR)/$(TARGET).mkenv	: #$(THIS_MAKE_DEP)
	$(if  $(call file_exists,$@),\
		@$(call print_cmd_info,"INFO","One of the make files has changed (complete rebuild required)"),\
	)
	@touch $(XWORK_DIR)/$(TARGET).mkenv

$(XWORK_DIR)/$(TARGET).vinc: $(XWORK_DIR)/$(TARGET).config $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"MK VAR DEPS","Generating dependency files for the configuration values")
	@$(foreach var_list,$(DEP_VAR_LIST),\
		$(call GEN_VAR_DEP_FILE,$($(var_list)_VARS),$($(var_list)))\
	)
	@echo "# This is a dummy file. Generated on " `date` > $(XWORK_DIR)/$(TARGET).vinc

$(XWORK_DIR)/%.vdep:
	$(call print_cmd_info,"ERROR","File $(*F) not found.")
	$(error "To fix this error try a rebuild from scratch. $(*F)")

################################################################################
#### Common toolchain
################################################################################

    ############################################################################
    ## Settings
    ############################################################################
.PRECIOUS: %.ngc %.ngd

    ############################################################################
    ## Variables
    ############################################################################

## INTSTYLE         	?= -intstyle ise      # call Xilinx tools in ise mode

XWORK_DIR_XST_DEP	= $(XWORK_DIR)/xst/_dir
XWORK_DIR_XST		= $(XWORK_DIR)/xst

XST_FLAGS        	+= $(INTSTYLE)
NGDBUILD_FLAGS   	+= $(INTSTYLE)

XST_FLAGS_DEP	 	= $(XWORK_DIR)/$(TARGET)_XST_FLAGS_DEP.vdep
XST_FLAGS_DEP_VARS	= ISE_VERSION DESIGN_NAME TOP_NAME PART XST_LSO XST_FLAGS \
			  XST_OPTIONS SYNTH_PROJ

NGD_FLAGS_DEP		= $(XWORK_DIR)/$(TARGET)_NGD_FLAGS_DEP.vdep
NGD_FLAGS_DEP_VARS	= PART UCF_FILES BMM_FILE NGDBUILD_FLAGS


BMM_DEP		 	= $(if $(BMM_FILE),$(XWORK_DIR)/$(TARGET).bmm,)

OUTPUT_MASTER_LOG	= $(TARGET).master.outlog

## declare absolute BMM path (this one should be absolute for correct operation
## of the FPGA editor. On cygwin platforms the path should start with a drive
## letter and contain forward slashes e.g. c:/

ifeq ($(IS_CYGWIN),yes)
   BMM_LOCATION	= $(shell cygpath -m $(shell pwd)/$(XWORK_DIR)/$(TARGET).bmm)
else
   BMM_LOCATION	= $(shell pwd)/$(XWORK_DIR)/$(TARGET).bmm
endif

    ############################################################################
    ## Environment
    ############################################################################
    ## Create the appropriate working directories
    ############################################################################

.PRECIOUS:	$(XWORK_DIR_DEP) $(XWORK_DIR_XST_DEP)

$(XWORK_DIR_XST_DEP):
	@$(call print_cmd_info,"MK XST DIR",$(dir $@))
	@$(MKDIR) -p $(dir $@)
	@touch $@

    ############################################################################
    ## Synthesis
    ############################################################################
    ## Synthesize the HDL files into an NGC file.  This rule is triggered if
    ## any of the HDL files are changed or the synthesis options are changed.
    ############################################################################
$(XWORK_DIR)/$(TARGET).lso: $(XWORK_DIR_DEP) $(XST_FLAGS_DEP) $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"MK XST LSO",'$@')
	@echo -n "" > $(XWORK_DIR)/$(TARGET).lso
	@$(foreach lib,$(XST_LSO),echo $(lib) >> $(XWORK_DIR)/$(TARGET).lso;)

$(XWORK_DIR)/$(TARGET).xst: $(XWORK_DIR_DEP) $(XWORK_DIR_XST_DEP) $(XST_FLAGS_DEP) $(XWORK_DIR)/$(TARGET).lso $(XWORK_DIR)/$(TARGET).prj $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"MK XST OPTS",'$@')
	@echo "set -tmpdir $(XWORK_DIR_XST) -xsthdpdir $(XWORK_DIR_XST)" \
		  > $(XWORK_DIR)/$(TARGET).xst
	@echo "run "-lso $(XWORK_DIR)/$(TARGET).lso" -ifn $(XWORK_DIR)/$(TARGET).prj -ofn $(XWORK_DIR)/$(TARGET).ngc -ifmt mixed" \
          "-ofmt NGC -p $(PART)" \
          $(XST_OPTIONS) \
          >> $(XWORK_DIR)/$(TARGET).xst

$(XWORK_DIR)/$(TARGET).prj: $(XWORK_DIR_DEP) $(XST_FLAGS_DEP) $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"MK XST PRJ",'$@')
	@$(call purge, $(XWORK_DIR)/$(TARGET).prj.tmp)
	@$(foreach el,$(SYNTH_PROJ), echo "$(el)" >> $(XWORK_DIR)/$(TARGET).prj.tmp;)
	@awk --field-separator=\, '{print $$1 " " $$2 " \""$$3"\""}' $(XWORK_DIR)/$(TARGET).prj.tmp\
	> $(XWORK_DIR)/$(TARGET).prj

$(XWORK_DIR)/$(TARGET).ngc: $(XWORK_DIR_DEP) $(XST_FLAGS_DEP) $(XWORK_DIR)/$(TARGET).xst $(XWORK_DIR)/$(TARGET).sources $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"SYNTH",$@)
	@$(call purge, $(XWORK_DIR)/$(TARGET).ngc)
	@$(call purge, $(XWORK_DIR)/$(TARGET).syr)
	@touch $(XWORK_DIR)/$(TARGET).ngc
	@( $(XST) $(XST_FLAGS) -ifn $(XWORK_DIR)/$(TARGET).xst -ofn $(XWORK_DIR)/$(TARGET).syr || \
	   ( mv $(XWORK_DIR)/$(TARGET).ngc $(XWORK_DIR)/$(TARGET).ngc_err; false; ) ) \
	   > $(XWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog


    ############################################################################
    ## Build ngd (annotate with constraints)
    ############################################################################
    ## Take the output of the synthesizer and create the NGD file.  This rule
    ## will also be triggered if the constraints file is changed.
    ############################################################################

$(XWORK_DIR)/$(TARGET)_cs.ngc: $(XWORK_DIR_DEP) $(XWORK_DIR)/$(TARGET).ngc $(CHIPSCOPE_CDC) $(XWORK_DIR)/$(TARGET).mkenv $(CHIPSCOPE_CDC)
	@$(call print_cmd_info,"CPS INSERTER","$(TARGET).ngc + $(CHIPSCOPE_CDC) -> $@")
	@( $(CPS_INS) -mode insert \
	-proj $(call abs_path,$(CHIPSCOPE_CDC)) \
	-p $(PART) -dd $(call abs_path,$(XWORK_DIR)/_ngo)\
	-uc $(call abs_path,$(XWORK_DIR)/$(TARGET).ucf) \
	$(if $(BMM_FILE),-bm $(call abs_path,$(BMM_LOCATION)),) -i \
	$(call abs_path,$(XWORK_DIR)/$(TARGET).ngc) \
	$(call abs_path,$(XWORK_DIR)/$(TARGET)_cs.ngc) || \
	( mv $(XWORK_DIR)/$(TARGET)_cs.ngc $(XWORK_DIR)/$(TARGET)_cs.ngc_err; false; )) \
	> $(XWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

# switch flow between chipscope inserter or normal flow
ifeq ($(INSERT_CHIPSCOPE),yes)
#ifeq ($(file_exists $(CHIPSCOPE_CDC)),yes)
ifeq ($(if $(wildcard $(CHIPSCOPE_CDC)),yes,),yes)
NGD_BUILD_INPUT_FILE 	= $(XWORK_DIR)/$(TARGET)_cs.ngc
NGD_BUILD_INFO_STRING	= "NGD BUILD (with cs)"
else
NGD_BUILD_INPUT_FILE 	= $(XWORK_DIR)/$(TARGET).ngc
NGD_BUILD_INFO_STRING	= "NGD BUILD"
endif
else
NGD_BUILD_INPUT_FILE 	= $(XWORK_DIR)/$(TARGET).ngc
NGD_BUILD_INFO_STRING	= "NGD BUILD"
endif


$(XWORK_DIR)/$(TARGET).ngd: $(XWORK_DIR_DEP) $(NGD_FLAGS_DEP) $(NGD_BUILD_INPUT_FILE) $(XWORK_DIR)/$(TARGET).ucf $(BMM_DEP) $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,$(NGD_BUILD_INFO_STRING),$@)
	@$(call purge, $(XWORK_DIR)/$(TARGET).ngd)
	@$(call purge, $(XWORK_DIR)/$(TARGET).bld)
	@$(NGDBUILD) $(NGDBUILD_FLAGS) -p $(PART) \
	$(if $(BMM_FILE),-bm $(BMM_LOCATION),) \
	-uc $(XWORK_DIR)/$(TARGET).ucf -dd $(XWORK_DIR)/_ngo \
	$(NGD_BUILD_INPUT_FILE) \
	$(XWORK_DIR)/$(TARGET).ngd > $(XWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog


################################################################################
#### CPLD toolchain
################################################################################

    ############################################################################
    ## Settings
    ############################################################################
.PRECIOUS: %.vm6 %.jed

    ############################################################################
    ## Variables
    ############################################################################
SIGNATURE        ?= $(TOP_NAME)        # JTAG-accessible signature stored in the CPLD

# append signature if it exists
HPREP6_FLAGS     += -s IEEE1149 $(if $(SIGNATURE),-n $(SIGNATURE),)

    ############################################################################
    ## CPLD fit
    ############################################################################
    ## Fit the NGD file
    ############################################################################

$(XWORK_DIR)/$(TARGET).vm6: $(XWORK_DIR)/$(TARGET).ngd $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"CPLD FIT",$@)
	@cd $(XWORK_DIR) && $(CPLDFIT) $(CPLDFIT_FLAGS) -p $(PART)\
		$(TARGET).ngd > $(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

    ############################################################################
    ## Static timing report
    ############################################################################
    ## Generates an static timing report
    ############################################################################

$(XWORK_DIR)/$(TARGET).tim: $(XWORK_DIR_DEP) $(XWORK_DIR)/$(TARGET).vm6 $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"TIMING RPT",)
	@$(call purge, $(XWORK_DIR)/$(TARGET).tim)
	@cd $(XWORK_DIR) && $(TAENGINE) $(TAE_FLAGS) -f $(TARGET).vm6 \
			    -l $(TARGET).tim > $(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

$(XWORK_DIR)/$(TARGET).cpld_tc: $(XWORK_DIR_DEP) $(XWORK_DIR)/$(TARGET).tim $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info_nonl,"CONSTRAINT CHECK","")
	@$(if $(shell awk -f $(SCRIPT_PATH)/cpld_timing_check.awk $(XWORK_DIR)/$(TARGET).tim\
			| grep "All constraints were met"),\
			echo "all constraints were met",\
			echo "Error! Not all constraints are met!"; false)
	@touch $(XWORK_DIR)/$(TARGET).cpld_tc

    ############################################################################
    ## CPLD JED GEN
    ############################################################################
    ## Generates CPLD programming file
    ############################################################################
$(XWORK_DIR)/$(TARGET).jed: $(XWORK_DIR)/$(TARGET).vm6 $(XWORK_DIR)/$(TARGET).cpld_tc $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"CPLD JED GEN",$@)
	@cd $(XWORK_DIR) && $(HPREP6) $(HPREP6_FLAGS) -i $(TARGET).vm6 \
	> $(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

    ############################################################################
    ## Generate netlist for timing simulation
    ############################################################################
$(XWORK_DIR)/$(TARGET).nga: $(XWORK_DIR)/$(TARGET).vm6 $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"CPLD TSIM",$@)
	@cd $(XWORK_DIR) && $(TSIM) $(TARGET).vm6 $(TARGET).nga \
	> $(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

$(XWORK_DIR)/$(TARGET).sdf: $(XWORK_DIR)/$(TARGET).nga $(XWORK_DIR)/$(TARGET).mkenv $(XWORK_DIR)/$(TARGET).config
	@$(call print_cmd_info,"CPLD NET GEN",$@)
	@cd $(XWORK_DIR) && $(NETGEN) $(NETGEN_FLAGS) $(TARGET).nga \
	> $(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

################################################################################
#### FPGA toolchain
################################################################################

    ############################################################################
    ## Settings
    ############################################################################
.PRECIOUS: %_map.ncd %.ncd %.twr %.pcf

    ############################################################################
    ## Variables
    ############################################################################

# NOTE that internal flags do not retrigger build on change
INTERNAL_MAP_FLAGS 		+= $(INTSTYLE)
INTERNAL_PAR_FLAGS  	+= $(INTSTYLE)
INTERNAL_TRCE_FLAGS     += $(INTSTYLE)
INTERNAL_BITGEN_FLAGS   += $(INTSTYLE)

# the normal flags do

MAP_FLAGS        		+=
PAR_FLAGS        		+=
TRCE_FLAGS       		+=
BITGEN_FLAGS     		+=


    ## Make special settings for the smartguide feature
ifeq ($(ISE_FEATURE_SMARTGUIDE),yes)
SMART_GUIDE_FILE		= $(XWORK_DIR)/$(TARGET)_smgd.ncd
SMART_GUIDE_FILE_EXISTS	= $(call file_exists,$(SMART_GUIDE_FILE))

ifeq ($(ENABLE_SMARTGUIDE),yes)
INTERNAL_MAP_FLAGS 		+= $(if $(SMART_GUIDE_FILE_EXISTS),-smartguide $(SMART_GUIDE_FILE),)
INTERNAL_PAR_FLAGS  	+= $(if $(SMART_GUIDE_FILE_EXISTS),-smartguide $(SMART_GUIDE_FILE),)
endif
endif


MAP_FLAGS_DEP		= $(XWORK_DIR)/$(TARGET)_MAP_FLAGS_DEP.vdep
MAP_FLAGS_DEP_VARS	= PART MAP_FLAGS

PAR_FLAGS_DEP		= $(XWORK_DIR)/$(TARGET)_PAR_FLAGS_DEP.vdep
PAR_FLAGS_DEP_VARS	= PART PAR_FLAGS

TRCE_FLAGS_DEP		= $(XWORK_DIR)/$(TARGET)_TRCE_FLAGS_DEP.vdep
TRCE_FLAGS_DEP_VARS	= PART TRCE_FLAGS

BITGEN_FLAGS_DEP	= $(XWORK_DIR)/$(TARGET)_BITGEN_FLAGS_DEP.vdep
BITGEN_FLAGS_DEP_VARS	= PART BITGEN_FLAGS

PROMGEN_FLAGS_DEP	= $(XWORK_DIR)/$(TARGET)_PROMGEN_FLAGS_DEP.vdep
PROMGEN_FLAGS_DEP_VARS	= PROMGEN_FLAGS

ifeq ($(IGNORE_TIMING_PIN_ERRORS),yes)
   PINCHECK_ERROR_CMD 	= true
else
   PINCHECK_ERROR_CMD 	= false
endif

TIMING_CHK_CMD 	= grep "All constraints were met" $(XWORK_DIR)/$(TARGET).twr > \
				  /dev/null && echo "OK" || echo ""
PIN_CHk_CMD 	= expr `awk --field-separator=\| '{if (($$3 == "IOB") && ($$5 !~\
				  /UNUSED/) && ($$13 != "LOCATED")) {print $$2}}'\
				  $(XWORK_DIR)/$(TARGET).pad | wc -l` == 0 > /dev/null && echo "OK" || echo ""
PIN_CHk_SHOW	= echo "The following pins are not assigned:" ; \
				  awk --field-separator=\| '{if (($$3 == "IOB") && ($$5 !~ /UNUSED/) \
					&& ($$13 != "LOCATED")) {print $$2}}' $(XWORK_DIR)/$(TARGET).pad

    ############################################################################
    ## Rules
    ############################################################################

    ############################################################################
    ## Map
    ############################################################################
    ## Map the NGD file and physical-constraints to the FPGA to create the
    ## mapped NCD file.
    ############################################################################

$(XWORK_DIR)/$(TARGET)_map.ncd $(XWORK_DIR)/$(TARGET).pcf: $(XWORK_DIR_DEP) $(MAP_FLAGS_DEP) $(XWORK_DIR)/$(TARGET).ngd $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"MAP",$@)
	@$(call purge, $(XWORK_DIR)/$(TARGET)_map.ncd)
	@$(call purge, $(XWORK_DIR)/$(TARGET)_map.mrp)
	@$(call purge, $(XWORK_DIR)/$(TARGET).pcf)
	@$(MAP) $(MAP_FLAGS) $(INTERNAL_MAP_FLAGS) -p $(PART) -o $(XWORK_DIR)/$(TARGET)_map.ncd \
		 $(XWORK_DIR)/$(TARGET).ngd $(XWORK_DIR)/$(TARGET).pcf \
		 > $(XWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

    ############################################################################
    ## Place and route (PAR)
    ############################################################################
    ## Place & route the mapped NCD file to create the final NCD file.
    ############################################################################

$(XWORK_DIR)/$(TARGET).pad $(XWORK_DIR)/$(TARGET).ncd: $(XWORK_DIR_DEP) $(PAR_FLAGS_DEP) $(XWORK_DIR)/$(TARGET)_map.ncd $(XWORK_DIR)/$(TARGET).pcf $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"PAR",$@)
	@$(call purge, $(XWORK_DIR)/$(TARGET).ncd)
	@$(call purge, $(XWORK_DIR)/$(TARGET).par)
	@( $(PAR) $(PAR_FLAGS) $(INTERNAL_PAR_FLAGS) $(XWORK_DIR)/$(TARGET)_map.ncd $(XWORK_DIR)/$(TARGET).ncd \
		 $(XWORK_DIR)/$(TARGET).pcf || ( mv $(XWORK_DIR)/$(TARGET).ncd \
		 $(XWORK_DIR)/$(TARGET)_error.ncd; false; )) > $(XWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog
ifeq ($(ISE_FEATURE_SMARTGUIDE),yes)
	@$(if $(SMART_GUIDE_FILE_EXISTS),, cp $(XWORK_DIR)/$(TARGET).ncd $(SMART_GUIDE_FILE))
endif

    ############################################################################
    ## Timing report
    ############################################################################
    ## Create the FPGA timing report after place & route.
    ############################################################################

$(XWORK_DIR)/$(TARGET).twr: $(XWORK_DIR_DEP) $(TRCE_FLAGS_DEP) $(XWORK_DIR)/$(TARGET).ncd $(XWORK_DIR)/$(TARGET).pcf $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"TIMING RPT",$@)
	@$(call purge, $(XWORK_DIR)/$(TARGET).twr)
	@$(TRCE) $(TRCE_FLAGS) $(INTERNAL_TRCE_FLAGS) $(XWORK_DIR)/$(TARGET).ncd -o \
          $(XWORK_DIR)/$(TARGET).twr $(XWORK_DIR)/$(TARGET).pcf \
          > $(XWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

$(XWORK_DIR)/$(TARGET).pincheck: $(XWORK_DIR_DEP) $(XWORK_DIR)/$(TARGET).twr $(XWORK_DIR)/$(TARGET).pad $(XWORK_DIR)/$(TARGET).mkenv
	$(if $(shell $(TIMING_CHK_CMD)),\
		$(if $(shell $(PIN_CHk_CMD)),\
			@$(call print_cmd_info,"CONSTRAINT CHECK","all constraints were met"),\
			@$(call print_cmd_info,"CONSTRAINT CHECK","Error: not all pins assigned!"); $(PIN_CHk_SHOW); $(PINCHECK_ERROR_CMD);),\
		@$(call print_cmd_info,"CONSTRAINT CHECK","Error: timing constrains not met!"); $(PINCHECK_ERROR_CMD))
	@touch $(XWORK_DIR)/$(TARGET).pincheck

    ############################################################################
    ## Bitgen
    ############################################################################
    ## Take the final NCD file and create an FPGA bitstream file.  This rule will
    ## also be triggered if the bit generation options are changed.
    ############################################################################

$(XWORK_DIR)/$(TARGET).bit: $(XWORK_DIR_DEP) $(BITGEN_FLAGS_DEP) $(XWORK_DIR)/$(TARGET).ncd $(XWORK_DIR)/$(TARGET).pincheck $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"BITGEN",$@)
	@$(call purge, $(XWORK_DIR)/$(TARGET).bit)
	@$(call purge, $(XWORK_DIR)/$(TARGET).bgn)
	@$(BITGEN) $(BITGEN_FLAGS) $(INTERNAL_BITGEN_FLAGS) $(XWORK_DIR)/$(TARGET).ncd > \
	$(XWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

    ############################################################################
    ## Promgen
    ############################################################################
    ## Take the final bit file and create an FPGA rom file.  This rule will
    ## also be triggered if the ROM generation options are changed.
    ############################################################################

$(XWORK_DIR)/$(TARGET).mcs: $(XWORK_DIR_DEP) $(PROMGEN_FLAGS_DEP) $(XWORK_DIR)/$(TARGET).bit $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"PROMGEN (mcs - Intel Hex)",$@)
	@$(call purge, $(XWORK_DIR)/$(TARGET).mcs)
	@$(PROMGEN) $(PROMGEN_FLAGS) -o $(XWORK_DIR)/$(TARGET).mcs -p mcs -w -u 0 $(XWORK_DIR)/$(TARGET).bit > \
	$(XWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

$(XWORK_DIR)/$(TARGET).exo: $(XWORK_DIR_DEP) $(PROMGEN_FLAGS_DEP) $(XWORK_DIR)/$(TARGET).bit $(XWORK_DIR)/$(TARGET).mkenv
	@$(call print_cmd_info,"PROMGEN (exo - Motorola)",$@)
	@$(call purge, $(XWORK_DIR)/$(TARGET).exo)
	@$(PROMGEN) $(PROMGEN_FLAGS) -o $(XWORK_DIR)/$(TARGET).exo -p exo -w -u 0 $(XWORK_DIR)/$(TARGET).bit > \
	$(XWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

################################################################################
#### PHONY targets
################################################################################
#### PHONY targets are used to trigger toolchain and execute aditional commands
################################################################################

    ############################################################################
    ## FPGA Editor
    ############################################################################
    ## Starts the FPGA Editor as background process
    ############################################################################
.PHONY: fpga_editor fe

fpga_editor fe: $(XWORK_DIR)/$(TARGET).ncd
	 @$(call print_cmd_info,"FPGA EDITOR (BG)","The FPGA editor will start as background process.")
	@$(FPGA_EDITOR) $(call abs_path,$(XWORK_DIR)/$(TARGET).ncd) $(call abs_path,$(XWORK_DIR)/$(TARGET).pcf) &

    ############################################################################
    ## Timing analyzer
    ############################################################################
    ## Starts the timing analyzer as background proces
    ############################################################################

.PHONY: timingan ta

timingan ta: $(XWORK_DIR)/$(TARGET).twr $(XWORK_DIR)/$(TARGET).pcf $(XWORK_DIR)/$(TARGET).ncd
	@$(call print_cmd_info,"TIMING_ANALYZER (BG)","The timing analyzer GUI will start as background process.")
	@$(TIMINGAN) $(XWORK_DIR)/$(TARGET).ncd $(XWORK_DIR)/$(TARGET).pcf $(XWORK_DIR)/$(TARGET).twr &

    ############################################################################
    ## Tail master log
    ############################################################################
    ## Tracs the output of the master log.
    ############################################################################

.PHONY: tml

tml:
	@$(call print_cmd_info,"TAIL MASTER LOG","Shows the last lines of the master log")
	@tail -F $(XWORK_DIR)/$(OUTPUT_MASTER_LOG)

    ############################################################################
    ## Cat last log
    ############################################################################
    ## Shows the output of the last log (this is the content of the master log.
    ## Can be used in case of an error.
    ############################################################################

.PHONY: ll

ll:
	@$(call print_cmd_info,"CAT LAST LOG","Shows the last log")
	@cat $(XWORK_DIR)/$(OUTPUT_MASTER_LOG)

    ############################################################################
    ## Insert chipscope
    ############################################################################
    ## Use 'cps-ins' to launch the GUI to create a new or modify an existion CDC
    ## file that is configured in the make file.
    ############################################################################

.PHONY: cps-ins

%.cdc: 
	@$(call print_cmd_info,"CPS_CREATE","Creating chipscope project")
	@$(CPS_INS) -create $@ > $(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

cps-ins: $(TARGET).ngc $(CHIPSCOPE_CDC)
	@$(call print_cmd_info,"CPS_INSERT","Starts chipscope inserter GUI (BG)")
	@$(CPS_INS) -edit $(call abs_path,$(CHIPSCOPE_CDC)) \
	-p $(PART) -dd $(call abs_path,$(XWORK_DIR)/_ngo)\
	-uc $(call abs_path,$(XWORK_DIR)/$(TARGET).ucf) \
	$(if $(BMM_FILE),-bm $(call abs_path,$(BMM_LOCATION)),) -i \
	$(call abs_path,$(XWORK_DIR)/$(TARGET).ngc) \
	$(call abs_path,$(XWORK_DIR)/$(TARGET)_cs.ngc) > $(OUTPUT_MASTER_LOG)
	@cp $(XWORK_DIR)/$(OUTPUT_MASTER_LOG) $(XWORK_DIR)/$(@F).outlog

    ############################################################################
    ## Clean
    ############################################################################
    ## Commands to clean the environment
    ############################################################################

.PHONY: xilinx-clean
xilinx-clean::
	 @$(call print_cmd_info,"CLEANUP","xilinx environment")
	-@$(RMDIR) $(XWORK_DIR)
	-@$(RM) xlnx_auto_0.ise
	-@$(RMDIR) xlnx_auto_0_xdb

    ############################################################################
    ## Copy smartguide file
    ############################################################################
    ## Copy current ncd file as guide file
    ############################################################################
xilinx-usg:
	 @$(call print_cmd_info,"SMART GUIDE","Copy smart guide")
ifeq ($(ISE_FEATURE_SMARTGUIDE),yes)
	@cp $(XWORK_DIR)/$(TARGET).ncd $(SMART_GUIDE_FILE)
else
	$(error "The current version of ISE does not support smart guide")
endif

    ############################################################################
    ## Internal debug commands
    ############################################################################
    ##
    ############################################################################

xilinx-test:
	echo $(SMART_GUIDE_FILE_EXISTS)
	echo $(SMART_GUIDE_FILE)
	@echo $(THIS_MAKE_DEP)
	@echo lastword is: $(lastword x y z)
	@echo nr of words: $(words $(MAKEFILE_LIST))
	@echo $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST))
	@echo $(MAKEFILE_LIST)

    ############################################################################
    ## Build information
    ############################################################################
    ## Displays all possible build targets
    ############################################################################


.PHONY: syn ngc ngd map par bit mcs fit tim jed

syn: $(XWORK_DIR)/$(TARGET).ngc
ngc: $(XWORK_DIR)/$(TARGET).ngc
ngd: $(XWORK_DIR)/$(TARGET).ngd

$(TARGET).ngc: $(XWORK_DIR)/$(TARGET).ngc
$(TARGET).ngd: $(XWORK_DIR)/$(TARGET).ngd

## FPGA chain
map: $(XWORK_DIR)/$(TARGET)_map.ncd
par: $(XWORK_DIR)/$(TARGET).ncd
bit: $(XWORK_DIR)/$(TARGET).bit
mcs: $(XWORK_DIR)/$(TARGET).mcs

$(TARGET)_map.ncd: 	$(XWORK_DIR)/$(TARGET)_map.ncd
$(TARGET).ncd: 		$(XWORK_DIR)/$(TARGET).ncd
$(TARGET).bit: 		$(XWORK_DIR)/$(TARGET).bit
$(TARGET).mcs: 		$(XWORK_DIR)/$(TARGET).mcs

## CPLD chain
fit: $(XWORK_DIR)/$(TARGET).vm6
tim: $(XWORK_DIR)/$(TARGET).tim
jed: $(XWORK_DIR)/$(TARGET).jed
nga: $(XWORK_DIR)/$(TARGET).nga
sdf: $(XWORK_DIR)/$(TARGET).sdf

$(TARGET).vm6: $(XWORK_DIR)/$(TARGET).vm6
$(TARGET).tim: $(XWORK_DIR)/$(TARGET).tim
$(TARGET).jed: $(XWORK_DIR)/$(TARGET).jed
$(TARGET).nga: $(XWORK_DIR)/$(TARGET).nga
$(TARGET).sdf: $(XWORK_DIR)/$(TARGET).sdf

.PHONY: xilinx-info
xilinx-info::
	@$(ECHO) "*********************************************************************"
	@$(ECHO) "*** Xilinx Info"
	@$(ECHO) "*********************************************************************"
	@$(ECHO) "*** File targets"
	@$(ECHO) "***   <target>.bit, bit     : generates a bitfile image"
	@$(ECHO) "*** Virtual targets"
	@$(ECHO) "***   xilinx-clean          : cleans all files that are generated"
	@$(ECHO) "***   xilinx-info           : This info"
	@$(ECHO) "***   xilinx-usg            : Update smartguide ncd file with current ncd"
#	@$(ECHO) "***   timing                : (re-)generates timing report"
	@$(ECHO) "***   ta                    : opens timing analyzer report"
	@$(ECHO) "***   fe                    : starts the fpga editor"
	@$(ECHO) "***   tml                   : shows the output of the master log"
	@$(ECHO) "***   ll                    : shows last log, use to show the log of the"
	@$(ECHO) "***                           last command in case of an error"
	@$(ECHO) "***   cps-ins               : start chipscope inserter"
	@$(ECHO) "***"
	@$(ECHO) "***  ** common toolchain **"
	@$(ECHO) "***   syn, ngc              : perform synthesis only"
	@$(ECHO) "***   ngd                   : up to ngd only"
	@$(ECHO) "***"
	@$(ECHO) "***  ** FPGA toolchain **"
	@$(ECHO) "***   map                   : up to map only"
	@$(ECHO) "***   par                   : up to placement and route only"
	@$(ECHO) "***   bit                   : generate bit file"
	@$(ECHO) "***   mcs                   : generate prom file"
	@$(ECHO) "***"
	@$(ECHO) "***  ** CPLD toolchain **"
	@$(ECHO) "***   fit                   : up to fit"
	@$(ECHO) "***   tim                   : up to timing report"
	@$(ECHO) "***   jed                   : generate jed file"
	@$(ECHO) "***   nga                   : generate nga file"
	@$(ECHO) "***   sdf                   : generate sdf file"
	@$(ECHO) "***"
	@$(ECHO) "*********************************************************************"
	@$(ECHO) "*** The current target is   : $(TARGET)"
	@$(ECHO) "*********************************************************************"
