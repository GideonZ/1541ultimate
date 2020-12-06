logeb2	.byt $81,$38,$aa,$3b,$29
expcon	.byt $07,$71,$34,$58,$3e
	.byt $56,$74,$16,$7e
	.byt $b3,$1b,$77,$2f
	.byt $ee,$e3,$85,$7a
	.byt $1d,$84,$1c,$2a
	.byt $7c,$63,$59,$58
	.byt $0a,$7e,$75,$fd
	.byt $e7,$c6,$80,$31
	.byt $72,$18,$10,$81
	.byt 0,0,0,0
;
; start of kernal rom
;
exp	lda #<logeb2
	ldy #>logeb2
	jsr fmult
	lda facov
	adc #$50
	bcc stoldx
	jsr incrnd
stoldx	jmp stold       ;cross boundries

