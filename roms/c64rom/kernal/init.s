	.segment "INIT"
; start - system reset
; will goto rom at $8000...
; if locs $8004-$8008
; = 'cbm80'
;    ^^^  > these have msb set
; kernal expects...
; $8000- .word initilize (hard start)
; $8002- .word panic (warm start)
; ... else basic system used
; ******************testing only***************
; use auto disk/cassette load when developed...
;
start	ldx #$ff
	sei
	txs
	cld
	jsr a0int       ;test for $a0 rom in
	bne start1
	jmp ($8000)     ; go init as $a000 rom wants
start1	stx vicreg+22   ;set up refresh (.x=<5)
	jsr ioinit      ;go initilize i/o devices
	jsr ramtas      ;go ram test and set
	jsr restor      ;go set up os vectors
;
	jsr pcint       ;go initilize screen newxxx
	cli             ;interrupts okay now
	jmp ($a000)     ;go to basic system

; a0int - test for an $8000 rom
;  returns z - $8000 in
;
a0int	ldx #tbla0e-tbla0r ;check for $8000
a0in1	lda tbla0r-1,x
	cmp $8004-1,x
	bne a0in2
	dex
	bne a0in1
a0in2	rts
;
tbla0r	.byt $c3,$c2,$cd,"80" ;..cbm80..
tbla0e

; restor - set kernal indirects and vectors (system)
;
restor	ldx #<vectss
	ldy #>vectss
	clc
;
; vector - set kernal indirect and vectors (user)
;
vector	stx tmp2
	sty tmp2+1
	ldy #vectse-vectss-1
movos1	lda cinv,y      ;get from storage
	bcs movos2      ;c...want storage to user
	lda (tmp2),y     ;...want user to storage
movos2	sta (tmp2),y     ;put in user
	sta cinv,y      ;put in storage
	dey
	bpl movos1
	rts
;
vectss	.word key,timb,nnmi
	.word nopen,nclose,nchkin
	.word nckout,nclrch,nbasin
	.word nbsout,nstop,ngetin
	.word nclall,timb ;goto break on a usrcmd jmp
	.word nload,nsave
vectse

; ramtas - memory size check and set
;
ramtas	lda #0          ;zero low memory
	tay             ;start at 0002
ramtz0	sta $0002,y     ;zero page
	sta $0200,y     ;user buffers and vars
	sta $0300,y     ;system space and user space
	iny
	bne ramtz0
;
;allocate tape buffers
;
	ldx #<tbuffr
	ldy #>tbuffr
	stx tape1
	sty tape1+1
;
; set top of memory
;
ramtbt
	tay             ;move $00 to .y
	lda #3          ;set high inital index
	sta tmp0+1
;
ramtz1	inc tmp0+1      ;move index thru memory
ramtz2	lda (tmp0),y     ;get present data
	tax             ;save in .x
	lda #$55        ;do a $55,$aa test
	sta (tmp0),y
	cmp (tmp0),y
	bne size
	rol a
	sta (tmp0),y
	cmp (tmp0),y
	bne size
	txa             ;restore old data
	sta (tmp0),y
	iny
	bne ramtz2
	beq ramtz1
;
size	tya             ;set top of memory
	tax
	ldy tmp0+1
	clc
	jsr settop
	lda #$08        ;set bottom of memory
	sta memstr+1    ;always at $0800
	lda #$04        ;screen always at $400
	sta hibase      ;set base of screen
	rts

bsit	.word wrtz,wrtn,key,read ;table of indirects for cassette irq's

; ioinit - initilize io devices
;
ioinit	lda #$7f        ;kill interrupts
	sta d1icr
	sta d2icr
	sta d1pra       ;turn on stop key
	lda #%00001000  ;shut off timers
	sta d1cra
	sta d2cra
	sta d1crb
	sta d2crb
; configure ports
	ldx #$00        ;set up keyboard inputs
	stx d1ddrb      ;keyboard inputs
	stx d2ddrb      ;user port (no rs-232)
	stx sidreg+24   ;turn off sid
	dex
	stx d1ddra      ;keyboard outputs
	lda #%00000111  ;set serial/va14/15 (clkhi)
	sta d2pra
	lda #%00111111  ;set serial in/out, va14/15out
	sta d2ddra
;
; set up the 6510 lines
;
	lda #%11100111  ;motor on, hiram lowram charen high
	sta r6510
	lda #%00101111  ;mtr out,sw in,wr out,control out
	sta d6510
;
;jsr clkhi ;clkhi to release serial devices  ^
;
iokeys	lda palnts      ;pal or ntsc
	beq i0010	;ntsc
	lda #<sixtyp
	sta d1t1l
	lda #>sixtyp
	jmp i0020
i0010	lda #<sixty     ;keyboard scan irq's
	sta d1t1l
	lda #>sixty
i0020	sta d1t1h
	jmp piokey
; lda #$81 ;enable t1 irq's
; sta d1icr
; lda d1cra
; and #$80 ;save only tod bit
; ora #%00010001 ;enable timer1
; sta d1cra
; jmp clklo ;release the clock line
;
; sixty hertz value
;
sixty	= 17045         ; ntsc
sixtyp	= 16421         ; pal

setnam	sta fnlen
	stx fnadr
	sty fnadr+1
	rts

setlfs	sta la
	stx fa
	sty sa
	rts

readss	lda fa          ;see which devices' to read
	cmp #2          ;is it rs-232?
	bne readst      ;no...read serial/cass
	lda rsstat      ;yes...get rs-232 up
	pha
	lda #00         ;clear rs232 status when read
	sta rsstat
	pla
	rts
setmsg	sta msgflg
readst	lda status
udst	ora status
	sta status
	rts

settmo	sta timout
	rts

memtop	bcc settop
;
;carry set--read top of memory
;
gettop	ldx memsiz
	ldy memsiz+1
;
;carry clear--set top of memory
;
settop	stx memsiz
	sty memsiz+1
	rts

;manage bottom of memory
;
membot	bcc setbot
;
;carry set--read bottom of memory
;
	ldx memstr
	ldy memstr+1
;
;carry clear--set bottom of memory
;
setbot	stx memstr
	sty memstr+1
	rts

; rsr 8/5/80 change io structure
; rsr 8/15/80 add memory test
; rsr 8/21/80 change i/o for mod
; rsr 8/25/80 change i/o for mod2
; rsr 8/29/80 change ramtest for hardware mistake
; rsr 9/22/80 change so ram hang rs232 status read
; rsr 5/12/82 change start1 order to remove disk problem
