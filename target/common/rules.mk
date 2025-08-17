VPATH += $(OUTPUT)

OBJS_ASMS = $(notdir $(SRCS_ASMS:%.S=%.o))
OBJS_ASM = $(notdir $(SRCS_ASM:%.s=%.o))
OBJS_C   = $(notdir $(SRCS_C:%.c=%.o))
OBJS_CC  = $(notdir $(SRCS_CC:%.cc=%.o))
OBJS_6502 = $(notdir $(SRCS_6502:%.tas=%.o))
OBJS_IEC = $(notdir $(SRCS_IEC:%.iec=%.o))
OBJS_NANO = $(notdir $(SRCS_NANO:%.nan=%.o))
OBJS_BIN = $(notdir $(SRCS_BIN:%.bin=%.o))
OBJS_HTML = $(notdir $(SRCS_HTML:%.html=%.o))
OBJS_RBF = $(notdir $(SRCS_RBF:%.rbf=%.o))
OBJS_APP = $(notdir $(SRCS_APP:%.app=%.ao))
CHK_BIN  = $(notdir $(SRCS_BIN:%.bin=%.chk))

ALL_OBJS      = $(addprefix $(OUTPUT)/,$(OBJS_ASM) $(OBJS_ASMS) $(OBJS_C) $(OBJS_CC) $(OBJS_IEC) $(OBJS_NANO) $(OBJS_6502) $(OBJS_BIN) $(OBJS_RBF) $(OBJS_APP) $(OBJS_HTML)) 
ALL_DEP_OBJS  = $(addprefix $(OUTPUT)/,$(OBJS_C) $(OBJS_CC))


.PHONY: clean all mem gitinfo

all: $(OUTPUT) gitinfo $(RESULT) $(FINAL)
	@echo Making all requires $(OUTPUT), $(RESULT) and $(FINAL)	

gitinfo::
	@echo '/* Generated file, do not edit. */' >$(OUTPUT)/gitinfo.h
	@echo '#define APP_VERSION_TAG    "'`git describe --tags`'"' >>$(OUTPUT)/gitinfo.h
	@echo '#define APP_VERSION_BRANCH "'`git describe --all`'"' >>$(OUTPUT)/gitinfo.h
	@echo '#define APP_VERSION_DATE   "'`git log -n 1 --format=%ai`'"' >>$(OUTPUT)/gitinfo.h
	@echo '#define APP_VERSION_HASH   "'`git rev-parse --short HEAD`'"' >>$(OUTPUT)/gitinfo.h
	@echo '#define APP_BUILD_DATE     "'`date +"%F %R"`'"' >>$(OUTPUT)/gitinfo.h
	@echo '#define APP_BUILD_MACHINE  "'`hostname`'"' >>$(OUTPUT)/gitinfo.h

mem: $(OUTPUT)/$(PRJ).mem

$(OUTPUT):
	@echo creating output directory $(OUTPUT)
	@mkdir -p $(OUTPUT)

$(RESULT):
	@echo creating result directory $(RESULT)
	@mkdir -p $(RESULT)
		
$(RESULT)/$(PRJ).a: $(OBJS_C)
	@echo Creating Archive $@
	$(AR) -rc "$@" $(ALL_OBJS)

$(RESULT)/$(PRJ).u2u: $(OUTPUT)/$(PRJ).out
	@echo Creating Updater Binary $@
	@$(OBJCOPY) -O binary $< $@

$(RESULT)/$(PRJ).bin: $(OUTPUT)/$(PRJ).out
	@echo Creating Binary $@
	@$(OBJCOPY) -O binary $< $@

$(RESULT)/$(PRJ).app: $(OUTPUT)/$(PRJ).shex
	@echo Creating Binary Application $@
	@$(HEX2BIN) -r $< $@

$(RESULT)/$(PRJ).hex: $(OUTPUT)/$(PRJ).out
	@echo Creating Hex File for On Chip Memory $@
	@elf2hex --input=$< 0x0000 $(HEXLAST) --width=32 --little-endian-mem --create-lanes=0 --record=4 --output=$@ --base=$(HEXBASE) --end=$(HEXEND)

$(OUTPUT)/$(PRJ).shex: $(OUTPUT)/$(PRJ).out
	@echo Creating Hex File to extract startaddr $@
	@$(OBJCOPY) -O ihex  $< $@

#	@$(BIN2HEX) $< $@

%.chk: %.bin
	@echo Calculating checksum of $(<F) binary to $(@F)..
	@$(CHECKSUM) $< $(OUTPUT)/$(@F)

%.chk: %.b
	@echo Calculating checksum of $< binary to $(@F)..
	@$(CHECKSUM) $(OUTPUT)/$(<F) $(OUTPUT)/$(@F)

%.b: %.svf
	@echo Converting $(<F) to binary $(@F)..
	@$(SVFDUMP) $< >$(OUTPUT)/svf_dump.txt
	@mv dump.bin $(OUTPUT)/$@

%.o: %.bin
	@echo Converting $(<F) binary to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _$(subst .,_,$(subst -,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $< $(OUTPUT)/$@ \
	--rename-section .data=.rodata,alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.o: %.html
	@echo Converting $(<F) html to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _$(subst .,_,$(subst -,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $< $(OUTPUT)/$@ \
	--rename-section .data=.rodata,alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.bin: %.srec
	@echo Converting $(<F) SREC to $(@F)..
	@$(OBJCOPY) -I srec -O binary $< $@

%.swp: %.rbf
	@echo Swapping FPGA Image $(<F)
	@$(SWAP) $< $(OUTPUT)/$@
	
# Essentially do a swapped copy of the rbf first to the output directory and then convert to elf..
%.o: %.rbf
	@echo Converting $(<F) FPGA Image to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _$(subst .,_,$(subst -,_,$(<F))))
	@$(SWAP) $< $(OUTPUT)/$(@F)
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $(OUTPUT)/$(@F) $(OUTPUT)/$@ \
	--rename-section .data=.rodata.$(<F),alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.ao: %.app
	@echo Converting $(<F) binary to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _$(subst .,_,$(subst -,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $< $(OUTPUT)/$@ \
	--rename-section .data=.rodata.$(<F),alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.65: %.tas
	@echo Assembling $<
	@$(TASS) $< --m6502 --nostart -o $(OUTPUT)/$(@F)

%.b: %.iec
	@echo Assembling IEC processor code $(<F)
	@python $(PARSE_IEC) $< $(OUTPUT)/$(@F) >python_out_iec

%.b: %.nan
	@echo Assembling Nano processor code $(<F)
	@python $(PARSE_NANO) $< $(OUTPUT)/$(@F) >python_out_nano

%.b: %.bit
	@echo "Converting $(<F) bitfile to $(@F)"
	@$(PROMGEN) -r $< $(OUTPUT)/$(@F) 

%.o: %.b
	@echo Converting binary $(<F) to .o
	@$(eval fn := $(OUTPUT)/$(<F))
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$(fn)))))
	@$(eval becomes := _$(subst .,_,$(subst /,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $(fn) $(OUTPUT)/$@ \
	--rename-section .data=.rodata.$(<F),alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.o: %.65
	@echo Converting binary $(<F) to .o
	@$(eval fn := $(OUTPUT)/$(<F))
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$(fn)))))
	@$(eval becomes := _$(subst .,_,$(subst /,_,$(<F))))
	@$(OBJCOPY) -I binary -O $(ELFTYPE) --binary-architecture $(ARCHITECTURE) $(fn) $(OUTPUT)/$@ \
	--rename-section .data=.rodata.$(<F),alloc,load,readonly,data,contents \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end
    	
%.o: %.s
	@echo Assembling $(<F)
	@$(CC) $(OPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.S
	@echo Assembling $(<F)
	@$(CC) $(OPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.c
	@echo Compiling $(<F)
	@$(CC) $(COPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<
	@$(CC) $(COPTIONS) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.o: %.cc
	@echo Compiling $(<F)
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<
	@$(CPP) $(CPPOPT) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.d: %.cc
	@$(CPP) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.d: %.c
	@$(CC) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

$(RESULT)/$(PRJ).elf: $(OUTPUT)/$(PRJ).out $(RESULT)
	@echo Providing ELF.
	@cp $(OUTPUT)/$(PRJ).out $(RESULT)/$(PRJ).elf
	
$(OUTPUT)/$(PRJ).s00: $(RESULT)/$(PRJ).bin
	@echo Make mem...
	@$(MAKEMEM) -w $< $@ 1000000

$(OUTPUT)/$(PRJ).sim: $(RESULT)/$(PRJ).bin
	@echo Make mem...
	@$(MAKEMEM) -w $< $@ 1000000 $(SIMOFFSET) 

$(OUTPUT)/$(PRJ).mem: $(RESULT)/$(PRJ).bin
	@echo Make mem...
	@$(MAKEMEM) $< $@ 2048

$(OUTPUT)/$(PRJ).m32: $(RESULT)/$(PRJ).bin
	@echo Make mem 32...
	@$(MAKEMEM) -w $< $@ 2048

LINKMETHOD ?= gcc

include ../../../common/$(LINKMETHOD).mk

# pull in dependency info for *existing* .o files
-include $(ALL_DEP_OBJS:.o=.d)

clean:
	@rm -rf $(OUTPUT) $(RESULT) $(OUTPUTDIRS) python*out*

dep:  $(OBJS_CC:.o=.d) $(OBJS_C:.o=.d)

   
