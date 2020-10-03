	.segment "TIME"
;***********************************
;*                                 *
;* time                            *
;*                                 *
;*consists of three functions:     *
;* (1) udtim-- update time. usually*
;*     called every 60th second.   *
;* (2) settim-- set time. .y=msd,  *
;*     .x=next significant,.a=lsd  *
;* (3) rdtim-- read time. .y=msd,  *
;*     .x=next significant,.a=lsd  *
;*                                 *
;***********************************

;interrupts are coming from the 6526 timers
;
udtim	ldx #0          ;pre-load for later
;
;here we proceed with an increment
;of the time register.
;
ud20	inc time+2
	bne ud30
	inc time+1
	bne ud30
	inc time
;
;here we check for roll-over 23:59:59
;and reset the clock to zero if true
;
ud30	sec
	lda time+2
	sbc #$01
	lda time+1
	sbc #$1a
	lda time
	sbc #$4f
	bcc ud60
;
;time has rolled--zero register
;
	stx time
	stx time+1
	stx time+2
;
;set stop key flag here
;
ud60	lda rows        ;wait for it to settle
	cmp rows
	bne ud60        ;still bouncing
	tax             ;set flags...
	bmi ud80        ;no stop key...exit  stop key=$7f
	ldx #$ff-$42    ;check for a shift key (c64 keyboard)
	stx colm
ud70	ldx rows        ;wait to settle...
	cpx rows
	bne ud70
	sta colm        ;!!!!!watch out...stop key .a=$7f...same as colms was...
	inx             ;any key down aborts
	bne ud90        ;leave same as before...
ud80	sta stkey       ;save for other routines
ud90	rts

rdtim	sei             ;keep time from rolling
	lda time+2      ;get lsd
	ldx time+1      ;get next most sig.
	ldy time        ;get msd

settim	sei             ;keep time from changing
	sta time+2      ;store lsd
	stx time+1      ;next most significant
	sty time        ;store msd
	cli
	rts

; rsr 8/21/80 remove crfac change stop
; rsr 3/29/82 add shit key check for commodore 64
