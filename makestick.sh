cp ../ult64/target/u64_loader/work/u64_loader.sof $1
mkdir $1/u64
mkdir $1/logs
cp roms/1541.bin $1/u64/1541.rom
cp roms/1571.bin $1/u64/1571.rom
cp roms/1581.bin $1/u64/1581.rom
cp roms/snds1541.bin $1/u64
cp roms/snds1571.bin $1/u64
cp roms/snds1581.bin $1/u64
make u64
cp update.u64 $1
cp target/software/nios2_u64/result/ultimate.app $1/u64
cp target/software/nios2_update_u64a4/output/u64.swp $1/u64

