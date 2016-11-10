
$(OUTPUT)/$(PRJ).out: $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_ASMS) $(OBJS_6502) $(OBJS_BIN) $(OBJS_IEC) $(OBJS_NANO) $(OBJS_RBF) $(OBJS_APP) $(LWIPLIB) $(OBJS_RAW)
	@echo Linking using GCC...
	$(CPP) -Wl,-Map=$(OUTPUT)/$(PRJ).map,$(LFLAGS) -T'$(LINK)' $(OPTIONS) -o $@ $(ALL_OBJS) $(LIBS2)
	@$(SIZE) $@

