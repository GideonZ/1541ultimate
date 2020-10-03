keycod	;keyboard mode 'dispatch'
	.word mode1
	.word mode2
	.word mode3
	.word contrl    ;control keys
;
; cottaconna mode
;
;.word mode1  ;pet mode1
;.word mode2  ;pet mode2
;.word cctta3 ;dummy word
;.word contrl
;
; extended katakana mode
;
;.word cctta2 ;katakana characters
;.word cctta3 ;limited graphics
;.word cctta3 ;dummy
;.word contrl

mode1
;del,3,5,7,9,+,yen sign,1
	.byt $14,$0d,$1d,$88,$85,$86,$87,$11
;return,w,r,y,i,p,*,left arrow
	.byt $33,$57,$41,$34,$5a,$53,$45,$01
;rt crsr,a,d,g,j,l,;,ctrl
	.byt $35,$52,$44,$36,$43,$46,$54,$58
;f4,4,6,8,0,-,home,2
	.byt $37,$59,$47,$38,$42,$48,$55,$56
;f1,z,c,b,m,.,r.shiftt,space
	.byt $39,$49,$4a,$30,$4d,$4b,$4f,$4e
;f2,s,f,h,k,:,=,com.key
	.byt $2b,$50,$4c,$2d,$2e,$3a,$40,$2c
;f3,e,t,u,o,@,exp,q
	.byt $5c,$2a,$3b,$13,$01,$3d,$5e,$2f
;crsr dwn,l.shift,x,v,n,,,/,stop
	.byt $31,$5f,$04,$32,$20,$02,$51,$03
	.byt $ff        ;end of table null

mode2	;shift
;ins,%,',),+,yen,!
	.byt $94,$8d,$9d,$8c,$89,$8a,$8b,$91
;sreturn,w,r,y,i,p,*,sleft arrow
	.byt $23,$d7,$c1,$24,$da,$d3,$c5,$01
;lf.crsr,a,d,g,j,l,;,ctrl
	.byt $25,$d2,$c4,$26,$c3,$c6,$d4,$d8
;,$,&,(,      ,"
	.byt $27,$d9,$c7,$28,$c2,$c8,$d5,$d6
;f5,z,c,b,m,.,r.shift,sspace
	.byt $29,$c9,$ca,$30,$cd,$cb,$cf,$ce
;f6,s,f,h,k,:,=,scom.key
	.byt $db,$d0,$cc,$dd,$3e,$5b,$ba,$3c
;f7,e,t,u,o,@,pi,g
	.byt $a9,$c0,$5d,$93,$01,$3d,$de,$3f
;crsr dwn,l.shift,x,v,n,,,/,run
	.byt $21,$5f,$04,$22,$a0,$02,$d1,$83
	.byt $ff        ;end of table null
;
mode3	;left window grahpics
;ins,c10,c12,c14,9,+,pound sign,c8
	.byt $94,$8d,$9d,$8c,$89,$8a,$8b,$91
;return,w,r,y,i,p,*,lft.arrow
	.byt $96,$b3,$b0,$97,$ad,$ae,$b1,$01
;lf.crsr,a,d,g,j,l,;,ctrl
	.byt $98,$b2,$ac,$99,$bc,$bb,$a3,$bd
;f8,c11,c13,c15,0,-,home,c9
	.byt $9a,$b7,$a5,$9b,$bf,$b4,$b8,$be
;f2,z,c,b,m,.,r.shift,space
	.byt $29,$a2,$b5,$30,$a7,$a1,$b9,$aa
;f4,s,f,h,k,:,=,com.key
	.byt $a6,$af,$b6,$dc,$3e,$5b,$a4,$3c
;f6,e,t,u,o,@,pi,q
	.byt $a8,$df,$5d,$93,$01,$3d,$de,$3f
;crsr.up,l.shift,x,v,n,,,/,stop
	.byt $81,$5f,$04,$95,$a0,$02,$ab,$83
	.byt $ff        ;end of table null
;cctta2 ;was cctta2 in japanese version
lower
	cmp #$0e        ;does he want lower case?
	bne upper       ;branch if not
	lda vicreg+24   ;else set vic to point to lower case
	ora #$02
	bne ulset       ;jmp

upper
	cmp #$8e        ;does he want upper case
	bne lock        ;branch if not
	lda vicreg+24   ;make sure vic point to upper/pet set
	and #$ff-$02
ulset	sta vicreg+24
outhre	jmp loop2

lock
	cmp #8          ;does he want to lock in this mode?
	bne unlock      ;branch if not
	lda #$80        ;else set lock switch on
	ora mode        ;don't hurt anything - just in case
	bmi lexit

unlock
	cmp #9          ;does he want to unlock the keyboard?
	bne outhre      ;branch if not
	lda #$7f        ;clear the lock switch
	and mode        ;dont hurt anything
lexit	sta mode
	jmp loop2       ;get out
;cctta3
;.byt $04,$ff,$ff,$ff,$ff,$ff,$e2,$9d
;run-k24-k31
;.byt $83,$01,$ff,$ff,$ff,$ff,$ff,$91
;k32-k39.f5
;.byt $a0,$ff,$ff,$ff,$ff,$ee,$01,$89
;co.key,k40-k47.f6
;.byt $02,$ff,$ff,$ff,$ff,$e1,$fd,$8a
;k48-k55
;.byt $ff,$ff,$ff,$ff,$ff,$b0,$e0,$8b
;k56-k63
;.byt $f2,$f4,$f6,$ff,$f0,$ed,$93,$8c
;.byt $ff ;end of table null

contrl
;null,red,purple,blue,rvs ,null,null,black
	.byt $ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff
;null, w  ,reverse, y  , i  , p  ,null,music
	.byt $1c,$17,$01,$9f,$1a,$13,$05,$ff
	.byt $9c,$12,$04,$1e,$03,$06,$14,$18
;null,cyan,green,yellow,rvs off,null,null,white
	.byt $1f,$19,$07,$9e,$02,$08,$15,$16
	.byt $12,$09,$0a,$92,$0d,$0b,$0f,$0e
	.byt $ff,$10,$0c,$ff,$ff,$1b,$00,$ff
	.byt $1c,$ff,$1d,$ff,$ff,$1f,$1e,$ff
	.byt $90,$06,$ff,$05,$ff,$ff,$11,$ff
	.byt $ff        ;end of table null
tvic
	.byt 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ;sprites (0-16)
	.byt $9b,55,0,0,0,$08,0,$14,$0f,0,0,0,0,0,0 ;data (17-31)
	.byt 14,6,1,2,3,4,0,1,2,3,4,5,6,7 ;32-46
;
runtb	.byt "LOAD",$d,"RUN",$d
;
linz0	= vicscn
linz1	= linz0+llen
linz2	= linz1+llen
linz3	= linz2+llen
linz4	= linz3+llen
linz5	= linz4+llen
linz6	= linz5+llen
linz7	= linz6+llen
linz8	= linz7+llen
linz9	= linz8+llen
linz10	= linz9+llen
linz11	= linz10+llen
linz12	= linz11+llen
linz13	= linz12+llen
linz14	= linz13+llen
linz15	= linz14+llen
linz16	= linz15+llen
linz17	= linz16+llen
linz18	= linz17+llen
linz19	= linz18+llen
linz20	= linz19+llen
linz21	= linz20+llen
linz22	= linz21+llen
linz23	= linz22+llen
linz24	= linz23+llen

;****** screen lines lo byte table ******
;
ldtb2
	.byte <linz0
	.byte <linz1
	.byte <linz2
	.byte <linz3
	.byte <linz4
	.byte <linz5
	.byte <linz6
	.byte <linz7
	.byte <linz8
	.byte <linz9
	.byte <linz10
	.byte <linz11
	.byte <linz12
	.byte <linz13
	.byte <linz14
	.byte <linz15
	.byte <linz16
	.byte <linz17
	.byte <linz18
	.byte <linz19
	.byte <linz20
	.byte <linz21
	.byte <linz22
	.byte <linz23
	.byte <linz24

; rsr 12/08/81 modify for vic-40 keyscan
; rsr  2/17/81 modify for the stinking 6526r2 chip
; rsr  3/11/82 modify for commodore 64
; rsr  3/28/82 modify for new pla
