#
# Host build of the reply table of the WiFi command channel.
#
#   make          build
#   make test     build and run
#   make legacy   build and run the model of the table before the fix; it fails
#
PRJ     = wifireply
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software

VPATH   = $(PATH_SW)/io/wifi $(PATH_SW)/test/wifi_reply

CC      = gcc
COPTIONS = -std=c99 -g -O0 -Wall -Wextra -Wno-unused-parameter \
           $(addprefix -I, $(VPATH))

SRCS_C  = wifi_reply_test.c
OBJS    = $(addprefix $(OUTPUT)/, $(SRCS_C:%.c=%.o))

.PHONY: all test legacy clean

all: $(RESULT)/$(PRJ)

$(OUTPUT)/%.o: %.c wifi_reply.h
	@mkdir -p $(OUTPUT)
	$(CC) $(COPTIONS) -c -o $@ $<

$(RESULT)/$(PRJ): $(OBJS)
	@mkdir -p $(RESULT)
	$(CC) -o $@ $(OBJS)

test: all
	@$(RESULT)/$(PRJ)

legacy:
	@mkdir -p $(OUTPUT)
	$(CC) $(COPTIONS) -DWIFI_REPLY_LEGACY -o $(OUTPUT)/legacy $(PATH_SW)/test/wifi_reply/wifi_reply_test.c
	@$(OUTPUT)/legacy

clean:
	@rm -rf $(OUTPUT) $(RESULT)
