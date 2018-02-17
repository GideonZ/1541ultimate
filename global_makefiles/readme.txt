Makefile environment for programmable logic development.

The environment is a work in progress, so use with care. It is
possible future changes will be incompatible with the current
setup.


How to use:

----------------------------------------------------------------
- Global
----------------------------------------------------------------

Copy the make_includes directory to the project tree.

These build scripts make use of the 'tl_env' command. This
command should be available on the Technolution build servers.

It should also be possible to build everything on another server
without modifications to the archive, by either implementing the
tl_env command on that server or making sure that all necessary
tools can be found in the sytem PATH. The latter is only
possible if no conflicting tools (e.g. different versions of the
same tool) are used.

----------------------------------------------------------------
- Xilinx synthesis
----------------------------------------------------------------

Copy the makefile from the xilinx_synthesis directory to
the synth directory in you project tree. Modify the
variables to correspond to the required settings. Be sure
to set the GLOBAL_INCS variable to the location of the
make_includes directory if it is not found automatically (this
should normally not be necessary).

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
variable to the location of the make_includes directory if it is
not found automatically (this should normally not be necessary).

Use the command

> make altera-info

to see the available targets.

----------------------------------------------------------------
- ModelSim simulation
----------------------------------------------------------------

Copy the makefile from the vsim_simulation directory to
the sim directory in you project tree. Modify the
variables to correspond to the required settings. Make sure all
targets (testbenches, test cases and regression tests) have
unique names. Furthermore, make sure to set the GLOBAL_INCS
variable to the location of the global_makefile directory if it
is not found automatically (this should normally not be
necessary).

If the VHDL source files are written according to the
Technolution guidelines, the dependencies will be detected
automatically. If this is not the case, you can add *.sinc
files containing the necessary dependency information.
[These files can be reused for the toplevel makefile. ??]

Use the command

> make vsim-info

to see the available targets.
