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

PLATFORM=lin64
ISE_VERSION	?= 13	# when not defined use highest version available

export ISE_LOCATIONS_FILE_INCLUDED=yes

ifeq ($(ISE_VERSION),13)
    export MYXILINX=/cygdrive/c/Xilinx/13.1/ISE_DS/ISE
    export XILINX=C:\\Xilinx\\13.1\\ISE_DS\\ISE
    PLATFORM=nt
endif

ifeq ($(ISE_VERSION),12)
    export MYXILINX=/cygdrive/c/Xilinx/12.1/ISE_DS/ISE
    export XILINX=C:\\Xilinx\\12.1\\ISE_DS\\ISE
    PLATFORM=nt
endif

ifeq ($(ISE_VERSION),11)
    export MYXILINX=/usr/local/xilinx/11.5/ISE
endif

ifeq ($(ISE_VERSION),10w)
    export MYXILINX=/cygdrive/c/Xilinx/10.1/ISE
    export XILINX=C:\\Xilinx\\10.1\\ISE
    PLATFORM=nt
endif

ifeq ($(ISE_VERSION),10)
    export MYXILINX=/usr/local/Xilinx/11.1/ISE
endif

ifeq ($(ISE_VERSION),9)
    export MYXILINX=/usr/local/Xilinx/11.1/ISE
endif

ifeq ($(ISE_VERSION),8)
    export MYXILINX=/usr/local/Xilinx/11.1/ISE
endif

#export PATH			= $(MYXILINX)/bin/$(PLATFORM) $PATH

export LMC_HOME				+= $(MYXILINX)/smartmodel/$(PLATFORM)/installed_lin
export LD_LIBRARY_PATH		+= $(MYXILINX)/bin/$(PLATFORM):/usr/X11R6/lib
export NPX_PLUGIN_PATH		+= $(MYXILINX)/java/${PLATFORM}/jre/plugin/i386/ns4

export XST 	= $(MYXILINX)/bin/$(PLATFORM)/xst
export NGDBUILD = $(MYXILINX)/bin/$(PLATFORM)/ngdbuild
export MAP	= $(MYXILINX)/bin/$(PLATFORM)/map
export PAR	= $(MYXILINX)/bin/$(PLATFORM)/par
export TRCE	= $(MYXILINX)/bin/$(PLATFORM)/trce
export BITGEN	= $(MYXILINX)/bin/$(PLATFORM)/bitgen
export CPLDFIT	= $(MYXILINX)/bin/$(PLATFORM)/cpldfit
export HPREP6	= $(MYXILINX)/bin/$(PLATFORM)/hprep6
export PROMGEN	= $(MYXILINX)/bin/$(PLATFORM)/promgen

