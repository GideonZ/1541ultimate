
	.setcpu		"6502"
    .segment    "CODE"
    
    .word   *+2     ; load address
	.export		_main

_main:
start:
    sei
    ldx #$FF
    txs
    
    lda #>data_start
    sta my_pnt+1
    lda #<data_start
    sta my_pnt
    
    ldy #$00
loop1:
    tya
    sta (my_pnt),y
    iny
    bne loop1
    
; Check now:

    ldx #$00
loop2:
    lda data_start,x
    stx work_reg
    cmp work_reg
    bne error
    inx
    bne loop2;

; Check again:

    ldx #$00
loop3:
    txa
    cmp data_start,x
    bne error
    inx
    bne loop3;

; Exit with success

    lda #$55
    sta $FFF8

    rts

; Exit with error
error:
    lda #$00
    sta $FFF8
    rts
    
nmi_vector:
    rti

reset_vector:
    jmp start
    
irq_vector:
    rti

    .segment    "WORK":zeropage
my_pnt:
    .word       0
work_reg:
    .byte       0
    
    
    
    .segment    "DATA"
data_start:
    .word       0

    .segment    "VECTORS"
    .word       nmi_vector
    .word       reset_vector
    .word       irq_vector
    