	.segment "KPATCH"

	; unused patch area
	.res 28, $aa

; prtyp - rs232 parity patch...added 901227-03
;
prtyp	sta rinone      ;good receiver start...disable flag
	lda #1          ;set parity to 1 always
	sta riprty
	rts

; cpatch - fix to clear line...modified 901227-03
;  prevents white character flash...
cpatch                  ;always clear to current foregnd color
	lda color
	sta (user),y
	rts

; fpatch - tape filename timeout
;
fpatch	adc #2          ;time is (8 to 13 sec of display)
fpat00	ldy stkey       ;check for key down on last row...
	iny
	bne fpat01      ;key...exit loop
	cmp time+1      ;watch timer
	bne fpat00
fpat01	rts

;
; baudop - baud rate table for pal
;   .985248e6/baud-rate/2-100
;
baudop	.word 9853-cbit ;50 baud
	.word 6568-cbit ;75 baud
	.word 4478-cbit ;110 baus
	.word 3660-cbit ;134.6 baud
	.word 3284-cbit ;150 baud
	.word 1642-cbit ;300 baud
	.word 821-cbit  ;600 baud
	.word 411-cbit  ;1200 baud
	.word 274-cbit  ;1800 baud
	.word 205-cbit  ;2400 baud
