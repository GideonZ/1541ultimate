Makefile environment for VHDL development.

Currently under heavy development, so use with care. It is
possible future changes will be incompatible with the current
setup.


How to use:

----------------------------------------------------------------
- Global
----------------------------------------------------------------

Copy the global_makefiles directory to the project tree.

Rename the files ise_locations-example.inc and
quartus_locations-example.inc to ise_locations.inc and
quartus_locations respectively. Change the paths in these file
according to your system setup. These files should not be checked
into the project repository because they can be different for
different developers.

----------------------------------------------------------------
- Xilinx synthesis
----------------------------------------------------------------

Copy the makefile from the xilinx_synthesis directory to
the synth directory in you project tree. Modify the
variables to correspond to the required settings. Be sure
to set the GLOBAL_INCS variable to the location of the
global_makefile directory.

Use the command

> make xilinx-info

to see the available targets.

----------------------------------------------------------------
- Altera synthesis
----------------------------------------------------------------

Make a project file using the quartus design environment. This
project file should contain all settings. Copy the makefile from
the altera_synthesis directory to the synth directory in your
project tree (at the same location of the quartus project file).
Modify the variables to correspond to the required settings (e.g.
project name, project files, etc). Be sure to set the GLOBAL_INCS
variable to the location of the global_makefile directory.

Use the command

> make altera-info

to see the available targets.

NOTE: the altera makefiles are untested.

----------------------------------------------------------------
- vsim simulation
----------------------------------------------------------------

Copy the makefile from the vsim_simulation directory to
the sim directory in you project tree. Modify the
variables to correspond to the required settings. Make sure all
targets (testbenches, testcases and regression tests) have
unique names. Furthermore, make sure to set the GLOBAL_INCS
variable to the location of the global_makefile directory.
Add the <module>.sinc files with the information of a module.
These files can be reused for the toplevel makefile.

Use the command

> make vsim-info

to see the available targets.


