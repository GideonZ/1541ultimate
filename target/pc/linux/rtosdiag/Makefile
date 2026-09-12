#
# Host build of the stack overflow hook and the stack and heap report in
# software/system/assert.c, against stand-ins for the kernel headers.
#
#   make          build
#   make test     build and run
#
PRJ     = rtosdiag
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software

VPATH   = $(PATH_SW)/system $(PATH_SW)/test/rtos_diag

CC      = gcc
# The stand-ins come first, so assert.c finds them instead of the kernel headers,
# and the real FreeRTOSConfig.h is found after them.
COPTIONS = -std=c99 -g -O0 -Wall -Wno-unused-parameter \
           -I$(PATH_SW)/test/rtos_diag -I$(PATH_SW)/FreeRTOS/Source -I$(PATH_SW)/system

SRCS_C  = assert.c rtos_diag_test.c
OBJS    = $(addprefix $(OUTPUT)/, $(SRCS_C:%.c=%.o))

.PHONY: all test clean

all: $(RESULT)/$(PRJ)

$(OUTPUT)/%.o: %.c
	@mkdir -p $(OUTPUT)
	$(CC) $(COPTIONS) -c -o $@ $<

$(RESULT)/$(PRJ): $(OBJS)
	@mkdir -p $(RESULT)
	$(CC) -o $@ $(OBJS)

test: all
	@$(RESULT)/$(PRJ)

clean:
	@rm -rf $(OUTPUT) $(RESULT)
