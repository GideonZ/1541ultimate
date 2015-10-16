VPATH += $(OUTPUT)

OBJS_ASM = $(notdir $(SRCS_ASM:%.s=%.o))
OBJS_C   = $(notdir $(SRCS_C:%.c=%.o))
OBJS_CC  = $(notdir $(SRCS_CC:%.cc=%.o))
OBJS_6502 = $(notdir $(SRCS_6502:%.tas=%.o))
OBJS_IEC = $(notdir $(SRCS_IEC:%.iec=%.o))
OBJS_NANO = $(notdir $(SRCS_NANO:%.nan=%.o))
OBJS_BIN = $(notdir $(SRCS_BIN:%.bin=%.o))
CHK_BIN  = $(notdir $(SRCS_BIN:%.bin=%.chk))

ALL_OBJS      = $(addprefix $(OUTPUT)/,$(OBJS_6502) $(OBJS_ASM) $(OBJS_C) $(OBJS_CC) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO))
ALL_DEP_OBJS  = $(addprefix $(OUTPUT)/,$(OBJS_C) $(OBJS_CC))


.PHONY: clean all mem

all: $(OUTPUT) $(RESULT) $(FINAL)
mem: $(OUTPUT)/$(PRJ).mem

$(OUTPUT):
	@mkdir $(OUTPUT)

$(RESULT):
	@mkdir $(RESULT)
		
$(RESULT)/$(PRJ).a: $(OBJS_C)
	@echo Creating Archive $@
	$(AR) -rc $@ $(ALL_OBJS)

$(RESULT)/$(PRJ).u2u: $(OUTPUT)/$(PRJ).out
	@echo Creating Updater Binary $@
	@$(OBJCOPY) -O binary $< $@

$(RESULT)/$(PRJ).bin: $(OUTPUT)/$(PRJ).out
	@echo Creating Binary $@
	@$(OBJCOPY) -O binary $< $@

%.chk: %.bin
	@echo Calculating checksum of $(<F) binary to $(@F)..
	@$(CHECKSUM) $< $(OUTPUT)/$(@F)

%.chk: %.b
	@echo Calculating checksum of $< binary to $(@F)..
	@$(CHECKSUM) $(OUTPUT)/$(<F) $(OUTPUT)/$(@F)

%.o: %.bin
	@echo Converting $(<F) binary to $(@F)..
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$<))))
	@$(eval becomes := _$(subst .,_,$(subst -,_,$(<F))))
	@$(OBJCOPY) -I binary -O elf32-microblaze --binary-architecture MicroBlaze $< $(OUTPUT)/$@ \
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
	@$(OBJCOPY) -I binary -O elf32-microblaze --binary-architecture MicroBlaze $(fn) $(OUTPUT)/$@ \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end

%.o: %.65
	@echo Converting binary $(<F) to .o
	@$(eval fn := $(OUTPUT)/$(<F))
	@$(eval was := _binary_$(subst .,_,$(subst /,_,$(subst -,_,$(fn)))))
	@$(eval becomes := _$(subst .,_,$(subst /,_,$(<F))))
	@$(OBJCOPY) -I binary -O elf32-microblaze --binary-architecture MicroBlaze $(fn) $(OUTPUT)/$@ \
	--redefine-sym $(was)_start=$(becomes)_start \
	--redefine-sym $(was)_size=$(becomes)_size \
	--redefine-sym $(was)_end=$(becomes)_end
    	
%.o: %.s
	@echo Compiling $(<F)
	@$(CC) $(OPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.c
	@echo Compiling $(<F)
	@$(CC) $(COPTIONS) $(PATH_INC) -B. -c -Wa,-ahlms=$(OUTPUT)/$(@:.o=.lst) -o $(OUTPUT)/$(@F) $<

%.o: %.cc
	@echo Compiling $(<F)
	@$(CPP) $(CPPOPT) $(PATH_INC) -B. -c -o $(OUTPUT)/$(@F) $<
	@$(CPP) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.d: %.cc
	@$(CPP) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

%.d: %.c
	@$(CC) -MM $(PATH_INC) $< >$(OUTPUT)/$(@F:.o=.d)

$(OUTPUT)/$(PRJ).out: $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO) $(LWIPLIB)
	@echo Linking...
	@$(LD) $(LLIB) $(LFLAGS) -T $(LINK) -Map=$(OUTPUT)/$(PRJ).map -o $(OUTPUT)/$(PRJ).out $(ALL_OBJS) $(LIBS)
	@$(SIZE) $(OUTPUT)/$(PRJ).out

$(OUTPUT)/$(PRJ).sim: $(RESULT)/$(PRJ).bin
	@echo Make mem...
	@$(MAKEMEM) $< $@ 1000000 65536 

$(OUTPUT)/$(PRJ).mem: $(RESULT)/$(PRJ).bin
	@echo Make mem...
	@$(MAKEMEM) $< $@ 2048

$(OUTPUT)/$(PRJ).m32: $(RESULT)/$(PRJ).bin
	@echo Make mem 32...
	@$(MAKEMEM) -w $< $@ 2048

# pull in dependency info for *existing* .o files
-include $(ALL_DEP_OBJS:.o=.d)

clean:
	@rm -rf $(OUTPUT)
	@rm -rf $(RESULT)

dep:  $(OBJS_CC:.o=.d) $(OBJS_C:.o=.d)

   
