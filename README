1541 Ultimate II Remote Tool
============================

Syntax: 1541u2.pl <c64-ip-addr> <scriptfile>
resp. 1541u2.exe <c64-ip-addr> <scriptfile>

1541u2.pl <ip> -c run:<filename>
1541u2.pl <ip> -c kernal:<filename>
1541u2.pl <ip> -e "<command>" [-e "<command>" ...]
1541u2.pl <ip> -c d64:<filename>
1541u2.pl <ip> -c rund64:<filename>
 
Commands to be used in the script file:

* reset-c64

Resets the C64

* wait <number>

Waits some time on the C64

* keys <valuelist>

Fill keyboard buffer

* load [run] [reset] [at <number>] [at reu <number>] [at kernal <number>] [sizeprefix] [from prg 'filename'] [from bin 'filename'] [fill <count> <value>] [values <valuelist>] 

load data into C64 Memory / the REU / the Kernal ROM. Either by startaddress from prg file or given with the at-Clause.
Optionally the size can be prefixed to the data (4 byte).

* insert-disk [run] d64 'filename'

inserts a disk into the 1st floppy drive. Currently only d64 is supported.

* send

sends the actual packet to the C64.

* large-send

sends the actual packet to the C64. In contrast to "send", this packet can be 200000 bytes and requires large packet support in the 1541 Ultimate II.

* reu-load-split [at <number>] [sizeprefix] [from prg 'filename'] [from bin 'filename'] [fill <count> <value>] [values <valuelist>]

Automatically splits REU content to packets and sends them.



Note: Order of options does not matter with one exception: The values-Clause has to be the last option.


<valuelist> is a space separated list of the following elements: 

* Decimal number

* A decimal number postfixed by a small 'k' meaning 1024 times the value given

* A decimal number postfixed by a capital 'M' meaning 1024*1024 times the value given

* Hexadecimal number prefixed by a dollar sign

* a String enclosed in quotation marks

* C="C64 string" - like above, but translated to C64 charset.





Examples
========

reset-c64
send

load run from prg 'duplicator3.prg'
send

load at $d020 values 15
send

load reset from prg 'duplicator3.prg'
keys C="run" 13
send


load at reu 0 from prg 'duplicator3.prg'
send

reu-load-split at 0 from prg 'duplicator3.prg' sizeprefix



load at $0000 fill 52k 0
load at $e000 fill 8k 0
reset-c64
send

