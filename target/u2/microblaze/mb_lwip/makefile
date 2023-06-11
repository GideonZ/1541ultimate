# System tool locations

TOOLS = ../../../tools
BIN2HEX = $(TOOLS)/bin2hex
HEX2BIN = $(TOOLS)/hex2bin
MAKEAPPL = $(TOOLS)/makeappl

include ../../../common/environment.mk

# Outputs
RESULT    = ./result
OUTPUT    = ./output

PRJ      =  liblwip

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

SRCS_CC	 =


SRCS_ASM =  

PATH_INC =  $(addprefix -I, $(VPATH))
COPTIONS = -g -ffunction-sections -Os -mcpu=v5.00.a -std=c99 -DBYTE_ORDER=BIG_ENDIAN
CPPOPT   = -g -ffunction-sections -Os -mcpu=v5.00.a -fno-exceptions -fno-rtti
#LINK 	 = ./mb_app.lds
#LLIB     =  -L ../mb_lib/result_mb \
            -L $(TOOLCHAIN)/microblaze-xilinx-elf/lib \
            -L $(TOOLCHAIN)/lib/gcc/microblaze-xilinx-elf/4.1.2
#LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lxil -lc -lm --end-group -lgcc
LIBS     =  -lc -lgcc
LFLAGS   = --gc-sections 

VPATH   += $(OUTPUT) $(RESULT)

all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).a


#$(RESULT)/$(PRJ).a: $(LINK) $(OBJS_C)
#	@echo "Creating Archive $@"
#	$(AR) -rc $@ $(ALL_OBJS)

include ../../../common/rules.mk
