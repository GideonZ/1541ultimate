* In order to build the various versions (U2 and U2+), you need the following toolchains:
U2+:
- Altera Quartus (tested with 15.1 Lite Edition) 

U2:
- Xilinx ISE 13.x, or 14.x
- Xilinx SDK 2016.3

In the near future, you may need the Xilinx SDK as well for the U2+, if you like to build the "Turbo" version.
The Nios-IIe processor that is license free has the advantage that you can use a JTAG debugger to test/debug
your code, but it is significantly slower than Microblaze. Because I am using a custom version of Microblaze,
written in plain VHDL (written by an EE student, and debugged by myself), it can be targeted for Altera as well.

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

==> You may compile the sources using ISE 13, with the built-in SDK. This works!


Environment variables
=====================
In order for the build to work, you must have an "ise_locations.inc" file, with the environment variable
ISE_LOCATIONS_FILE_PATH pointing to it. For instance, use:

export ISE_LOCATIONS_FILE_PATH="/home/yourhomedir/.ise_locations.inc"

In this file, you need to set the location of your Xilinx ISE toolchain.

Building targets
================
In order to build for U2, use the 'mk3' target in the top level makefile. This will build the FPGAs as well
as the microblaze software, including all updaters.

In order to build for U2+, use the 'u2plus' target. This will build the FPGAs as well as the NiosII software.

If you have already built the FPGAs, and only wish to update the software running on it, you can use the
target 'niosapps'. This target will only succeed if you have already successfully built the FPGAs.

==> When using the top level makefile, the results are copied into the root of the project.


If you are running into library issues, it may very well be that the LD_LIBRARY_PATH variable it set and points
to Xilinx or Altera system libraries that are incompatible with your Linux distribution. Usually, clearing this
variable will solve the problems. For this reason, I do not run the configuration settings shell file for
Quartus, but I have included this in my .bashrc:

export QSYS_ROOTDIR="/opt/altera_lite/15.1/quartus/sopc_builder/bin"
export ALTERAOCLSDKROOT="/opt/altera_lite/15.1/hld"
export QUARTUS_ROOTDIR="/opt/altera_lite/15.1/quartus"
export PATH=$PATH:$QUARTUS_ROOTDIR/
export PATH=$PATH:/opt/altera_lite/15.1/quartus/bin
export PATH=$PATH:/opt/altera_lite/15.1/nios2eds/bin
export PATH=$PATH:/opt/altera_lite/15.1/nios2eds/sdk2/bin
export PATH=$PATH:/opt/altera_lite/15.1/nios2eds/bin/gnu/H-x86_64-pc-linux-gnu/bin
export PATH=$PATH:/opt/altera_lite/15.1/quartus/sopc_builder/bin

