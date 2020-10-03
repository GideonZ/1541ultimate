; rsrcvr - nmi routine to collect
;  data into bytes
;
; rsr 8/18/80
;
; variables used
;   inbit - input bit value
;   bitci - bit count in
;   rinone - flag for start bit check <>0 start bit
;   ridata - byte input buffer
;   riprty - holds byte input parity
;   ribuf - indirect pointer to data buffer
;   ridbe - input buffer index to end
;   ridbs - input buffer pointer to start
;   if ridbe=ridbs then input buffer empty
;
rsrcvr	ldx rinone      ;check for start bit
	bne rsrtrt      ;was start bit
;
	dec bitci       ;check where we are in input...
	beq rsr030      ;have a full byte
	bmi rsr020      ;getting stop bits
;
; calc parity
;
	lda inbit       ;get data up
	eor riprty      ;calc new parity
	sta riprty
;
; shift data bit in
;
	lsr inbit       ;in bit pos 0
	ror ridata      ;c into data
;
; exit
;
rsrext	rts

; have stop bit, so store in buffer
;
rsr018	dec bitci       ;no parity, dec so check works
rsr020	lda inbit       ;get data...
	beq rsr060      ;...zero, an error?
;
	lda m51ctr      ;check for correct # of stop bits
	asl a           ;carry tell how may stop bits
	lda #01
	adc bitci
	bne rsrext      ;no..exit
;
; rsrabl - enable to recieve a byte
;
rsrabl	lda #$90        ;enable flag for next byte
	sta d2icr       ;toss bad/old nmi
	ora enabl       ;mark in enable register***********
	sta enabl       ;re-enabled by jmp oenabl
	sta rinone      ;flag for start bit
;
rsrsxt	lda #$02        ;disable t2
	jmp oenabl      ;flip-off enabl***************

; reciever start bit check
;
rsrtrt	lda inbit       ;check if space
	bne rsrabl      ;bad...try again
	jmp prtyp       ;go to parity patch 901227-03
; sta rinone ;good...disable flag
; rts ;and exit

;
; put data in buffer (at parity time)
;
rsr030	ldy ridbe       ;get end
	iny
	cpy ridbs       ;have we passed start?
	beq recerr      ;yes...error
;
	sty ridbe       ;move ridbe foward
	dey
;
	lda ridata      ;get byte buffer up
	ldx bitnum      ;shift untill full byte
rsr031	cpx #9          ;always 8 bits
	beq rsr032
	lsr a           ;fill with zeros
	inx
	bne rsr031
;
rsr032	sta (ribuf),y    ;data to page buffer
;
; parity checking
;
	lda #$20        ;check 6551 command register
	bit m51cdr
	beq rsr018      ;no parity bit so stop bit
	bmi rsrext      ;no parity check
;
; check calc parity
;
	lda inbit
	eor riprty      ;put in with parity
	beq rsr050      ;even parity
	bvs rsrext      ;odd...okay so exit
	.byt $2c        ;skip two
rsr050	bvc rsrext      ;even...okay so exit
;
; errors reported
	lda #1          ;parity error
	.byt $2c
recerr	lda #$4         ;reciever overrun
	.byt $2c
breake	lda #$80        ;break detected
	.byt $2c
framee	lda #$02        ;frame error
err232	ora rsstat
	sta rsstat
	jmp rsrabl      ;bad exit so hang ##????????##
;
; check for errors
;
rsr060	lda ridata      ;expecting stop...
	bne framee      ;frame error
	beq breake      ;could be a break

; rsr -  8/21/80 add mods
; rsr -  8/24/80 fix errors
; rsr -  8/27/80 fix major errors
; rsr -  8/30/80 fix t2 adjust
; rsr - 12/11/81 modify for vic-40 i/o
; rsr -  3/11/82 fix for bad/old nmi's
