all:
	ca65 -g basic/basic.s
	ca65 -g kernal/kernal.s
	ld65 -C rom.cfg -o rom.bin basic/basic.o kernal/kernal.o -Ln rom.txt
	# if it's the unchanged 901226-01 image, use old checksum algorithm
	$$SHELL -c "if [ $$(crc32 basic.bin) == cfdebff8 ]; then \
		python checksum.py --old basic.bin 0xa0 0x1f52; \
	else \
		python checksum.py --new basic.bin 0xa0 0x1f52; \
	fi"
	python checksum.py --new kernal.bin 0xe0 0x4ac

clean:
	rm -f basic/basic.o kernal/kernal.o basic.bin kernal.bin rom.bin
