###############################################################################
##
## (C) COPYRIGHT 2006-2011 TECHNOLUTION BV, GOUDA NL
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
## This is the Altera Quartus project configuration file. All project settings
## are found in the .qpf and .qsf files.
###############################################################################
## NOTE: The following restrictions apply when using this make file:
##		- Pin checking is not supported;
##		- Timing check only supported for FLOW type 'make'.
###############################################################################

## GLOBAL_INCS should point to the make include files directory. This value
## should only be overriden when it is not detected automatically.
# export GLOBAL_INCS = $(strip $(shell pwd)/<relative path to include files>)

.SUFFIXES:		#delete all known suffixes

###############################################################################
## Project Configuration:
###############################################################################
## Specify the name of the design (project), the Quartus II Settings
## File (.qsf), and the list of source files used.
###############################################################################
ENVIRONMENTS = quartus/10.0

TARGET	 				:=

QUARTUS_VERSION			:=

PROJECT					:= $(TARGET)
PROJECT_FILE			:= $(PROJECT).qpf
PROJECT_SETTINGS_FILE	:= $(PROJECT).qsf

###############################################################################
## Flow type
###############################################################################
## set flow type (this can be make
## make	: use make to handle process flow
## flow	: use quartus flow command to handle process flow
###############################################################################

FLOW					:= make

###############################################################################
## Special flow options
###############################################################################
##
###############################################################################

## IGNORE_TIMING_PIN_ERRORS
## When this variable is set to set yes a bit file is generated even when the
## timing constraints are not met or not all pins in the design are assigned.
##
## NOTE: pin checking is not supported! Timing check only supported when
## 		 FLOW is set to 'make'

IGNORE_TIMING_PIN_ERRORS = no

## TIMEQUEST
## Set to "yes" to use the TimeQuest Timing Analyzer.
## Set to "no" (or any other value) to use the Classic Timing Analyzer.

TIMEQUEST				:= yes

###############################################################################
## Set command arguments (advanced)
###############################################################################

MAP_ARGS 	+=
FIT_ARGS 	+=
ASM_ARGS 	+=
TAN_ARGS 	+=

###############################################################################
## Default rules
###############################################################################

all:	$(PROJECT).sof

clean:	altera-clean

###############################################################################
#### WARNING!!
###############################################################################
#### Do not touch the settings below (these statements should be placed at the
#### end of the makefile)
###############################################################################

# The following variable contains the actual rule file
RULES_INCLUDE := altera_quartus.inc

###############################################################################
#### Automatically find make_include directory
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
$(error $(RULES_INCLUDE) include file not found. Please enter path to the make_includes directory manually)
endif

include $(GLOBAL_INCS)/$(RULES_INCLUDE)
