#
# Host build of the SID player's per-UltiSID model rule.
#
#   make          build
#   make test     build and run
#
PRJ     = ultisidmodels
RESULT  = result
OUTPUT  = output

PATH_SW = ../../../../software

VPATH   = $(PATH_SW)/u64 $(PATH_SW)/test/ultisid_models

CXX     = g++
CXXOPTIONS = -std=c++11 -g -O0 -Wall -Wextra \
           $(addprefix -I, $(VPATH))

SRCS_CC = ultisid_models_test.cc
OBJS    = $(addprefix $(OUTPUT)/, $(SRCS_CC:%.cc=%.o))

.PHONY: all test clean

all: $(RESULT)/$(PRJ)

$(OUTPUT)/%.o: %.cc ultisid_models.h
	@mkdir -p $(OUTPUT)
	$(CXX) $(CXXOPTIONS) -c -o $@ $<

$(RESULT)/$(PRJ): $(OBJS)
	@mkdir -p $(RESULT)
	$(CXX) -o $@ $(OBJS)

test: all
	@$(RESULT)/$(PRJ)

clean:
	@rm -rf $(OUTPUT) $(RESULT)
