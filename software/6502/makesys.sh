../../tools/64tass/64tass -b u64_sysrom.tas -o sysrom.bin
../../tools/bin2hex sysrom.bin ../../../ult64/target/u64_a4/system_roms.hex

../../tools/64tass/64tass -b u64ii_default_kernal.tas -o u64iidef.bin
