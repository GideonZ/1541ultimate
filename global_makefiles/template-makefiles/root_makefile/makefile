###############################################################################
##
## (C) COPYRIGHT 2008-2011 TECHNOLUTION BV, GOUDA NL
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
## Root makefile for calling sub makefiles in the same directory
###############################################################################
## This makefile calls the other makefiles in the directory.
##
## The other makefiles should have the filename makefile-xxx (for example
## makefile-target1).
##
## The makefile-target1 can be executed by calling
##   make target1
## or if a specific rule is required
##   make target1 rule
###############################################################################

## GLOBAL_INCS should point to the make include files directory. This value
## should only be overridden when it is not detected automatically.
# export GLOBAL_INCS = $(strip $(shell pwd)/<relative path to include files>)

###############################################################################
#### Target specific rules
###############################################################################

.PHONY: all clean

all		: root-all
clean	: root-clean

###############################################################################
#### WARNING!!
###############################################################################
#### Do not touch the settings below (these statements should be placed at the
#### end of the makefile)
###############################################################################

# The following variable contains the actual rule file
RULES_INCLUDE := root_make.inc

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
