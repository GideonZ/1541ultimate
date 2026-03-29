riscv32-unknown-elf-as multest.s -o multest.o
riscv32-unknown-elf-ld -T link.ld -o multest.elf multest.o --nostdlib
riscv32-unknown-elf-objcopy -O binary multest.elf multest.bin
hexdump -v -e '1/4 "%08x\n"' multest.bin > multest.hex

