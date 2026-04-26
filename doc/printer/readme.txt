This directory contains some files and tools to test the printer
as a standlone program. This is supposed to run on a linux box but
you may adapt it to run on other systems as it's very standard C
and C++ code

Usefull files :
---------------
chargen_draft_table.png
chargen_italic_table.png
chargen_nlq_table.png

These PNG image files will show you all the chars that can be printed
they are defined in the file mps_chargen.cc and the number of each char
is the index in each corresponding array.

Standalone Printer :
--------------------
copy all the files starting with mps_ from the software/io/printer 
directory to same directory as the Makefile and source files in this
directory.

type make to build the execulable files (you must have a working gcc
compiler for C and C++ installed)

Normally you will have 5 executable files :
epsontest ibmgptest ibmpptest and cbmtest will sent to standard output
a RAW printer data file that you can redirect to a file to submit it
to the standalone virtual printer.

The standalone printer will be called printer and you can use this
calling syntax :

USAGE: printer [-d 0-3] [-f file] [-c n] [-t n] [-l n] [-sepg] file1 [file2 ...]
    -d : dot size 0, 1, 2 or 3
    -f : base filename (default is mps)
    -c : charset num (default is 0)
    -s : commodore secondary charset (upper/lower)
    -j : color printer (default is greyscale)
    -e : Epson FX80/JX80 emulation
    -p : IBM Proprinter emulation
    -g : IBM Graphics printer emulation
    -t : top margin (0-69, default 5)
    -l : page height (1-70, default 60)
default emulation if no option given is Commodore MPS

This is a very simple tool for testing purposees and parameter limits are not tested.

Charset num :
-------------
Commodore MPS emulation : 0=USA/UK 1=Denmark 2=France/Italy 3=Germany 4=Spain 5=Sweden 6=Switzerland
EPSON emulation : 0=Basic 1=USA 2=France 3=Germany 4=UK 5=Denmark-I 6=Sweden 7=Italy 8=Spain 9=Japan 10=Norway 11=Denmark-II
IBM emulation : 0=International-1 1=International-2 2=Israel 3=Greece 4=Portugal 5=Spain

Rene Garcia 2023

