TARGETDIR = target
PLAYERDIR = player
ADVPLAYERDIR = player/advanced

OBJECTS = $(TARGETDIR)/sidcrt.bin $(TARGETDIR)/player.bin $(TARGETDIR)/advancedplayer.bin

all: ${TARGETDIR} ${OBJECTS}

$(TARGETDIR)/player.bin: $(PLAYERDIR)/player.asm
	./tools/64tass --nostart -o $@ $(PLAYERDIR)/player.asm

$(TARGETDIR)/advancedplayer.bin: $(ADVPLAYERDIR)/advancedplayer.asm $(ADVPLAYERDIR)/clock.asm $(ADVPLAYERDIR)/math.asm $(ADVPLAYERDIR)/timebar.asm $(ADVPLAYERDIR)/keyboard.asm
	./tools/64tass --nostart -o $@ $(ADVPLAYERDIR)/advancedplayer.asm

$(TARGETDIR)/sidcrt.bin: sidcrt.asm relocator.asm memalloc.asm detection.asm songlengths.asm $(TARGETDIR)/player.bin $(TARGETDIR)/advancedplayer.bin
	./tools/64tass -o $@ -b sidcrt.asm

${TARGETDIR}:
	if not exist $@ (mkdir $@)

clean:
	del $(TARGETDIR)/*.bin
