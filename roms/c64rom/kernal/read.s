; variables used in cassette read routines
;
;  rez - counts zeros (if z then correct # of dipoles)
;  rer - flags errors (if z then no error)
;  diff - used to preserve syno (outside of bit routines)
;  syno - flags if we have block sync (16 zero dipoles)
;  snsw1 - flags if we have byte sync (a longlong)
;  data - holds most recent dipole bit value
;  mych - holds input byte being built
;  firt - used to indicate which half of dipole we're in
;  svxt - temp used to adjust software servo
;  temp - used to hold dipole time during type calculations
;  prty - holds current calculated parity bit
;  prp - has combined error values from bit routines
;  fsblk - indicate which block we're looking at (0 to exit)
;  shcnl - holds fsblk, used to direct routines, because of exit case
;  rdflg - holds function mode
;     mi - waiting for block sync
;     vs - in data block reading data
;     ne - waiting for byte sync
;  sal - indirect to data storage area
;  shcnh - left over from debugging
;  bad - storage space for bad read locations (bottom of stack)
;  ptr1 - count of read locations in error (pointer into bad, max 61)
;  ptr2 - count of re-read locations (pointer into bad, during re-read)
;  verchk - verify or load flag (z - loading)
;  cmp0 - software servo (+/- adjust to time calcs)
;  dpsw - if nz then expecting ll/l combination that ends a byte
;  pcntr - counts down from 8-0 for data then to ff for parity
;  stupid - hold indicator (nz - no t1irq yet) for t1irq
;  kika26 - holds old d1icr after clear on read
;
read	ldx d1t2h       ;get time since last interrupt
	ldy #$ff        ;compute counter difference
	tya
	sbc d1t2l
	cpx d1t2h       ;check for timer high rollover...
	bne read        ;...yes then recompute
	stx temp
	tax
	sty d1t2l       ;reload timer2 (count down from $ffff)
	sty d1t2h
	lda #$19        ;enable timer
	sta d1crb
	lda d1icr       ;clear read interrupt
	sta kika26      ;save for latter
	tya
	sbc temp        ;calculate high
	stx temp
	lsr a           ;move two bits from high to temp
	ror temp
	lsr a
	ror temp
	lda cmp0        ;calc min pulse value
	clc
	adc #60
	cmp temp        ;if pulse less than min...
	bcs rdbk        ;...then ignore as noise
	ldx dpsw        ;check if last bit...
	beq rjdj        ;...no then continue
	jmp radj        ;...yes then go finish byte

rjdj	ldx pcntr       ;if 9 bits read...
	bmi jrad2       ;... then goto ending
	ldx #0          ;set bit value to zero
	adc #48         ;add up to half way between...
	adc cmp0        ;...short pulse and sync pulse
	cmp temp        ;check for short...
	bcs radx2       ;...yes it's a short
	inx             ;set bit value to one
	adc #38         ;move to middle of high
	adc cmp0
	cmp temp        ;check for one...
	bcs radl        ;...yes it's a one
	adc #44         ;move to longlong
	adc cmp0
	cmp temp        ;check for longlong...
	bcc srer        ;...greater than is error
jrad2	jmp rad2        ;...it's a longlong

srer	lda snsw1       ;if not syncronized...
	beq rdbk        ;...then no error
	sta rer         ;...else flag rer
	bne rdbk        ;jmp

radx2	inc rez         ;count rez up on zeros
	bcs rad5        ;jmp
radl	dec rez         ;count rez down on ones
rad5	sec             ;calc actual value for compare store
	sbc #19
	sbc temp        ;subtract input value from constant...
	adc svxt        ;...add difference to temp storage...
	sta svxt        ;...used later to adjust soft servo
	lda firt        ;flip dipole flag
	eor #1
	sta firt
	beq rad3        ;second half of dipole
	stx data        ;first half so store its value

rdbk	lda snsw1       ;if no byte start...
	beq radbk       ;...then return
	lda kika26      ;check to see if timer1 irqd us...
	and #$01
	bne radkx       ;...yes
	lda stupid      ;check for old t1irq
	bne radbk       ;no...so exit
;
radkx	lda #0          ;...yes, set dipole flag for first half
	sta firt
	sta stupid      ;set t1irq flag
	lda pcntr       ;check where we are in byte...
	bpl rad4        ;...doing data
	bmi jrad2       ;...process parity

radp	ldx #166        ;set up for longlong timeout
	jsr stt1
	lda prty        ;if parity not even...
	bne srer        ;...then go set error
radbk	jmp prend       ;go restore regs and rti

rad3	lda svxt        ;adjust the software servo (cmp0)
	beq rout1       ;no adjust
	bmi rout2       ;adjust for more base time
	dec cmp0        ;adjust for less base time
	.byt $2c        ;skip two bytes
rout2	inc cmp0
rout1	lda #0          ;clear difference value
	sta svxt
;check for consecutive like values in dipole...
	cpx data
	bne rad4        ;...no, go process info
	txa             ;...yes so check the values...
	bne srer        ;if they were ones then  error
; consecutive zeros
	lda rez         ;...check how many zeros have happened
	bmi rdbk        ;...if many don't check
	cmp #16         ;... do we have 16 yet?...
	bcc rdbk        ;....no so continue
	sta syno        ;....yes so flag syno (between blocks)
	bcs rdbk        ;jmp

rad4	txa             ;move read data to .a
	eor prty        ;calculate parity
	sta prty
	lda snsw1       ;real data?...
	beq radbk       ;...no so forget by exiting
	dec pcntr       ;dec bit count
	bmi radp        ;if minus then  time for parity
	lsr data        ;shift bit from data...
	ror mych        ;...into byte storage (mych) buffer
	ldx #218        ;set up for next dipole
	jsr stt1
	jmp prend       ;restore regs and rti

; rad2 - longlong handler (could be a long one)
rad2	lda syno        ;have we gotten block sync...
	beq rad2y       ;...no
	lda snsw1       ;check if we've had a real byte start...
	beq rad2x       ;...no
rad2y	lda pcntr       ;are we at end of byte...
	bmi rad2x       ;yes...go adjust for longlong
	jmp radl        ;...no so treat it as a long one read

rad2x	lsr temp        ;adjust timeout for...
	lda #147        ;...longlong pulse value
	sec
	sbc temp
	adc cmp0
	asl a
	tax             ;and set timeout for last bit
	jsr stt1
	inc dpsw        ;set bit throw away flag
	lda snsw1       ;if byte syncronized....
	bne radq2       ;...then skip to pass char
	lda syno        ;throws out data untill block sync...
	beq rdbk2       ;...no block sync
	sta rer         ;flag data as error
	lda #0          ;kill 16 sync flag
	sta syno
	lda #$81        ;set up for timer1 interrupts
	sta d1icr
	sta snsw1       ;flag that we have byte syncronized
;
radq2	lda syno        ;save syno status
	sta diff
	beq radk        ;no block sync, no byte looking
	lda #0          ;turn off byte sync switch
	sta snsw1
	lda #$01        ;disable timer1 interrupts
	sta d1icr
radk	lda mych        ;pass character to byte routine
	sta ochar
	lda rer         ;combine error values with zero count...
	ora rez
	sta prp         ;...and save in prp
rdbk2	jmp prend       ;go back and get last byte

radj	jsr newch       ;finish byte, clr flags
	sta dpsw        ;clear bit throw away flag
	ldx #218        ;initilize for next dipole
	jsr stt1
	lda fsblk       ;check for last value
	beq rd15
	sta shcnl

;*************************************************
;* byte handler of cassette read                 *
;*                                               *
;* this portion of in line code is passed the    *
;* byte assembled from reading tape in ochar.    *
;* rer is set if the byte read is in error.      *
;* rez is set if the interrupt program is reading*
;* zeros.  rdflg tells us what we are doing.     *
;* bit 7 says to ignore bytes until rez is set   *
;* bit 6 says to load the byte. otherwise rdflg  *
;* is a countdown after sync.  if verck is set   *
;* we do a compare instead of a store and set    *
;* status.  fsblk counts the two blocks. ptr1 is *
;* index to error table for pass1.  ptr2 is index*
;* to correction table for pass2.                *
;*************************************************
;
sperr=16
ckerr=32
sberr=4
lberr=8
;
rd15	lda #$f
;
	bit rdflg       ;test function mode
	bpl rd20        ;not waiting for zeros
;
	lda diff        ;zeros yet?
	bne rd12        ;yes...wait for sync
	ldx fsblk       ;is pass over?
	dex             ;...if fsblk zero then no error (first good)
	bne rd10        ;no...
;
	lda #lberr
	jsr udst        ;yes...long block error
	bne rd10        ;branch always
;
rd12	lda #0
	sta rdflg       ;new mode is wait for sync
rd10	jmp prend       ;exit...done
;
rd20	bvs rd60        ;we are loading
	bne rd200       ;we are syncing
;
	lda diff        ;do we have block sync...
	bne rd10        ;...yes, exit
	lda prp         ;if first byte has error...
	bne rd10        ;...then skip (exit)
	lda shcnl       ;move fsblk to carry...
	lsr a
	lda ochar       ; should be a header count char
	bmi rd22        ;if neg then firstblock data
	bcc rd40        ;...expecting firstblock data...yes
	clc
rd22	bcs rd40        ;expecting second block?...yes
	and #$f         ;mask off high store header count...
	sta rdflg       ;...in mode flag (have correct block)
rd200	dec rdflg       ;wait untill we get real data...
	bne rd10        ;...9876543210 real
	lda #$40        ;next up is real data...
	sta rdflg       ;...set data mode
	jsr rd300       ;go setup address pointers
	lda #0          ;debug code##################################################
	sta shcnh
	beq rd10        ;jmp to continue

rd40	lda #$80        ;we want to...
	sta rdflg       ;ignore bytes mode
	bne rd10        ;jmp

rd60	lda diff        ;check for end of block...
	beq rd70        ;...okay
;
	lda #sberr      ;short block error
	jsr udst
	lda #0          ;force rdflg for an end
	jmp rd161

rd70	jsr cmpste      ;check for end of storage area
	bcc *+5         ;not done yet
	jmp rd160
	ldx shcnl       ;check which pass...
	dex
	beq rd58        ;...second pass
	lda verck       ;check if load or verify...
	beq rd80        ;...loading
	ldy #0          ;...just verifying
	lda ochar
	cmp (sal),y      ;compare with data in pet
	beq rd80        ;...good so continue
	lda #1          ;...bad so flag...
	sta prp         ;...as an error

; store bad locations for second pass re-try
rd80	lda prp         ;chk for errors...
	beq rd59        ;...no errors
	ldx #61         ;max allowed is 30
	cpx ptr1        ;are we at max?...
	bcc rd55        ;...yes, flag as second pass error
	ldx ptr1        ;get index into bad...
	lda sah         ;...and store the bad location
	sta bad+1,x     ;...in bad table
	lda sal
	sta bad,x
	inx             ;advance pointer to next
	inx
	stx ptr1
	jmp rd59        ;go store character

; check bad table for re-try (second pass)
rd58	ldx ptr2        ;have we done all in the table?...
	cpx ptr1
	beq rd90        ;...yes
	lda sal         ;see if this is next in the table...
	cmp bad,x
	bne rd90        ;...no
	lda sah
	cmp bad+1,x
	bne rd90        ;...no
	inc ptr2        ;we found next one, so advance pointer
	inc ptr2
	lda verck       ;doing a load or verify?...
	beq rd52        ;...loading
	lda ochar       ;...verifying, so check
	ldy #0
	cmp (sal),y
	beq rd90        ;...okay
	iny             ;make .y= 1
	sty prp         ;flag it as an error

rd52	lda prp         ;a second pass error?...
	beq rd59        ;...no
;second pass err
rd55	lda #sperr
	jsr udst
	bne rd90        ;jmp

rd59	lda verck       ;load or verify?...
	bne rd90        ;...verify, don't store
	tay             ;make y zero
	lda ochar
	sta (sal),y      ;store character
rd90	jsr incsal      ;increment addr.
	bne rd180       ;branch always

rd160	lda #$80        ;set mode skip next data
rd161	sta rdflg
;
; modify for c64 6526's
;
	sei             ;protect clearing of t1 information
	ldx #$01
	stx d1icr       ;clear t1 enable...
	ldx d1icr       ;clear the interrupt
	ldx fsblk       ;dec fsblk for next pass...
	dex
	bmi rd167       ;we are done...fsblk=0
	stx fsblk       ;...else fsblk=next
rd167	dec shcnl       ;dec pass calc...
	beq rd175       ;...all done
	lda ptr1        ;check for first pass errors...
	bne rd180       ;...yes so continue
	sta fsblk       ;clear fsblk if no errors...
	beq rd180       ;jmp to exit

rd175	jsr tnif        ;read it all...exit
	jsr rd300       ;restore sal & sah
	ldy #0          ;set shcnh to zero...
	sty shcnh       ;...used to calc parity byte
;
;compute parity over load
;
vprty	lda (sal),y      ;calc block bcc
	eor shcnh
	sta shcnh
	jsr incsal      ;increment address
	jsr cmpste      ;test against end
	bcc vprty       ;not done yet...
	lda shcnh       ;check for bcc char match...
	eor ochar
	beq rd180       ;...yes, exit
;chksum error
	lda #ckerr
	jsr udst
rd180	jmp prend

rd300	lda stah        ; restore starting address...
	sta sah         ;...pointers (sah & sal)
	lda stal
	sta sal
	rts

newch	lda #8          ;set up for 8 bits+parity
	sta pcntr
	lda #0          ;initilize...
	sta firt        ;..dipole counter
	sta rer         ;..error flag
	sta prty        ;..parity bit
	sta rez         ;..zero count
	rts             ;.a=0 on return

; rsr 7/31/80 add comments
; rsr 3/28/82 modify for c64 (add stupid/comments)
; rsr 3/29/82 put block t1irq control
; rsr 5/11/82 modify c64 stupid code
