CC = gcc
OBJ = 64tass.o opcodes.o misc.o
#CFLAGS = -O2 -march=i486 -mcpu=i486 -pipe
CFLAGS = -Wall -pipe -I.

64tass: $(OBJ)

64tass.o: 64tass.c opcodes.h misc.h

opcodes.o: opcodes.c opcodes.h

misc.o: misc.c misc.h opcodes.h

#argp-parse.o: argp-parse.c argp-namefrob.h

.PHONY: clean strip

clean:
	rm -f $(OBJ) 64tass *~

strip:
	strip -R .note -R .comment -R .note.ABI-tag 64tass
