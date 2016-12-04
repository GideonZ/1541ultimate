# System tool locations

HERE = $(shell pwd)
TOOLS = ../../../tools
BIN2HEX = $(TOOLS)/bin2hex
HEX2BIN = $(TOOLS)/hex2bin
MAKEAPPL = $(TOOLS)/makeappl

# Configuration
SHELL     = bash
CROSS     = mb

# Outputs
RESULT    = $(shell pwd)/result
OUTPUT    = ./output
OUTPUT_FP = $(shell pwd)/output

PRJ      =  liblwip
PATH_SW  =  ../../../software

VPATH     = $(PATH_SW)/lwip-1.4.1/src/ \
			$(PATH_SW)/lwip-1.4.1/src/core/ \
			$(PATH_SW)/lwip-1.4.1/src/core/ipv4 \
			$(PATH_SW)/lwip-1.4.1/src/core/snmp \
			$(PATH_SW)/lwip-1.4.1/src/api \
			$(PATH_SW)/lwip-1.4.1/src/netif \
			$(PATH_SW)/lwip-1.4.1/src/include \
			$(PATH_SW)/lwip-1.4.1/src/include/ipv4 \
			$(PATH_SW)/FreeRTOS/Source \
			$(PATH_SW)/FreeRTOS/Source/include \
			$(PATH_SW)/FreeRTOS/Source/portable/microblaze \
			$(PATH_SW)/FreeRTOS/Source/MemMang \
			$(PATH_SW)/network \
			$(PATH_SW)/network/config \
			$(PATH_SW)/system

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))


SRCS_C   =	sys_arch.c \
			def.c \
			dhcp.c \
			dns.c \
			init.c \
			mem.c \
			memp.c \
			netif.c \
			pbuf.c \
			raw.c \
			stats.c \
			sys.c \
			tcp.c \
			tcp_in.c \
			tcp_out.c \
			timers.c \
			udp.c \
			autoip.c \
			icmp.c \
			igmp.c \
			inet.c \
			inet_chksum.c \
			ip.c \
			ip_addr.c \
			ip_frag.c \
			asn1_dec.c \
			asn1_enc.c \
			mib2.c \
			mib_structs.c \
			msg_in.c \
			msg_out.c \
			api_lib.c \
			api_msg.c \
			err.c \
			netbuf.c \
			netdb.c \
			netifapi.c \
			sockets.c \
			tcpip.c \
			etharp.c \
			slipif.c 

# the order of the files is important, because of the static constructors.
# main loop defines two important globals that define the event based polling mechanism:
# poll_list and the event queue. The sources following the main loop are the ones
# that register themselves in the main loop. The file manager is the second important
# root of the application. Devices can register themselves in the file system tree.
# Therefore, the file manager needs to exist.

SRCS_CC	 =
####  			$(PRJ).cc


SRCS_ASM =  

PATH_INC =  $(addprefix -I, $(VPATH))
COPTIONS = -g -ffunction-sections -O2 -std=c99 
CPPOPT   = -g -ffunction-sections -O2 -fno-exceptions -fno-rtti
LINK 	 = ./mb_app.lds
LLIB     =  -L ../mb_lib/result_mb \
            -L $(TOOLCHAIN)/microblaze-xilinx-elf/lib \
            -L $(TOOLCHAIN)/lib/gcc/microblaze-xilinx-elf/4.1.2
#LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lxil -lc -lm --end-group -lgcc
LIBS     =  -lc -lgcc
LFLAGS   = --gc-sections 

VPATH   += $(OUTPUT) $(RESULT)

CC		  = $(CROSS)-gcc
CPP		  = $(CROSS)-g++
LD		  = $(CROSS)-ld
OBJDUMP   = $(CROSS)-objdump
OBJCOPY	  = $(CROSS)-objcopy

.SUFFIXES:

OBJS_ASM = $(notdir $(SRCS_ASM:%.s=%.o))
OBJS_C   = $(notdir $(SRCS_C:%.c=%.o))
OBJS_CC  = $(notdir $(SRCS_CC:%.cc=%.o))

ALL_OBJS      = $(addprefix $(OUTPUT)/,$(OBJS_ASM) $(OBJS_C) $(OBJS_CC))
ALL_DEP_OBJS  = $(addprefix $(OUTPUT)/,$(OBJS_C) $(OBJS_CC))


.PHONY: hex clean all dep test

all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).a

test: sys_arch.p

$(OUTPUT):
	@mkdir $(OUTPUT)

$(RESULT):
	@mkdir $(RESULT)
		
$(RESULT)/$(PRJ).bin: $(OUTPUT)/$(PRJ).out
	@echo "Creating Binary $@"
	@$(OBJCOPY) -O binary $< $@

$(RESULT)/$(PRJ).a: $(LINK) $(OBJS_C)
	@echo "Creating Archive $@"
	$(AR) -rc $@ $(ALL_OBJS)

%.o: %.s
	@echo "Compiling $<"
	@$(CC) $(OPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.c
	@echo "Compiling $<"
	@$(CC) $(COPTIONS) $(PATH_INC) -E -o $(OUTPUT)/$(@F:.o=.pre) $<
	@$(CC) $(COPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.cc
	@echo "Compiling $<"
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -o $(OUTPUT)/$(@F) $<
	@$(CPP) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.pre: %.c
	@echo "Running preprocessor on $<"
	@$(CC) $(COPTIONS) $(PATH_INC) -B. -E -o $(OUTPUT)/$(@F) $<

%.d: %.cc
	@$(CPP) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.d: %.c
	@$(CC) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

$(OUTPUT)/$(PRJ).out: $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM)
	@echo "Linking..."
	@$(LD) $(LLIB) $(LFLAGS) -T $(LINK) -Map=$(OUTPUT)/$(PRJ).map -o $(OUTPUT)/$(PRJ).out $(ALL_OBJS) $(LIBS)


# pull in dependency info for *existing* .o files
-include $(ALL_DEP_OBJS:.o=.d)

clean:
	@-rm -rf $(OUTPUT)
	@-rm -rf $(RESULT)

dep:  $(OBJS_CC:.o=.d) $(OBJS_C:.o=.d)
