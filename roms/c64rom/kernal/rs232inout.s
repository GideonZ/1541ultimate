; output a file over usr port
;  using rs232
;
cko232	sta dflto       ;set default out
	lda m51cdr      ;check for 3/x line
	lsr a
	bcc cko100      ;3line...no turn around
;
;*turn around logic
;
; check for dsr and rts
;
	lda #$02        ;bit rts is on
	bit d2prb
	bpl ckdsrx      ;no dsr...error
	bne cko100      ;rts...outputing or full duplex
;
; check for active input
;  rts will be low if currently inputing
;
cko020	lda enabl
	and #$02        ;look at ier for t2
	bne cko020      ;hang untill input done
;
; wait for cts to be off as spec reqs
;
cko030	bit d2prb
	bvs cko030
;
; turn on rts
;
	lda d2prb
	ora #$02
	sta d2prb
;
; wait for cts to go on
;
cko040	bit d2prb
	bvs cko100      ;done...
	bmi cko040      ;we still have dsr
;
ckdsrx	lda #$40        ;a data set ready error
	sta rsstat      ;major error....will require reopen
;
cko100	clc             ;no error
	rts
;

; bso232 - output a char rs232
;   data passed in t1 from bsout
;
; hang loop for buffer full
;
bsobad	jsr bso100      ;keep trying to start system...
;
; buffer handler
;
bso232	ldy rodbe
	iny
	cpy rodbs       ;check for buffer full
	beq bsobad      ;hang if so...trying to restart
	sty rodbe       ;indicate new start
	dey
	lda t1          ;get data...
	sta (robuf),y    ;store data
;
; set up if necessary to output
;
bso100	lda enabl       ;check for a t1 nmi enable
	lsr a           ;bit 0
	bcs bso120      ;running....so exit
;
; set up t1 nmi's
;
bso110	lda #$10        ;turn off timer to prevent false start...
	sta d2cra
	lda baudof      ;set up timer1
	sta d2t1l
	lda baudof+1
	sta d2t1h
	lda #$81
	jsr oenabl
	jsr rstbgn      ;set up to send (will stop on cts or dsr error)
	lda #$11        ;turn on timer
	sta d2cra
bso120	rts

; input a file over user port
;  using rs232
;
cki232	sta dfltn       ;set default input
;
	lda m51cdr      ;check for 3/x line
	lsr a
	bcc cki100      ;3 line...no handshake
;
	and #$08        ;full/half check (byte shifted above)
	beq cki100      ;full...no handshake
;
;*turn around logic
;
; check if dsr and not rts
;
	lda #$02        ;bit rts is on
	bit d2prb
	bpl ckdsrx      ;no dsr...error
	beq cki110      ;rts low...in correct mode
;
; wait for active output to be done
;
cki010	lda enabl
	lsr a           ;check t1 (bit 0)
	bcs cki010
;
; turn off rts
;
	lda d2prb
	and #$ff-02
	sta d2prb
;
; wait for dcd to go high (in spec)
;
cki020	lda d2prb
	and #$04
	beq cki020
;
; enable flag for rs232 input
;
cki080	lda #$90
	clc             ;no error
	jmp oenabl      ;flag in enabl**********
;
; if not 3 line half then...
;  see if we need to turn on flag
;
cki100	lda enabl       ;check for flag or t2 active
	and #$12
	beq cki080      ;no need to turn on
cki110	clc             ;no error
	rts

; bsi232 - input a char rs232
;
; buffer handler
;
bsi232	lda rsstat      ;get status up to change...
	ldy ridbs       ;get last byte address
	cpy ridbe       ;see if buffer empty
	beq bsi010      ;return a null if no char
;
	and #$ff-$08    ;clear buffer empty status
	sta rsstat
	lda (ribuf),y    ;get last char
	inc ridbs       ;inc to next pos
;
; receiver always runs
;
	rts
;
bsi010	ora #$08        ;set buffer empty status
	sta rsstat
	lda #$0         ;return a null
	rts

; rsp232 - protect serial/cass from rs232 nmi's
;
rsp232	pha             ;save .a
	lda enabl       ;does rs232 have any enables?
	beq rspok       ;no...
rspoff	lda enabl       ;wait untill done
	and #%00000011  ; with t1 & t2
	bne rspoff
	lda #%00010000  ; disable flag (need to renable in user code)
	sta d2icr       ;turn of enabl************
	lda #0
	sta enabl       ;clear all enabls
rspok	pla             ;all done
	rts

; rsr  8/24/80 original code out
; rsr  8/25/80 original code in
; rsr  9/22/80 remove parallel refs & fix xline logic
; rsr 12/11/81 modify for vic-40 i/o
; rsr  2/15/82 fix some enabl problems
; rsr  3/31/82 fix flase starts on transmitt
; rsr  5/12/82 reduce code and fix x-line cts hold-off
