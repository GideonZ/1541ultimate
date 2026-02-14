
LIBS    += $(shell $(CC) -march=$(MARCH) -print-file-name=libc.a)
LIBS    += $(shell $(CC) -march=$(MARCH) -print-libgcc-file-name)

$(OUTPUT)/$(PRJ).out: $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_ASMS) $(OBJS_6502) $(OBJS_BIN) $(OBJS_HTML) $(OBJS_IEC) $(OBJS_NANO) $(OBJS_RBF) $(OBJS_APP) $(LWIPLIB)
	@echo Linking using LD...
	$(LD) $(LLIB) $(LFLAGS) -T $(LINK) -Map=$(OUTPUT)/$(PRJ).map -o $@ $(ALL_OBJS) $(LIBS)
	@$(SIZE) $@
	
