TOOLS = ../../../tools
TARGETDIR = target
PLAYERDIR = player
ADVPLAYERDIR = player/advanced

OBJECTS = $(TARGETDIR)/sidcrt.bin $(TARGETDIR)/player.bin $(TARGETDIR)/advancedplayer.bin

all: ${TARGETDIR} ${OBJECTS}

$(TARGETDIR)/player.bin: $(PLAYERDIR)/player.asm
	$(TOOLS)/64tass/64tass --nostart -o $@ $(PLAYERDIR)/player.asm

$(TARGETDIR)/advancedplayer.bin: $(ADVPLAYERDIR)/advancedplayer.asm $(ADVPLAYERDIR)/clock.asm $(ADVPLAYERDIR)/math.asm $(ADVPLAYERDIR)/timebar.asm $(ADVPLAYERDIR)/keyboard.asm
	$(TOOLS)/64tass/64tass --nostart -D INCLUDE_RUNSTOP=0 -o $@ $(ADVPLAYERDIR)/advancedplayer.asm

$(TARGETDIR)/sidcrt.bin: sidcrt.asm relocator.asm memalloc.asm detection.asm songlengths.asm sidfx.asm $(TARGETDIR)/player.bin $(TARGETDIR)/advancedplayer.bin
	$(TOOLS)/64tass/64tass -o $@ -b sidcrt.asm

${TARGETDIR}:
	mkdir -p $@

clean:
	rm -rf $(TARGETDIR)
