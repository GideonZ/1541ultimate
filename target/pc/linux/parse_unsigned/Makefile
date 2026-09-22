# The parser suite of ../parse, built with -funsigned-char, as the RISC-V targets
# (Ultimate 64 II, Ultimate II+L) compile it. The host and Nios II builds have a signed
# char, so a defect that depends on the signedness of char, like the shifted space rule
# of SI-147, can only fail here.
include ../parse/Makefile

OPTIONS := $(filter-out -fsigned-char,$(OPTIONS)) -funsigned-char
