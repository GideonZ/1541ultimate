
CROSS	     = nios2-elf-
ELFTYPE      = elf32-littlenios2
ARCHITECTURE = nios2

include ../common/environment.mk

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
			$(PATH_SW)/FreeRTOS/Source/MemMang \
			$(PATH_SW)/FreeRTOS/Source/portable/nios \
            $(PATH_SW)/niosbsp \
            $(PATH_SW)/niosbsp/HAL/inc \
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
OPTIONS  = -g -Os -mno-hw-div -mno-hw-mul -mno-hw-mulx -ffunction-sections -DNIOS
COPTIONS = $(OPTIONS) -std=c99 
CPPOPT   = $(OPTIONS) -fno-exceptions -fno-rtti
LINK 	 = ./mb_app.lds
LLIB     =  
#LIBS     = -lstdc++ -lsupc++ -lgcc --start-group -lxil -lc -lm --end-group -lgcc
LIBS     =  -lc -lgcc
LFLAGS   = --gc-sections 

VPATH   += $(OUTPUT) $(RESULT)


all: $(OUTPUT) $(RESULT) $(RESULT)/$(PRJ).a

$(RESULT)/$(PRJ).a: $(LINK) $(OBJS_C)
	@echo "Creating Archive $@"
	$(AR) -rc $@ $(ALL_OBJS)


include ../common/rules.mk
