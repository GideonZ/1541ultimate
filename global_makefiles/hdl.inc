################################################################################
##
## (C) COPYRIGHT 2015 TECHNOLUTION BV, GOUDA NL
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
## This is the HDL build common makefile.
## It contains details for the compiler 
################################################################################

RM = rm -rf
MKDIR = mkdir -p
CP = cp -a
EGREP = egrep

# The following lines specify the output files of HDLC.
# Cache file is a special case since it is also an input file
# The protel netlist is also treated separately since it acts as a reference
CACHE_FILE = cache.hdl
# Do not define the netlist filename here, since both Mentor and Altium designs will be used next to each other
# NETLIST_FILE = protel.net

# Not all lines in the cache contain information relevant to the PCB layout. During consistency tests,
# only the relevant information shall be compared to the current design.
# This genertes the cache regular expression filter, based on the "CHECK_PROPS" that contain relevant information.
MINIMUM_PROPS = order_code\
			manufacturer\
			value\
			voltage\
			power\
			tolerance\
			dielectric\
			option

ALL_PROPS = $(MINIMUM_PROPS) $(CHECK_PROPS)

KEYWORD_REGEX = (\{)|(\})
$(foreach keyword, $(ALL_PROPS), $(eval KEYWORD_REGEX := $(KEYWORD_REGEX)|($(keyword) =)))


LIB_CUR_LEV := $(subst /, , $(subst ' ',_, $(CURDIR)))
LIB_LEVELS := $(wordlist 3, $(words $(LIB_CUR_LEV)),$(LIB_CUR_LEV))
$(foreach level, $(LIB_LEVELS), $(eval LIB_LE := $(LIB_LE)../) $(eval HDL_LIB_SEARCH_PATH := $(HDL_LIB_SEARCH_PATH) $(LIB_LE)hdl_lib))
HDL_LIB_REL := $(firstword $(wildcard $(HDL_LIB_SEARCH_PATH)))
GLOBAL_LIB_DIR ?= $(CURDIR)/$(HDL_LIB_REL)

LIBRARY     = $(GLOBAL_LIB_DIR)/library.hdl
