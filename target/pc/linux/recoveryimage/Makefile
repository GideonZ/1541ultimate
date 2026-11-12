#
# Host build of the update image reader of the control module.
#
#   make          build
#   make test     build and run
#
PRJ     = recoveryimage
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software

VPATH   = $(PATH_SW)/u64ctrl/main $(PATH_SW)/test/recovery_image

CC      = gcc
COPTIONS = -std=c99 -g -O0 -Wall -Wextra -Wno-unused-parameter \
           $(addprefix -I, $(VPATH))

SRCS_C  = recovery_image.c recovery_image_test.c
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
