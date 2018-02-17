################################################################################
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
################################################################################
## Root makefile for calling sub makefiles in the same directory
################################################################################
## This makefile calls the variant makefiles
################################################################################

SUBMAKEFILES			?= $(wildcard makefile-*)
SUBMAKES				?= $(patsubst makefile-%,%,$(SUBMAKEFILES))

################################################################################
## includes
################################################################################
include $(GLOBAL_INCS)/shell.inc

################################################################################
## sub makefile rule
################################################################################
## The following rule makes it possible to execute the submakefiles by
## separating the makefilename and command with a '-'. For example aspi-ll
## Note that it is allowed to have dashes in the command as well for example
## make aspi-vsim-targets executes the makefile-aspi with target "vsim-targets"
################################################################################

makefile-name 	= $(shell echo $(1) | sed s/-/' '/ | awk '{print "makefile-"$$1}')
makefile-target	= $(shell echo $(1) | sed s/-/' '/ | awk '{print $$2}')

# Add a target for each submake. The submake can be called with and without target
# The target is separated with a '-'. The rules are converted to a %.submake rule.
# Creating the separate rules is to assure make executes all the rules and does
# not ommit execution of multiple makefiles, because it assumes the target/rule
# is equal.

$(foreach submake,$(SUBMAKES),$(eval $(submake)-%: $(submake)-%.submake;))
$(foreach submake,$(SUBMAKES),$(eval $(submake): $(submake).submake;))

%.submake: FORCE
	@$(call print_cmd_info,"MAKE","$(call makefile-name, $(*F)) $(call makefile-target, $(*F))")
	@$(MAKE) --no-print-directory -f $(call makefile-name, $(*F)) $(call makefile-target, $(*F))

FORCE:

################################################################################
## default rules
################################################################################

.PHONY: root-all root-clean root-info

root-all	: $(SUBMAKES)
root-clean	: $(patsubst %,%-clean,$(SUBMAKES))
root-test	:

	echo $(SUBMAKEFILES)
	echo $(SUBMAKES)

root-info	:
	@echo "*********************************************************************"
	@echo "*** Xilinx Info"
	@echo "*********************************************************************"
	@echo "*** Virtual targets"
	@echo "***   root-all    : executes default rule for all submakefiles"
	@echo "***   root-clean  : executes clean for all submakefiles"
	@echo "*** Available makefiles        :"
	@$(foreach submake,$(SUBMAKES),echo "***   $(submake)";)
	@echo "*********************************************************************"
