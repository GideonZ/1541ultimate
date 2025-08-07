WELCOME TO THE OFFICIAL REPOSITORY FOR THE ULTIMATE-II, ULTIMATE-II+, ULTIMATE-II+L and U64 FIRMWARE
====================================================================================================

Before you continue, be aware that this git repository uses some submodules. Make sure you also
clone them by issuing the following commands after cloning this repository:
$ git submodule init
$ git submodule update

* In order to build the various versions, you need the following toolchains:

U2:
- Xilinx ISE 14.7 for the FPGA itself
- Xilinx SDK 2018.2 for the software running on the Microblaze processor. Older / other versions
  DO NOT WORK or are unstable!
- RiscV GNU C/C++ Compiler (for RV32I) for all firmware versions based on RiscV processor.

U2+:
- Altera Quartus (tested with 18.1 Lite Edition) 

U2+L:
- Lattice Diamond 3.12 with a (free) generated license
- RiscV GNU C/C++ Compiler (for RV32I), same as for U2/RiscV.
- Espressif

U64:
- Altera Quartus (tested with 18.1 Lite Edition)  - for the Nios-II compiler only.
- Espressif


IMPORTANT NOTICE!!!!
====================

Xilinx made a complete mess out of their Microblaze compiler and support libraries in some of the ISE versions.

* ISE 14.5 - 14.7 have a bug that emits wrong code for 'shift operations', in combination with the -Os
  optimization flag; it shifts one bit too many. Do not use this version.
* ISE 14.4 comes with precompiled newlib (libc.a) that contains barrelshifter instructions, even for the non-
  barrel shifter enabled processor cores. Do not use this version.
* ISE 14.1 - 14.3: Not tested due to severe annoyance with other 14.x versions.
* SDK 2016.3 uses the GNU 5.2.0 compiler. This compiler is more strict than previous versions and also emits
  different code. The sources have been changed to support GNU 5. Important notice: When the compiler detects
  that you try to access address 0, it emits an endless loop to itself (lockup). Be aware, just in case you
  want to intentionally write address 0.   In order to run SDK2016.3, you will need to have 32 bit libraries
  installed:
sudo apt-get install libstdc++6:i386
sudo apt-get install libgtk2.0-0:i386
sudo apt-get install dpkg-dev:i386

* SDK 2018.2 uses the GNU 7.2.0 compiler. This compiler is again stricter than previous versions and also emits
  different code. The sources have been changed to support GNU 7. However, when using the -Os optimization flag,
  SOME return values from functions are incorrect. This is because the compiler swaps an instruction from the delay
  slot with another instruction, which is not allowed.

==> You may compile the sources using ISE 13, with the built-in SDK. This works!
==> Compiling the sources using the SDK 2018.2 is possible by adding the -mcpu=v5.00.0 flag to the compiler. This
    enables the use of PCMPEQ and PCMPNE instructions, which effectively fixes the branch delay swap issue. These
    instructions were added to the Microblaze core on Feb 23, 2019.
    
In retrospect, the Microblaze compiler has issues emitting correct code when the processor is crippled; i.e. when
it does not have a barrel shifter, nor a multiplier, nor pattern compare instructions. Once either the barrel shifter
or the pattern compare instructions are enabled, the emitted code seems to be correct.


Environment variables
=====================
In order for the build to work, you must have an "ise_locations.inc" file, with the environment variable
ISE_LOCATIONS_FILE_PATH pointing to it. For instance, use:

export ISE_LOCATIONS_FILE_PATH="/home/yourhomedir/.ise_locations.inc"

In this file, you need to set the location of your Xilinx ISE toolchain.

Building targets
================
ALL targets can be built by just typing 'make' in the root of the repository.

In order to build for U2 only, use the 'u2' target in the top level makefile. This will build the FPGAs as well
as the microblaze software, including all updaters. If you want the RiscV variants for the U2, run the
'u2_rv' make target.

In order to build for U2+, use the 'u2plus' target. This will build the FPGAs as well as the NiosII software.

If you have already built the FPGAs, and only wish to update the software running on it, you can use the
target 'niosapps'. This target will only succeed if you have already successfully built the FPGAs.

For the U2+L, you can build the 'u2pl' target.

The U64 firmware can be built by typing 'make u64'.

==> When using the top level makefile, the results are copied into the root of the project.

====================================================================================================

If you are running into library issues, it may very well be that the LD_LIBRARY_PATH variable it set and points
to Xilinx or Altera system libraries that are incompatible with your Linux distribution. Usually, clearing this
variable will solve the problems. For this reason, I do not run the configuration settings shell file for
Quartus, but I have included this in my .bashrc:

export QSYS_ROOTDIR="/opt/altera_lite/18.1/quartus/sopc_builder/bin"
export ALTERAOCLSDKROOT="/opt/altera_lite/18.1/hld"
export QUARTUS_ROOTDIR="/opt/altera_lite/18.1/quartus"
export PATH=$PATH:$QUARTUS_ROOTDIR/
export PATH=$PATH:/opt/altera_lite/18.1/quartus/bin
export PATH=$PATH:/opt/altera_lite/18.1/nios2eds/bin
export PATH=$PATH:/opt/altera_lite/18.1/nios2eds/sdk2/bin
export PATH=$PATH:/opt/altera_lite/18.1/nios2eds/bin/gnu/H-x86_64-pc-linux-gnu/bin
export PATH=$PATH:/opt/altera_lite/18.1/quartus/sopc_builder/bin

Additional issues
=================
1) Some users report Nios Build Tools for Eclipse fails to load. If this occurs, try adding this line to you .bashrc file:
export SWT_GTK3=0

2) Ubuntu users may need to install an older deprecated version of libpng for Quartus 18.1 to open.  Unfortunately this is difficult to find online. Below is a reliable source as of this writing:

wget -q -O /tmp/libpng12.deb http://mirrors.kernel.org/ubuntu/pool/main/libp/libpng/libpng12-0_1.2.54-1ubuntu1_amd64.deb \
>   && dpkg -i /tmp/libpng12.deb \
>   && rm /tmp/libpng12.deb

