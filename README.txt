WELCOME TO THE OFFICIAL REPOSITORY FOR THE COMMODORE 64 ULTIMATE
================================================================

Before you continue, be aware that this git repository uses some submodules. Make sure you also
clone them by issuing the following commands after cloning this repository:
$ git submodule init
$ git submodule update

* In order to build the various versions, you need the following toolchains:

U2:
- RiscV GNU C/C++ Compiler (for RV32I) for all firmware versions based on RiscV processor.
- Microblaze builds are no longer supported.

U2+:
- Altera Quartus (tested with 18.1 Lite Edition) 

U2+L:
- Lattice Diamond 3.13 with a (free) generated license
- RiscV GNU C/C++ Compiler (for RV32I), same as for U2/RiscV.
- Espressif IDF 5.x

U64:
- Altera Quartus (tested with 18.1 Lite Edition)  - for the Nios-II compiler only.
- Espressif IDF 5.x

U64E-II:
- RiscV GNU C/C++ Compiler (for RV32I), same as for U2/RiscV.
- Espressif IDF 5.x


Environment variables
=====================
In order for the build to work, you must have an "ise_locations.inc" file, with the environment variable
ISE_LOCATIONS_FILE_PATH pointing to it. For instance, use:

export ISE_LOCATIONS_FILE_PATH="/home/yourhomedir/.ise_locations.inc"

In this file, you need to set the location of your Xilinx ISE toolchain.

Building targets
================
ALL targets can be built by just typing 'make' in the root of the repository.

In order to build for U2 only, use the 'u2_rv' target in the top level makefile. This will build the FPGAs as well
as the RiscV based software, including all updaters. 

In order to build for U2+, use the 'u2plus' target. This will build the FPGAs as well as the NiosII software.

If you have already built the FPGAs, and only wish to update the software running on it, you can use the
target 'niosapps'. This target will only succeed if you have already successfully built the FPGAs.

For the U2+L, you can build the 'u2pl' target.

The U64 firmware can be built by typing 'make u64'.

The U64E-II firmware can be built by typing 'make u64ii'

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

