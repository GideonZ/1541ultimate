
CROSS	     = riscv32-unknown-elf-
ELFTYPE      = elf32-littleriscv
ARCHITECTURE = riscv
# CPU architecture and ABI
MARCH ?= rv32i
MABI  ?= ilp32

include ../../../common/environment.mk

# Outputs
RESULT    = $(shell pwd)/result
OUTPUT    = ./output
OUTPUT_FP = $(shell pwd)/output

PRJ      =  liblwip
#PATH_SW  =  ../../../software

VPATH     = $(PATH_SW)/lwip/src/ \
			$(PATH_SW)/lwip/src/core/ \
			$(PATH_SW)/lwip/src/core/ipv4 \
			$(PATH_SW)/lwip/src/core/snmp \
			$(PATH_SW)/lwip/src/api \
			$(PATH_SW)/lwip/src/netif \
			$(PATH_SW)/lwip/src/include \
			$(PATH_SW)/lwip/src/include/ipv4 \
			$(PATH_SW)/lwip/src/apps/sntp \
			$(PATH_SW)/lwip/src/apps/mdns \
			$(PATH_SW)/lwip/src/include/apps \
			$(PATH_SW)/FreeRTOS/Source \
			$(PATH_SW)/FreeRTOS/Source/include \
			$(PATH_SW)/FreeRTOS/Source/MemMang \
			$(PATH_SW)/FreeRTOS/Source/portable/risc-v \
			$(PATH_SW)/network \
			$(PATH_SW)/network/config \
			$(PATH_SW)/system

INCLUDES =  $(wildcard $(addsuffix /*.h, $(VPATH)))


SRCS_C   =	sys_arch.c \
			def.c \
			dns.c \
			inet_chksum.c \
			init.c \
			mem.c \
			memp.c \
			netif.c \
			pbuf.c \
			raw.c \
			ip.c \
			stats.c \
			sys.c \
			tcp.c \
			tcp_in.c \
			tcp_out.c \
			timeouts.c \
			udp.c \
			autoip.c \
			etharp.c \
			dhcp.c \
			icmp.c \
			igmp.c \
			acd.c \
			ip4_addr.c \
			ip4_frag.c \
			ip4.c \
			api_lib.c \
			api_msg.c \
			err.c \
			if_api.c \
			netbuf.c \
			netdb.c \
			netifapi.c \
			sockets.c \
			tcpip.c \
			ethernet.c \
			slipif.c \
			zepif.c \
			sntp.c \
			mdns.c \
			mdns_out.c \
			mdns_domain.c

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
OPTIONS  = -march=$(MARCH) -mabi=$(MABI) -gdwarf-2 -Os -ffunction-sections -DIOBASE=0x10000000 -DU2P_IO_BASE=0x10100000
COPTIONS = $(OPTIONS) -std=c99 
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti
LINK 	 = 
LLIB     =  
#LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lxil -lc -lm --end-group -lgcc
LIBS     =  -lc -lgcc
LFLAGS   = --gc-sections 

VPATH   += $(OUTPUT) $(RESULT)


all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).a

$(RESULT)/$(PRJ).a: $(LINK) $(OBJS_C)
	@echo "Creating Archive $@"
	$(AR) -rc $@ $(ALL_OBJS)


include ../../../common/rules.mk
