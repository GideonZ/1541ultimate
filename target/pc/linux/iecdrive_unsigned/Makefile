# The drive suite of ../iecdrive, built with -funsigned-char, as the RISC-V targets
# (Ultimate 64 II, Ultimate II+L) compile it; see ../parse_unsigned. Run it from this
# directory, because it creates its FAT image file in the working directory.
include ../iecdrive/Makefile

OPTIONS := $(filter-out -fsigned-char,$(OPTIONS)) -funsigned-char
