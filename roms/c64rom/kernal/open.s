;***********************************
;*                                 *
;* open function                   *
;*                                 *
;* creates an entry in the logical *
;* files tables consisting of      *
;* logical file number--la, device *
;* number--fa, and secondary cmd-- *
;* sa.                             *
;*                                 *
;* a file name descriptor, fnadr & *
;* fnlen are passed to this routine*
;*                                 *
;***********************************
;
nopen	ldx la          ;check file #
	bne op98        ;is not the keyboard
;
	jmp error6      ;not input file...
;
op98	jsr lookup      ;see if in table
	bne op100       ;not found...o.k.
;
	jmp error2      ;file open
;
op100	ldx ldtnd       ;logical device table end
	cpx #10         ;maximum # of open files
	bcc op110       ;less than 10...o.k.
;
	jmp error1      ;too many files
;
op110	inc ldtnd       ;new file
	lda la
	sta lat,x       ;store logical file #
	lda sa
	ora #$60        ;make sa an serial command
	sta sa
	sta sat,x       ;store command #
	lda fa
	sta fat,x       ;store device #
;
;perform device specific open tasks
;
	beq op175       ;is keyboard...done.
	cmp #3
	beq op175       ;is screen...done.
	bcc op150       ;are cassettes 1 & 2
;
	jsr openi       ;is on serial...open it
	bcc op175       ;branch always...done
;
;perform tape open stuff
;
op150	cmp #2
	bne op152
;
	jmp opn232
;
op152	jsr zzz         ;see if tape buffer
	bcs op155       ;yes
;
	jmp error9      ;no...deallocated
;
op155	lda sa
	and #$f         ;mask off command
	bne op200       ;non zero is tape write
;
;open cassete tape file to read
;
	jsr cste1       ;tell "press play"
	bcs op180       ;stop key pressed
;
	jsr luking      ;tell user "searching"
;
	lda fnlen
	beq op170       ;looking for any file
;
	jsr faf         ;looking for named file
	bcc op171       ;found it!!!
	beq op180       ;stop key pressed
;
op160	jmp error4      ;file not found
;
op170	jsr fah         ;get any old header
	beq op180       ;stop key pressed
	bcc op171       ;all o.k.
	bcs op160       ;file not found...
;
;open cassette tape for write
;
op200	jsr cste2       ;tell "press play and record"
	bcs op180       ;stop key pressed
	lda #bdfh       ;data file header type
	jsr tapeh       ;write it
;
;finish open for tape read/write
;
op171	lda #bufsz-1    ;assume force read
;
	ldy sa
	cpy #$60        ;open for read?
	beq op172
;
;set pointers for buffering data
;
	ldy #0
	lda #bdf        ;type flag for block
	sta (tape1),y    ;to begin of buffer
	tya
;
op172	sta bufpt       ;point to data
op175	clc             ;flag good open
op180	rts             ;exit in peace

openi	lda sa
	bmi op175       ;no sa...done
;
	ldy fnlen
	beq op175       ;no file name...done
;
	lda #0          ;clear the serial status
	sta status
;
	lda fa
	jsr listn       ;device la to listen
;
	lda sa
	ora #$f0
	jsr secnd
;
	lda status      ;anybody home?
	bpl op35        ;yes...continue
;
;this routine is called by other
;kernal routines which are called
;directly by os.  kill return
;address to return to os.
;
	pla
	pla
	jmp error5      ;device not present
;
op35	lda fnlen
	beq op45        ;no name...done sequence
;
;send file name over serial
;
	ldy #0
op40	lda (fnadr),y
	jsr ciout
	iny
	cpy fnlen
	bne op40
;
op45	jmp cunlsn      ;jsr unlsn: clc: rts

; opn232 - open an rs-232 or parallel port file
;
; variables initilized
;   bitnum - # of bits to be sent calc from m51ctr
;   baudof - baud rate full
;   rsstat - rs-232 status reg
;   m51ctr - 6551 control reg
;   m51cdr - 6551 command reg
;   m51ajb - user baud rate (clock/baud/2-100)
;   enabl  - 6526 nmi enables (1-nmi bit on)
;
opn232	jsr cln232      ;set up rs232, .y=0 on return
;
; pass prams to m51regs
;
	sty rsstat      ;clear status
;
opn020	cpy fnlen       ;check if at end of filename
	beq opn025      ;yes...
;
	lda (fnadr),y    ;move data
	sta m51ctr,y    ;to m51regs
	iny
	cpy #4          ;only 4 possible prams
	bne opn020
;
; calc # of bits
;
opn025	jsr bitcnt
	stx bitnum
;
; calc baud rate
;
	lda m51ctr
	and #$0f
	beq opn028
;
; calculate start-test rate...
;  different than original release 901227-01
;
	asl a           ;get offset into tables
	tax
	lda palnts      ;get tv standard
	bne opn026
	ldy baudo-1,x   ;ntsc standard
	lda baudo-2,x
	jmp opn027
;
opn026	ldy baudop-1,x  ;pal standard
	lda baudop-2,x
opn027	sty m51ajb+1    ;hold start rate in m51ajb
	sta m51ajb
opn028	lda m51ajb      ;calculate baud rate
	asl
	jsr popen       ;goto patch area
;
; check for 3/x line response
;
opn030	lda m51cdr      ;bit 0 of m51cdr
	lsr a
	bcc opn050      ;...3 line
;
; check for x line proper states
;
	lda d2prb
	asl a
	bcs opn050
	jsr ckdsrx      ;change from jmp to prevent system 
;
; set up buffer pointers (dbe=dbs)
;
opn050	lda ridbe
	sta ridbs
	lda rodbe
	sta rodbs
;
; allocate buffers
;
opn055	jsr gettop      ;get memsiz
	lda ribuf+1     ;in allocation...
	bne opn060      ;already
	dey             ;there goes 256 bytes
	sty ribuf+1
	stx ribuf
opn060	lda robuf+1     ;out allocation...
	bne memtcf      ;alreay
	dey             ;there goes 256 bytes
	sty robuf+1
	stx robuf
memtcf	sec             ;signal top of memory change
	lda #$f0
	jmp settop      ;top changed
;
; cln232 - clean up 232 system for open/close
;  set up ddrb and cb2 for rs-232
;
cln232	lda #$7f        ;clear nmi's
	sta d2icr
	lda #%00000110  ;ddrb
	sta d2ddrb
	sta d2prb       ;dtr,rts high
	lda #$04        ;output high pa2
	ora d2pra
	sta d2pra
	ldy #00
	sty enabl       ;clear enabls
	rts

; rsr  8/25/80 - add rs-232 code
; rsr  8/26/80 - top of memory handler
; rsr  8/29/80 - add filename to m51regs
; rsr  9/02/80 - fix ordering of rs-232 routines
; rsr 12/11/81 - modify for vic-40 i/o
; rsr  2/08/82 - clear status in openi
; rsr  5/12/82 - compact rs232 open/close code
