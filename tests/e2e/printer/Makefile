# Build/check the virtual-printer E2E harness.
#
# The PRG is normally assembled on the fly by printer-e2e.py; this Makefile
# exists for standalone syntax-checking and for producing a PRG you can load
# by hand (e.g. via the Ultimate file browser) while debugging.

ASSEMBLER ?= 64tass
ASM_SRC    = printer-e2e.asm
PRG_OUT    = printer-e2e.prg

.PHONY: all assemble check clean

all: assemble

assemble: $(PRG_OUT)

$(PRG_OUT): $(ASM_SRC)
	$(ASSEMBLER) -q -o $(PRG_OUT) $(ASM_SRC)

check: assemble
	python3 -m py_compile printer-e2e.py verify-printer-output.py
	@echo "printer-e2e.asm assembled and Python sources compiled cleanly"

clean:
	rm -f $(PRG_OUT) *.pyc
	rm -rf __pycache__
