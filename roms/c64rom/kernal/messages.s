	.segment "MESSAGES"
ms1	.byt $d,"I/O ERROR ",$a3
ms5	.byt $d,"SEARCHING",$a0
ms6	.byt "FOR",$a0
ms7	.byt $d,"PRESS PLAY ON TAP",$c5
ms8	.byt "PRESS RECORD & PLAY ON TAP",$c5
ms10	.byt $d,"LOADIN",$c7
ms11	.byt $d,"SAVING",$a0
ms21	.byt $d,"VERIFYIN",$c7
ms17	.byt $d,"FOUND",$a0
ms18	.byt $d,"OK",$8d
; ms34 .byt $d,"MONITOR",$8d
; ms36 .byt $d,"BREA",$cb

;print message to screen only if
;output enabled
;
spmsg	bit msgflg      ;printing messages?
	bpl msg10       ;no...
msg	lda ms1,y
	php
	and #$7f
	jsr bsout
	iny
	plp
	bpl msg
msg10	clc
	rts

