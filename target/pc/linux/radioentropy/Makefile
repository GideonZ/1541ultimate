# Host build of radio_entropy.c, the helper that keeps the hardware RNG fed with the radio
# on or off, with wifi_radio.c that starts and stops the radio through it, against a model
# of the chip that fails the test on any call the hardware would not allow. `make test`
# builds and runs it.
PRJ     = radioentropy
PATH_SW = ../../../../software

# rpc_calls.h of raw_u64 needs lwIP's headers, which the wifi_random test stubs out
VPATH   = $(PATH_SW)/wifi/common $(PATH_SW)/test/radio_entropy $(PATH_SW)/test/wifi_random \
          $(PATH_SW)/wifi/raw_u64/main

CC      = gcc
CFLAGS  = -std=gnu11 -g -O0 -Wall -Wextra -Wno-unused-parameter -pthread \
          $(addprefix -I, $(VPATH))

OUTPUT  = output
RESULT  = result
SRCS    = radio_entropy.c wifi_radio.c radio_entropy_test.c
OBJS    = $(addprefix $(OUTPUT)/, $(SRCS:%.c=%.o))

.PHONY: all run test clean

all: $(RESULT)/$(PRJ)

$(OUTPUT)/%.o: %.c
	@mkdir -p $(OUTPUT)
	$(CC) $(CFLAGS) -c -o $@ $<

$(RESULT)/$(PRJ): $(OBJS)
	@mkdir -p $(RESULT)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

run: all
	@$(RESULT)/$(PRJ)


test: run

clean:
	@rm -rf $(OUTPUT) $(RESULT)
