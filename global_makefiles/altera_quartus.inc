################################################################################
##
## (C) COPYRIGHT 2006-2013 TECHNOLUTION BV, GOUDA NL
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
## This is the Altera Quartus build make file. It is based on the makefile
## provided by Altera.
################################################################################


################################################################################
## Settings
################################################################################

.SUFFIXES:		# delete all known suffixes

################################################################################
## Global variables
################################################################################

WORK_DIR		?= work
QWORK_DIR		?= $(WORK_DIR)
QWORK_DIR_DEP		?= $(QWORK_DIR)/_dir
TL_ENV ?= true

VPATH			+= $(QWORK_DIR)

ASSIGNMENT_FILES 	:= $(PROJECT_FILE) $(PROJECT_SETTINGS_FILE)

SOURCE_FILES 		= $(shell grep "VHDL_FILE" $(PROJECT_SETTINGS_FILE) | \
				fromdos | awk '{print $$4}' | tr -d \" | tr "\n" " ")

.NOTPARALLEL:


################################################################################
## Includes
################################################################################

include $(GLOBAL_INCS)/shell.inc
THIS_MAKE_DEP := $(THIS_MAKE_DEP) $(lastword $(MAKEFILE_LIST))
include $(GLOBAL_INCS)/altera_build_user_image.inc


################################################################################
## Toolchain version
################################################################################
## Gets the location of all required Quartus tools from a Quartus location file.
## The default path is used for all applications that are not defined in the
## location file(s).
################################################################################

THIS_MAKE_DEP := $(THIS_MAKE_DEP) $(lastword $(MAKEFILE_LIST))

# If no location of the tools are defined use the current active Quartus version
# in the shell.
QUARTUS_MAP	= ${TL_ENV} $(ENVIRONMENTS) && quartus_map
QUARTUS_FIT	= ${TL_ENV} $(ENVIRONMENTS) && quartus_fit
QUARTUS_ASM	= ${TL_ENV} $(ENVIRONMENTS) && quartus_asm
QUARTUS_TAN	= ${TL_ENV} $(ENVIRONMENTS) && quartus_tan
QUARTUS_SH	= ${TL_ENV} $(ENVIRONMENTS) && quartus_sh
QUARTUS_CDB	= ${TL_ENV} $(ENVIRONMENTS) && quartus_cdb
QUARTUS_GUI = ${TL_ENV} $(ENVIRONMENTS) && quartus
QUARTUS_CPF = ${TL_ENV} $(ENVIRONMENTS) && quartus_cpf

################################################################################
#### Make tool flow
################################################################################


    ############################################################################
    ## Settings
    ############################################################################
.PRECIOUS: %.rpt %.pincheck


    ############################################################################
    ## Variables
    ############################################################################

MAP_ARGS 	+= --write_settings_files=off
FIT_ARGS 	+= --write_settings_files=off
ASM_ARGS 	+= --write_settings_files=off
TAN_ARGS 	+=
STA_ARGS 	+=
CDB_ARGS	+=

ifeq ($(IGNORE_TIMING_PIN_ERRORS),yes)
   PINCHECK_ERROR_CMD 	= true
else
   PINCHECK_ERROR_CMD 	= false
endif

TIMING_CHK_CMD 	= grep "Info: All timing requirements were met." $(*F).tan.rpt > \
				  /dev/null && echo "OK" || echo ""

PIN_CHK_CMD 	= echo "not implemented"
PIN_CHK_SHOW	= echo "not implemented"

OUTPUT_MASTER_LOG	= $(*F).master.outlog

# Used files (for file check):
USED_FILES := $(SOURCE_FILES)


    ############################################################################
    ## Environment
    ############################################################################
    ## Create the appropriate working directories
    ############################################################################

$(QWORK_DIR_DEP):
	@$(call print_cmd_info,"MK QWORK DIR",$(dir $@))
	@$(MKDIR) -p $(dir $@)
	@touch $@


    ############################################################################
    ## Project initialization
    ############################################################################

$(ASSIGNMENT_FILES):
	$(QUARTUS_SH) --prepare $(PROJECT)


    ############################################################################
    ## Project settings dependencies
    ############################################################################
    ## Generate dependencies of the different tools on the settings file
    ##
    ## The 'determine_smart_action' script will generate or update the a
    ## <executable>.chg file for every tool when the related settings in the
    ## project settings file have changed.
    ############################################################################

.PRECIOUS: %.chg %.smlog

%.chg:
	@$(call print_cmd_info,"GEN CHG",$@)
	@touch $@

%.smlog: $(ASSIGNMENT_FILES)
	@$(call print_cmd_info,"SMART UPDATE","Project settings of '$(*F)' have changed")
	@$(QUARTUS_SH) --determine_smart_action $(*F) > $@


    ############################################################################
    ## Synthesis & Mapping
    ############################################################################
    ## Synthesize the HDL files.
    ############################################################################

%.map.rpt: map.chg %.smlog $(SOURCE_FILES) $(QWORK_DIR_DEP)
	@$(call print_cmd_info,"MAP",$(*F))
	@$(QUARTUS_MAP) $(MAP_ARGS) $(*F) > $(QWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(QWORK_DIR)/$(OUTPUT_MASTER_LOG) $(QWORK_DIR)/$(@F).outlog


    ############################################################################
    ## Fit the design
    ############################################################################
    ## Place and route the design
    ############################################################################

%.fit.rpt: fit.chg %.map.rpt %.smlog $(QWORK_DIR_DEP)
	@$(call print_cmd_info,"FIT",$(*F))
	@$(QUARTUS_FIT) $(FIT_ARGS) $(*F) > $(QWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(QWORK_DIR)/$(OUTPUT_MASTER_LOG) $(QWORK_DIR)/$(@F).outlog


    ############################################################################
    ## Timing analyzer
    ############################################################################
    ## Analyze timing
    ############################################################################

ifeq ($(TIMEQUEST),yes)
  # Create and include dependencies for TimeQuest.
  $(QWORK_DIR)/$(PROJECT).sta.depend.make : $(QWORK_DIR_DEP) $(PROJECT).qsf
	@$(call print_cmd_info,"DEPENDS",$@)
	@$(GLOBAL_INCS)/scripts/altera_gen_sta_deps.sh $(PROJECT) > $@

  -include $(QWORK_DIR)/$(PROJECT).sta.depend.make

  # TimeQuest Timing Analyzer:
  %.sta.rpt: sta.chg %.fit.rpt %.smlog $(QWORK_DIR_DEP)
	@$(call print_cmd_info,"STA",$(*F))
	@$(QUARTUS_STA) $(STA_ARGS) $(*F) > $(QWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(QWORK_DIR)/$(OUTPUT_MASTER_LOG) $(QWORK_DIR)/$(@F).outlog

  # Check timing in TimeQuest Timing Analyzer report file
  %.timing: %.sta.rpt
	@$(call print_cmd_info,"CHECK TIMING",$(*F))
	@if grep -n "not met" $< ; then false; else touch $@ ; true; fi

else

  # Classic Timing Analyzer:
  %.tan.rpt: tan.chg %.fit.rpt %.smlog $(QWORK_DIR_DEP)
	@$(call print_cmd_info,"TAN",$(*F))
	@$(QUARTUS_TAN) $(TAN_ARGS) $(*F) > $(QWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(QWORK_DIR)/$(OUTPUT_MASTER_LOG) $(QWORK_DIR)/$(@F).outlog

  # Check timing in Classic Timing Analyzer report file
  %.timing: %.tan.rpt
	$(if $(shell $(TIMING_CHK_CMD)),\
		$(if $(shell $(PIN_CHK_CMD)),\
			@$(call print_cmd_info,"CONSTRAINT CHECK","all constraints were met"),\
			@$(call print_cmd_info,"CONSTRAINT CHECK","Error: not all pins assigned!"); $(PIN_CHK_SHOW); $(PINCHECK_ERROR_CMD);),\
		@$(call print_cmd_info,"CONSTRAINT CHECK","Error: timing constrains not met!"); $(PINCHECK_ERROR_CMD))
	@touch $@
endif

%.pincheck: %.fit.rpt
	@$(call print_cmd_info,"CHECK PINNING",$(*F))
	@awk '   										\
		BEGIN { FS=";" ; CHECK=0; }							\
		/^+/ || !/^;/ { next }								\
		$$2 ~ /Name/ && $$3 ~ /Pin #/ {							\
			CHECK=0;								\
			for(i=3; i<NF; i++) {							\
				if($$i ~ /Location assigned by/) {				\
					USER=i;							\
					CHECK=1;						\
					CHECK_NF=NF;						\
					break;							\
				}								\
			}									\
			next;									\
		}										\
		!CHECK { next }									\
		NF!=CHECK_NF { CHECK=0; next; }							\
		$$USER ~ /User/ { next }							\
		{ printf("Pin %s was not user assigned\n",$$3) >"/dev/stderr"; }	\
	' $< >$@ || (rm $@; $(PINCHECK_ERROR_CMD))

    ############################################################################
    ## Generate programming image
    ############################################################################
    ## Image generator
    ############################################################################

%.asm.rpt: asm.chg %.fit.rpt %.pincheck %.timing %.smlog $(QWORK_DIR_DEP)
	@$(call print_cmd_info,"ASM",$(*F))
	@$(QUARTUS_ASM) $(ASM_ARGS) $(*F) > $(QWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(QWORK_DIR)/$(OUTPUT_MASTER_LOG) $(QWORK_DIR)/$(@F).outlog


################################################################################
#### flow script
################################################################################

.PHONY: %.qflow

%.qflow: $(QWORK_DIR_DEP)
	@$(call print_cmd_info,"QSH FLOW - COMPILE",$(*F))
	@$(QUARTUS_SH) --flow compile $(*F) > $(QWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(QWORK_DIR)/$(OUTPUT_MASTER_LOG) $(QWORK_DIR)/$(@F).outlog
	@! grep "Critical Warning" $(QWORK_DIR)/$(PROJECT).*.rpt
	@$(call print_cmd_info,"QSH FLOW - COMPLETE",$(*F))

################################################################################
#### PHONY targets
################################################################################
#### PHONY targets are used to trigger toolchain and execute aditional commands
################################################################################

    ############################################################################
    ## test
    ############################################################################
    ## debugging only
    ############################################################################

altera-test:
#	@echo $(SOURCE_FILES)
	@echo $(PATH)
	@echo ""
	@$(QUARTUS_FIT) --version


    ############################################################################
    ## Clean
    ############################################################################
    ## Commands to clean the environment
    ############################################################################

.PHONY: altera-clean
altera-clean:
	 @$(call print_cmd_info,"CLEANUP","altera environment")
	-@$(RM) cmp_state.ini \
			*.chg \
			*.db_info \
			*.done \
			*.eqn \
			*.htm \
			*.pin \
			*.pincheck \
			*.pof \
			*.qws \
			*.rbf \
			*.rpt \
			*.smlog \
			*.smsg \
			*.sof \
			*.summary \
			*.timing \
			*.ttf \
			PLLJ_PLLSPE_INFO.txt
	-@$(RMDIR) db \
			incremental_db \
			work


    ############################################################################
    ## Clean
    ############################################################################
    ## Commands to start the gui
    ############################################################################
altera-gui: $(PROJECT_FILE)
	@$(call print_cmd_info,"GUI","Quartus II")
	@$(QUARTUS_GUI) $(PROJECT_FILE) &

    ############################################################################
    ## Tail master log
    ############################################################################
    ## Tracs the output of the master log.
    ############################################################################

.PHONY: %.tml

%.tml:
	@$(call print_cmd_info,"TAIL MASTER LOG","Shows the last lines of the master log")
	@tail -F $(QWORK_DIR)/$(OUTPUT_MASTER_LOG)


    ############################################################################
    ## Cat last log
    ############################################################################
    ## Shows the output of the last log (this is the content of the master log.
    ## Can be used in case of an error.
    ############################################################################

.PHONY: %.ll

%.ll:
	@$(call print_cmd_info,"CAT LAST LOG","Shows the last log")
	@cat $(QWORK_DIR)/$(OUTPUT_MASTER_LOG)


    ############################################################################
    ## Target
    ############################################################################
    ## target names
    ############################################################################

.PHONY: map fit asm timing tml ll

ifeq ($(FLOW),make)
  map			: $(PROJECT).map.rpt
  fit			: $(PROJECT).fit.rpt
  asm			: $(PROJECT).asm.rpt
  tan			: $(PROJECT).tan.rpt
  sta			: $(PROJECT).sta.rpt
  $(PROJECT).sof	: $(PROJECT).asm.rpt
  ifeq ($(TIMEQUEST),yes)
    $(PROJECT).timing	: $(PROJECT).sta.rpt
  else
    $(PROJECT).timing	: $(PROJECT).tan.rpt
  endif
else
  map			: $(PROJECT).qflow
  fit			: $(PROJECT).qflow
  asm			: $(PROJECT).qflow
  tan			: $(PROJECT).qflow
  $(PROJECT).sof	: $(PROJECT).qflow
  $(PROJECT).timing	: $(PROJECT).qflow
endif

tml		: $(PROJECT).tml
ll		: $(PROJECT).ll


    ############################################################################
    ## Build information
    ############################################################################
    ## Displays all possible build targets
    ############################################################################

.PHONY: altera-info

altera-info:
	@$(ECHO) "*********************************************************"
	@$(ECHO) "*** ALTERA TARGETS"
	@$(ECHO) "*********************************************************"
	@$(ECHO) "*** File targets"
	@$(ECHO) "***   map                   : synthesis & target mapping"
	@$(ECHO) "***   fit                   : place and route"
	@$(ECHO) "***   asm                   : generates a target image"
	@$(ECHO) "***   <target>.sof          : generates a sof image"
	@$(ECHO) "*** Virtual targets"
	@$(ECHO) "***   <target>.timing       : (re-)generates timing report"
	@$(ECHO) "***   	"
	@$(ECHO) "***   altera-clean          : cleans all files that are generated by the altera makefile"
	@$(ECHO) "***   altera-info           : this info"
	@$(ECHO) "***   <target>.tml          : shows the output of the master log"
	@$(ECHO) "***   <target>.ll           : shows last log, use to show the log of the"
	@$(ECHO) "***   			            last command incase of an error"
	@$(ECHO) "*********************************************************"
