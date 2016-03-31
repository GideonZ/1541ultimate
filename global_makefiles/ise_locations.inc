################################################################################
##
## (C) COPYRIGHT 2010 Gideon's Logic Architectures'
##
################################################################################
## XILINX ISE locations include file
##
## rename this file to ise_locations.inc to add it to the make file. Change 
## the directory locations to that what correspond to your system.
################################################################################

export ISE_LOCATIONS_FILE_INCLUDED=yes

export CYGXILINX=/cygdrive/c/Xilinx/13.2/ISE_DS/ISE
export XILINX=C:\\Xilinx\\13.2\\ISE_DS\\ISE
PLATFORM=nt64

#export PATH			= $(CYGXILINX)/bin/$(PLATFORM) $PATH

export LMC_HOME				+= $(CYGXILINX)/smartmodel/$(PLATFORM)/installed_lin
export LD_LIBRARY_PATH		+= $(CYGXILINX)/bin/$(PLATFORM):/usr/X11R6/lib
export NPX_PLUGIN_PATH		+= $(CYGXILINX)/java/${PLATFORM}/jre/plugin/i386/ns4

export XST 	= $(CYGXILINX)/bin/$(PLATFORM)/xst
export NGDBUILD = $(CYGXILINX)/bin/$(PLATFORM)/ngdbuild
export MAP	= $(CYGXILINX)/bin/$(PLATFORM)/map
export PAR	= $(CYGXILINX)/bin/$(PLATFORM)/par
export TRCE	= $(CYGXILINX)/bin/$(PLATFORM)/trce
export BITGEN	= $(CYGXILINX)/bin/$(PLATFORM)/bitgen
export CPLDFIT	= $(CYGXILINX)/bin/$(PLATFORM)/cpldfit
export HPREP6	= $(CYGXILINX)/bin/$(PLATFORM)/hprep6
export PROMGEN	= $(CYGXILINX)/bin/$(PLATFORM)/promgen
