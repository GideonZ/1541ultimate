;most references to kernal are defined here
;
erexit	cmp #$f0        ;check for special case
	bne erexix
; top of memory has changed
	sty memsiz+1
	stx memsiz
	jmp cleart      ;act as if he typed clear
erexix	tax             ;set termination flags
	bne erexiy
	ldx #erbrk      ;break error
erexiy	jmp error       ;normal error

clschn	=$ffcc

outch	jsr $ffd2
	bcs erexit
	rts

inchr	jsr $ffcf
	bcs erexit
	rts

ccall	=$ffe7

settim	=$ffdb
rdtim	=$ffde

coout	jsr ppach       ; go out to save .a for print# patch
	bcs erexit
	rts

coin	jsr $ffc6
	bcs erexit
	rts

readst	=$ffb7

cgetl	jsr $ffe4
	bcs erexit
	rts

rdbas	=$fff3

setmsg	=$ff90

plot	=$fff0

csys	jsr frmnum      ;eval formula
	jsr getadr      ;convert to int. addr
	lda #>csysrz    ;push return address
	pha
	lda #<csysrz
	pha
	lda spreg       ;status reg
	pha
	lda sareg       ;load 6502 regs
	ldx sxreg
	ldy syreg
	plp             ;load 6502 status reg
	jmp (linnum)    ;go do it
csysrz	=*-1            ;return to here
	php             ;save status reg
	sta sareg       ;save 6502 regs
	stx sxreg
	sty syreg
	pla             ;get status reg
	sta spreg
	rts             ;return to system

csave	jsr plsv        ;parse parms
	ldx vartab      ;end save addr
	ldy vartab+1
	lda #<txttab    ;indirect with start address
	jsr $ffd8       ;save it
	bcs erexit
	rts

cverf	lda #1          ;verify flag
	.byt $2c        ;skip two bytes

cload	lda #0          ;load flag
	sta verck
	jsr plsv        ;parse parameters
;
cld10	; jsr $ffe1 ;check run/stop
; cmp #$ff ;done yet?
; bne cld10 ;still bouncing
	lda verck
	ldx txttab      ;.x and .y have alt...
	ldy txttab+1    ;...load address
	jsr $ffd5       ;load it
	bcs jerxit      ;problems
;
	lda verck
	beq cld50       ;was load
;
;finish verify
;
	ldx #ervfy      ;assume error
	jsr $ffb7       ;read status
	and #$10        ;check error
	bne cld55       ;replaces beq *+5/jmp error
;
;print verify 'ok' if direct
;
	lda txtptr
	cmp #bufpag
	beq cld20
	lda #<okmsg
	ldy #>okmsg
	jmp strout
;
cld20	rts

;
;finish load
;
cld50	jsr $ffb7       ;read status
	and #$ff-$40    ;clear e.o.i.
	beq cld60       ;was o.k.
	ldx #erload
cld55	jmp error
;
cld60	lda txtptr+1
	cmp #bufpag     ;direct?
	bne cld70       ;no...
;
	stx vartab
	sty vartab+1    ;end load address
	lda #<reddy
	ldy #>reddy
	jsr strout
	jmp fini
;
;program load
;
cld70	jsr stxtpt
	jsr lnkprg
	jmp fload

copen	jsr paoc        ;parse statement
	jsr $ffc0       ;open it
	bcs jerxit      ;bad stuff or memsiz change
	rts             ;a.o.k.

cclos	jsr paoc        ;parse statement
	lda andmsk      ;get la
	jsr $ffc3       ;close it
	bcc cld20       ;it's okay...no memsize change
;
jerxit	jmp erexit

;
;parse load and save commands
;
plsv
;default file name
;
	lda #0          ;length=0
	jsr $ffbd
;
;default device #
;
	ldx #1          ;device #1
	ldy #0          ;command 0
	jsr $ffba
;
	jsr paoc20      ;by-pass junk
	jsr paoc15      ;get/set file name
	jsr paoc20      ;by-pass junk
	jsr plsv7       ;get ',fa'
	ldy #0          ;command 0
	stx andmsk
	jsr $ffba
	jsr paoc20      ;by-pass junk
	jsr plsv7       ;get ',sa'
	txa             ;new command
	tay
	ldx andmsk      ;device #
	jmp $ffba

;look for comma followed by byte
plsv7	jsr paoc30
	jmp getbyt

;skip return if next char is end
;
paoc20	jsr chrgot
	bne paocx
	pla
	pla
paocx	rts

;check for comma and good stuff
;
paoc30	jsr chkcom      ;check comma
paoc32	jsr chrgot      ;get current
	bne paocx       ;is o.k.
paoc40	jmp snerr       ;bad...end of line

;parse open/close
;
paoc	lda #0
	jsr $ffbd       ;default file name
;
	jsr paoc32      ;must got something
	jsr getbyt      ;get la
	stx andmsk
	txa
	ldx #1          ;default device
	ldy #0          ;default command
	jsr $ffba       ;store it
	jsr paoc20      ;skip junk
	jsr plsv7
	stx eormsk
	ldy #0          ;default command
	lda andmsk      ;get la
	cpx #3
	bcc paoc5
	dey             ;default ieee to $ff
paoc5	jsr $ffba       ;store them
	jsr paoc20      ;skip junk
	jsr plsv7       ;get sa
	txa
	tay
	ldx eormsk
	lda andmsk
	jsr $ffba       ;set up real eveything
paoc7	jsr paoc20
	jsr paoc30
paoc15	jsr frmevl
	jsr frestr      ;length in .a
	ldx index1
	ldy index1+1
	jmp $ffbd

; rsr 8/10/80 - change sys command
; rsr 8/26/80 - add open&close memsiz detect
; rsr 10/7/80 - change load (remove run wait)
; rsr 4/10/82 - inline fix program load
; rsr 7/02/82 - fix print# problem
