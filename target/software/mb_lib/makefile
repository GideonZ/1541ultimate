# System tool locations

HERE = $(shell pwd)

# Configuration
SHELL     = bash
CROSS     = mb

# Outputs
RESULT    = ./result_mb
OUTPUT    = ./output_mb
OUTPUT_FP = $(shell pwd)/output_mb

PRJ      =  libc_u2
PATH_SW  =  ../../../software

VPATH     = $(PATH_SW)/libc/stdlib \
			$(PATH_SW)/libc/errno \
			$(PATH_SW)/libc/include \
			$(PATH_SW)/libc/reent 
#			$(PATH_SW)/libc/machine/spu/sys 

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))

SRCS_C   =	errno.c \
			reent.c \
            malloc.c \
			mallocr.c

CC		  = $(CROSS)-gcc
AR		  = $(CROSS)-ar
OBJDUMP   = $(CROSS)-objdump
OBJCOPY	  = $(CROSS)-objcopy
SIZE	  = $(CROSS)-size

TOOLCHAIN = C:/Xilinx/13.2/ISE_DS/EDK/gnu/microblaze/nt64

PATH_INC =  $(addprefix -I, $(VPATH))
#COPTIONS = -ffunction-sections -Os -std=c99 -DHAVE_MMAP=0 -DLACKS_UNISTD_H 
COPTIONS = -g -ffunction-sections -O0 -std=c99 -DINTERNAL_NEWLIB -DINTERNAL_NEWLIB -DDEFINE_MALLOC -DDEFINE_FREE -DDEFINE_CALLOC
VPATH   += $(OUTPUT) $(RESULT)

.SUFFIXES:

OBJS_C   = $(notdir $(SRCS_C:%.c=%.o))

ALL_OBJS      = $(addprefix $(OUTPUT)/,$(OBJS_C))

.PHONY: clean all dep

all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).a

$(OUTPUT):
	@mkdir $(OUTPUT)

$(RESULT):
	@mkdir $(RESULT)
		
$(RESULT)/$(PRJ).a: $(OBJS_C)
	@echo "Creating Archive $(@F)"
	$(AR) -rc $(RESULT)/$(PRJ).a $(ALL_OBJS)

%.o: %.c
	@echo "Compiling $<"
	@$(CC) $(COPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.d: %.c
	@$(CC) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)


# pull in dependency info for *existing* .o files
-include $(ALL_OBJS:.o=.d)

clean:
	@-rm -rf $(OUTPUT)
	@-rm -rf $(RESULT)

dep:  $(OBJS_C:.o=.d)
