
# Prebuilt libc and libgcc are picked from the toolchain multilib by -march/-mabi,
# so this must name an architecture whose libraries the target can execute; it is
# independent of the flags the sources are compiled with.
LIBMARCH ?= $(MARCH)
LIBS    += $(shell $(CC) -march=$(LIBMARCH) -mabi=$(MABI) -print-file-name=libc.a)
LIBS    += $(shell $(CC) -march=$(LIBMARCH) -mabi=$(MABI) -print-libgcc-file-name)

$(OUTPUT)/$(PRJ).out: $(LINK) $(OBJS_C) $(OBJS_CC) $(OBJS_ASM) $(OBJS_ASMS) $(OBJS_6502) $(OBJS_BIN) $(OBJS_HTML) $(OBJS_YAML) $(OBJS_IEC) $(OBJS_NANO) $(OBJS_RBF) $(OBJS_APP) $(LWIPLIB)
	@echo Linking using LD...
	$(LD) $(LLIB) $(LFLAGS) -T $(LINK) -Map=$(OUTPUT)/$(PRJ).map -o $@ $(ALL_OBJS) $(LIBS)
	@$(SIZE) $@
	
