	.segment "IRQFILE"
; simirq - simulate an irq (for cassette read)
;  enter by a jsr simirq
;
simirq	php
	pla             ;fix the break flag
	and #$ef
	pha
; puls - checks for real irq's or breaks
;
puls	pha
	txa
	pha
	tya
	pha
	tsx
	lda $104,x      ;get old p status
	and #$10        ;break flag?
	beq puls1       ;...no
	jmp (cbinv)     ;...yes...break instr
puls1	jmp (cinv)      ;...irq

; pcint - add universal to cinit
;
pcint	jsr cint
p0010	lda vicreg+18   ;check raster compare for zero
	bne p0010       ;if it's zero then check value
	lda vicreg+25   ;get raster irq value
	and #$01
	sta palnts      ;place in pal/ntsc indicator
	jmp iokeys
;
; piokey - add universal to iokeys
;
piokey	lda #$81        ;enable t1 irq's
	sta d1icr
	lda d1cra
	and #$80        ;save only tod bit
	ora #%00010001  ;enable timer1
	sta d1cra
	jmp clklo       ;release the clock line***901227-03***
