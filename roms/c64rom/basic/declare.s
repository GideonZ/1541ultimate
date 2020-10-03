addprc	=1
romloc	=$a000          ;vic-40 basic rom
linlen	=40             ;vic screen size ?why?
buflen	=89             ;vic buffer
bufpag	=2
buf	=512
stkend	=507
clmwid	=10             ;print window 10 chars
pi	=255
numlev	=23
strsiz	=3
.segment "ZPBASIC" : zeropage
blank0	.res 3           ;6510 register area
adray1	.res 2           ;convert float->integer
adray2	.res 2           ;convert integer->float
integr
charac	.res 1
endchr	.res 1
trmpos	.res 1
verck	.res 1
count	.res 1
dimflg	.res 1
valtyp	.res 1
intflg	.res 1
garbfl
dores	.res 1
subflg	.res 1
inpflg	.res 1
domask
tansgn	.res 1
channl	.res 1
poker
linnum	.res 2
temppt	.res 1
lastpt	.res 2
tempst	.res 9
index
index1	.res 2
index2	.res 2
resho	.res 1
resmoh	.res 1
addend
resmo	.res 1
reslo	.res 1
	.res 1
txttab	.res 2
vartab	.res 2
arytab	.res 2
strend	.res 2
fretop	.res 2
frespc	.res 2
memsiz	.res 2
curlin	.res 2
oldlin	.res 2
oldtxt	.res 2
datlin	.res 2
datptr	.res 2
inpptr	.res 2
varnam	.res 2
fdecpt	
varpnt	.res 2
lstpnt
andmsk
forpnt	.res 2
eormsk	=forpnt+1
vartxt
opptr	.res 2
opmask	.res 1
grbpnt
tempf3
defpnt	.res 2
dscpnt	.res 2
	.res 1
four6	.res 1
jmper	.res 1
size	.res 1
oldov	.res 1
tempf1	.res 1
arypnt
highds	.res 2
hightr	.res 2
tempf2
	.res 1
deccnt
lowds	.res 2
grbtop
dptflg
lowtr	.res 1
expsgn	.res 1
tenexp	=lowds+1
epsgn	=lowtr+1
dsctmp
fac
facexp	.res 1
facho	.res 1
facmoh	.res 1
indice
facmo	.res 1
faclo	.res 1
facsgn	.res 1
degree
sgnflg	.res 1
bits	.res 1
argexp	.res 1
argho	.res 1
argmoh	.res 1
argmo	.res 1
arglo	.res 1
argsgn	.res 1
strngi
arisgn	.res 1
facov	.res 1
bufptr
strng2
polypt
curtol
fbufpt	.res 2
chrget	.res 6
chrgot	.res 1
txtptr	.res 6
qnum	.res 10
chrrts	.res 1
rndx	.res 5

	.segment "STRTMP" : zeropage
lofbuf	.res 1
fbuffr	.res 1
strng1	=arisgn
;
	.segment "BVECTORS" ;basic indirects
ierror	.res 2           ;indirect error (output error in .x)
imain	.res 2           ;indirect main (system direct loop)
icrnch	.res 2           ;indirect crunch (tokenization routine)
iqplop	.res 2           ;indirect list (char list)
igone	.res 2           ;indirect gone (char dispatch)
ieval	.res 2           ;indirect eval (symbol evaluation)
;temp storage untill system intergration
; sys 6502 regs
sareg	.res 1           ;.a reg
sxreg	.res 1           ;.x reg
syreg	.res 1           ;.y reg
spreg	.res 1           ;.p reg
usrpok	.res 3           ;user function dispatch

