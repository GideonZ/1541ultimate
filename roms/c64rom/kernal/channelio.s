	.segment "CHANNEL"
;***************************************
;* getin -- get character from channel *
;*      channel is determined by dfltn.*
;* if device is 0, keyboard queue is   *
;* examined and a character removed if *
;* available.  if queue is empty, z    *
;* flag is returned set.  devices 1-31 *
;* advance to basin.                   *
;***************************************
;
ngetin	lda dfltn       ;check device
	bne gn10        ;not keyboard
;
	lda ndx         ;queue index
	beq gn20        ;nobody there...exit
;
	sei
	jmp lp2         ;go remove a character
;
gn10	cmp #2          ;is it rs-232
	bne bn10        ;no...use basin
;
gn232	sty xsav        ;save .y, used in rs232
	jsr bsi232
	ldy xsav        ;restore .y
gn20	clc             ;good return
	rts

;***************************************
;* basin-- input character from channel*
;*     input differs from get on device*
;* #0 function which is keyboard. the  *
;* screen editor makes ready an entire *
;* line which is passed char by char   *
;* up to the carriage return.  other   *
;* devices are:                        *
;*      0 -- keyboard                  *
;*      1 -- cassette #1               *
;*      2 -- rs232                     *
;*      3 -- screen                    *
;*   4-31 -- serial bus                *
;***************************************
;
nbasin	lda dfltn       ;check device
	bne bn10        ;is not keyboard...
;
;input from keyboard
;
	lda pntr        ;save current...
	sta lstp        ;... cursor column
	lda tblx        ;save current...
	sta lsxp        ;... line number
	jmp loop5       ;blink cursor until return
;
bn10	cmp #3          ;is input from screen?
	bne bn20        ;no...
;
	sta crsw        ;fake a carriage return
	lda lnmx        ;say we ended...
	sta indx        ;...up on this line
	jmp loop5       ;pick up characters
;
bn20	bcs bn30        ;devices >3
	cmp #2          ;rs232?
	beq bn50
;
;input from cassette buffers
;
	stx xsav
	jsr jtget
	bcs jtg37       ;stop key/error
	pha
	jsr jtget
	bcs jtg36       ;stop key/error
	bne jtg35       ;not an end of file
	lda #64         ;tell user eof
	jsr udst        ;in status
jtg35	dec bufpt
	ldx xsav        ;.x preserved
	pla             ;character returned
;c-clear from jtget
	rts             ;all done
;
jtg36	tax             ;save error info
	pla             ;toss data
	txa             ;restore error
jtg37	ldx xsav        ;return
	rts             ;error return c-set from jtget

;get a character from appropriate
;cassette buffer
;
jtget	jsr jtp20       ;buffer pointer wrap?
	bne jtg10       ;no...
	jsr rblk        ;yes...read next block
	bcs bn33        ;stop key pressed
	lda #0
	sta bufpt       ;point to begin.
	beq jtget       ;branch always
;
jtg10	lda (tape1),y    ;get char from buf
	clc             ;good return
	rts 

;input from serial bus
;
bn30	lda status      ;status from last
	beq bn35        ;was good
bn31	lda #$d         ;bad...all done
bn32	clc             ;valid data
bn33	rts
;
bn35	jmp acptr       ;good...handshake
;
;input from rs232
;
bn50	jsr gn232       ;get info
	bcs bn33        ;error return
	cmp #00
	bne bn32        ;good data...exit
	lda rsstat      ;check for dsr or dcd error
	and #$60
	bne bn31        ;an error...exit with c/r
	beq bn50        ;no error...stay in loop

;***************************************
;* bsout -- out character to channel   *
;*     determined by variable dflto:   *
;*     0 -- invalid                    *
;*     1 -- cassette #1                *
;*     2 -- rs232                      *
;*     3 -- screen                     *
;*  4-31 -- serial bus                 *
;***************************************
;
nbsout	pha             ;preserve .a
	lda dflto       ;check device
	cmp #3          ;is it the screen?
	bne bo10        ;no...
;
;print to crt
;
	pla             ;restore data
	jmp prt         ;print on crt
;
bo10
	bcc bo20        ;device 1 or 2
;
;print to serial bus
;
	pla
	jmp ciout
;
;print to cassette devices
;
bo20	lsr a           ;rs232?
	pla             ;get data off stack...
;
casout	sta t1          ;pass data in t1
; casout must be entered with carry set!!!
;preserve registers
;
	txa
	pha
	tya
	pha
	bcc bo50        ;c-clr means dflto=2 (rs232)
;
	jsr jtp20       ;check buffer pointer
	bne jtp10       ;has not reached end
	jsr wblk        ;write full buffer
	bcs rstor       ;abort on stop key
;
;put buffer type byte
;
	lda #bdf
	ldy #0
	sta (tape1),y
;
;reset buffer pointer
;
	iny             ;make .y=1
	sty bufpt       ;bufpt=1
;
jtp10	lda t1
	sta (tape1),y    ;data to buffer
;
;restore .x and .y
;
rstoa	clc             ;good return
rstor	pla
	tay
	pla
	tax
	lda t1          ;get .a for return
	bcc rstor1      ;no error
	lda #00         ;stop error if c-set
rstor1	rts
;
;output to rs232
;
bo50	jsr bso232      ;pass data through variable t1
	jmp rstoa       ;go restore all..always good

; rsr 5/12/82 fix bsout for no reg affect but errors
