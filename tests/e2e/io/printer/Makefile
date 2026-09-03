# Regenerate the committed PRG fixtures this suite runs on the C64.
#
# printer_test.py loads printer_e2e.prg and issue_717_basic.prg directly, so
# running the suite needs no assembler and no BASIC tokenizer. Both PRGs are
# committed. Run `make` here only after editing printer_e2e.asm or
# issue_717_basic.bas, and commit the regenerated PRG with the source change.

ASSEMBLER ?= 64tass
TOKENIZER ?= petcat

ASM_SRC    = printer_e2e.asm
ASM_PRG    = printer_e2e.prg
BASIC_SRC  = issue_717_basic.bas
BASIC_PRG  = issue_717_basic.prg

.PHONY: all check clean

all: $(ASM_PRG) $(BASIC_PRG)

$(ASM_PRG): $(ASM_SRC)
	$(ASSEMBLER) -q -o $@ $<

$(BASIC_PRG): $(BASIC_SRC)
	$(TOKENIZER) -w2 -o $@ $<

check: all
	python3 -m py_compile printer_test.py verify_printer_output.py png_lite.py
	@echo "fixtures rebuilt and Python sources compiled cleanly"
	@git diff --stat -- $(ASM_PRG) $(BASIC_PRG)

clean:
	rm -f *.pyc
	rm -rf __pycache__
