;***************************************
;* chkin -- open channel for input     *
;*                                     *
;* the number of the logical file to be*
;* opened for input is passed in .x.   *
;* chkin searches the logical file     *
;* to look up device and command info. *
;* errors are reported if the device   *
;* was not opened for input ,(e.g.     *
;* cassette write file), or the logical*
;* file has no reference in the tables.*
;* device 0, (keyboard), and device 3  *
;* (screen), require no table entries  *
;* and are handled separate.           *
;***************************************
;
nchkin	jsr lookup      ;see if file known
	beq jx310       ;yup...
;
	jmp error3      ;no...file not open
;
jx310	jsr jz100       ;extract file info
;
	lda fa
	beq jx320       ;is keyboard...done.
;
;could be screen, keyboard, or serial
;
	cmp #3
	beq jx320       ;is screen...done.
	bcs jx330       ;is serial...address it
	cmp #2          ;rs232?
	bne jx315       ;no...
;
	jmp cki232
;
;some extra checks for tape
;
jx315	ldx sa
	cpx #$60        ;is command a read?
	beq jx320       ;yes...o.k....done
;
	jmp error6      ;not input file
;
jx320	sta dfltn       ;all input come from here
;
	clc             ;good exit
	rts
;
;an serial device has to be a talker
;
jx330	tax             ;device # for dflto
	jsr talk        ;tell him to talk
;
	lda sa          ;a second?
	bpl jx340       ;yes...send it
	jsr tkatn       ;no...let go
	jmp jx350
;
jx340	jsr tksa        ;send second
;
jx350	txa
	bit status      ;did he listen?
	bpl jx320       ;yes
;
	jmp error5      ;device not present

;***************************************
;* chkout -- open channel for output     *
;*                                     *
;* the number of the logical file to be*
;* opened for output is passed in .x.  *
;* chkout searches the logical file    *
;* to look up device and command info. *
;* errors are reported if the device   *
;* was not opened for input ,(e.g.     *
;* keyboard), or the logical file has   *
;* reference in the tables.             *
;* device 0, (keyboard), and device 3  *
;* (screen), require no table entries  *
;* and are handled separate.           *
;***************************************
;
nckout	jsr lookup      ;is file in table?
	beq ck5         ;yes...
;
	jmp error3      ;no...file not open
;
ck5	jsr jz100       ;extract table info
;
	lda fa          ;is it keyboard?
	bne ck10        ;no...something else.
;
ck20	jmp error7      ;yes...not output file
;
;could be screen,serial,or tapes
;
ck10	cmp #3
	beq ck30        ;is screen...done
	bcs ck40        ;is serial...address it
	cmp #2          ;rs232?
	bne ck15
;
	jmp cko232
;
;
;special tape channel handling
;
ck15	ldx sa
	cpx #$60        ;is command read?
	beq ck20        ;yes...error
;
ck30	sta dflto       ;all output goes here
;
	clc             ;good exit
	rts
;
ck40	tax             ;save device for dflto
	jsr listn       ;tell him to listen
;
	lda sa          ;is there a second?
	bpl ck50        ;yes...
;
	jsr scatn       ;no...release lines
	bne ck60        ;branch always
;
ck50	jsr secnd       ;send second...
;
ck60	txa
	bit status      ;did he listen?
	bpl ck30        ;yes...finish up
;
	jmp error5      ;no...device not present

