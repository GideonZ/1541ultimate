################################################################################
##
## (C) COPYRIGHT 2010 Gideon's Logic Architectures'
##
################################################################################
## VSIM Simulation include file
################################################################################


export SCRIPT_PATH = $(strip $(GLOBAL_INCS)/scripts)

################################################################################
## Tool settings
################################################################################

# Set the shell to execute this with

SHELL = /bin/bash

# Standard OS utilities.  These are for Linux.  Set them for your particular OS.
RM                 := rm -f
RMDIR              := rm -rf
MKDIR              := mkdir
ECHO               := echo

IS_CYGWIN		   := $(shell uname | grep "CYGWIN" > /dev/null && echo yes)

MAKE_LEVEL_SHIFT	= $(wordlist 1,$(MAKELEVEL),\x20\x20 \x20\x20 \x20\x20 \x20\x20 \x20\x20 \x20\x20 \x20\x20 \x20\x20)
print_cmd_info		= printf "[%s] - $(MAKE_LEVEL_SHIFT)%-20s : %s\n" `date +%H:%M:%S` $(1) $(2)
print_cmd_info_nonl	= printf "[%s] - $(MAKE_LEVEL_SHIFT)%-20s : %s" `date +%H:%M:%S` $(1) $(2)
file_exists			= $(if $(wildcard $(1)),yes,)
file_exist_in_paths	= $(firstword $(foreach path,$(1),$(call file_exists,$(path)/$(2))))

ifeq ($(IS_CYGWIN),yes)
	abs_path			= $(shell cygpath -m $(CURDIR)/$(1))
else
	abs_path			= $(abspath $(CURDIR)/$(1))
endif

purge			= rm -f $(1) ; [[ -e $(1) ]] && { echo "Error: file $(1) could not be removed"; false ; } || true;
