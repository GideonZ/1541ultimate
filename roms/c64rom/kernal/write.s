; cassette info - fsblk is block counter for record
;       fsblk = 2 -first header
;             = 1 -first data
;             = 0 -second data
;
; write - toggle write bit according to lsb in ochar
;
write	lda ochar       ;shift bit to write into carry
	lsr a
	lda #96         ;...c clr write short
	bcc wrt1
wrtw	lda #176        ;...c set write long
wrt1	ldx #0          ;set and store time
wrtx	sta d1t2l
	stx d1t2h
	lda d1icr       ;clear irq
	lda #$19        ;enable timer (one-shot)
	sta d1crb
	lda r6510       ;toggle write bit
	eor #$08
	sta r6510
	and #$08        ;leave only write bit
	rts
;
wrtl3	sec             ;flag prp for end of block
	ror prp
	bmi wrt3        ; jmp
;
; wrtn - called at the end of each byte
;   to write a long rer    rez
;              hhhhhhllllllhhhlll...
;
wrtn	lda rer         ;check for one long
	bne wrtn1
	lda #16         ;write a long bit
	ldx #1
	jsr wrtx
	bne wrt3
	inc rer
	lda prp         ;if end of block(bit set by wrtl3)...
	bpl wrt3        ;...no end continue
	jmp wrnc        ;...end ...finish off
;
wrtn1	lda rez         ;check for a one bit
	bne wrtn2
	jsr wrtw
	bne wrt3
	inc rez
	bne wrt3
;
wrtn2	jsr write
	bne wrt3        ;on bit low exit
	lda firt        ;check for first of dipole
	eor #1
	sta firt
	beq wrt2        ;dipole done
	lda ochar       ;flips bit for complementary right
	eor #1
	sta ochar
	and #1          ;toggle parity
	eor prty
	sta prty
wrt3	jmp prend       ;restore regs and rti exit
;
wrt2	lsr ochar       ;move to next bit
	dec pcntr       ;dec counter for # of bits
	lda pcntr       ;check for 8 bits sent...
	beq wrt4        ;...if yes move in parity
	bpl wrt3        ;...else send rest
;
wrts	jsr newch       ;clean up counters
	cli             ;allow for interrupts to nest
	lda cntdn       ;are we writing header counters?...
	beq wrt6        ;...no
; write header counters (9876543210 to help with read)
	ldx #0          ;clear bcc
	stx data
wrts1	dec cntdn
	ldx fsblk       ;check for first block header
	cpx #2
	bne wrt61       ;...no
	ora #$80        ;...yes mark first block header
wrt61	sta ochar       ;write characters in header
	bne wrt3
;
wrt6	jsr cmpste      ;compare start:end
	bcc wrt7        ;not done
	bne wrtl3       ;go mark end
	inc sah
	lda data        ;write out bcc
	sta ochar
	bcs wrt3        ;jmp
;
wrt7	ldy #0          ;get next character
	lda (sal),y
	sta ochar       ;store in output character
	eor data        ;update bcc
	sta data
	jsr incsal      ;increment fetch address
	bne wrt3        ;branch always
;
wrt4	lda prty        ;move parity into ochar...
	eor #1
	sta ochar       ;...to be written as next bit
wrtbk	jmp prend       ;restore regs and rti exit
;
wrnc	dec fsblk       ;check for end
	bne wrend       ;...block only
	jsr tnof        ;...write, so turn off motor
wrend	lda #80         ;put 80 cassette syncs at end
	sta shcnl
	ldx #8
	sei
	jsr bsiv        ;set vector to write zeros
	bne wrtbk       ;jmp
;
wrtz	lda #120        ;write leading zeros for sync
	jsr wrt1
	bne wrtbk
	dec shcnl       ;check if done with low sync...
	bne wrtbk       ;...no
	jsr newch       ;...yes clear up counters
	dec shcnh       ;check if done with sync...
	bpl wrtbk       ;...no
	ldx #10         ;...yes so set vector for data
	jsr bsiv
	cli
	inc shcnh       ;zero shcnh
	lda fsblk       ;if done then...
	beq stky        ;...goto system restore
	jsr rd300
	ldx #9          ;set up for header count
	stx cntdn
	stx prp         ;clear endof block flag
	bne wrts        ;jmp
;
tnif	php             ;clean up interrupts and restore pia's
	sei
	lda vicreg+17   ;unlock vic
	ora #$10        ;enable display
	sta vicreg+17
	jsr tnof        ;turn off motor
	lda #$7f        ;clear interrupts
	sta d1icr
	jsr iokeys      ;restore keyboard irq from timmer1
	lda irqtmp+1    ;restore keyboard interrupt vector
	beq tniq        ;no irq (irq vector cannot be z-page)
	sta cinv+1
	lda irqtmp
	sta cinv
tniq	plp
	rts
;
stky	jsr tnif        ;go restore system interrupts
	beq wrtbk       ;came for cassette irq so rti
;
; bsiv - subroutine to change irq vectors
;  entrys - .x = 8 write zeros to tape
;           .x = 10 write data to tape
;           .x = 12 restore to keyscan
;           .x = 14 read data from tape
;
bsiv	lda bsit-8,x    ;move irq vectors, table to indirect
	sta cinv
	lda bsit+1-8,x
	sta cinv+1
	rts
;
tnof	lda r6510       ;turn off cassette motor
	ora #$20        ;
	sta r6510
	rts

;compare start and end load/save
;addresses.  subroutine called by
;tape read, save, tape write
;
cmpste	sec
	lda sal
	sbc eal
	lda sah
	sbc eah
	rts

;increment address pointer sal
;
incsal	inc sal
	bne incr
	inc sah
incr	rts

; rsr 7/28/80 add comments
; rsr 8/4/80 changed i/o for vixen
; rsr 8/21/80 changed i/o for vixen mod
; rsr 8/25/80 changed i/o for vixen mod2
; rsr 12/11/81 modify i/o for vic-40
; rsr 2/9/82 add vic turn on, replace sah with prp
