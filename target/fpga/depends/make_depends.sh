cd ../u2plus_ecp5 && python ../depends/lattice_depends.py u2p_ecp5.ldf >../depends/u2pl.txt
cd ../u2plus_run && python ../depends/quartus_depends.py ultimate_run.qsf >../depends/u2p.txt
python ../depends/ise_depends.py ../ultimate_logic4.inc ../ultimate_logic_loader.inc >../depends/u2.txt
