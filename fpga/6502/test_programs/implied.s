
	.setcpu		"6502"
    .segment    "CODE"
    
    .word   *+2     ; load address
	.export		_main

_main:
start:
    sei
    ldx #$FF
    txs

    lda #<start2
    sta data_start
    lda #>start2
    sta data_start+1
    jmp (data_start)
    
    nop
    nop
    nop
    
start2:
    lda #$00
    sta work_reg
    
; Check implied functions
; PHP PLP PHA PLA DEY TAY INY INX
; ASL ROL LSR ROR TXA TAX DEX NOP
; CLC SEC CLI SEI TYA CLV CLD SED
; NOP*NOP*NOP*NOP*TXS TSX NOP*NOP*


;    new_flags(7) <= n_out;
;    new_flags(6) <= v_out;
;    new_flags(5) <= '1';
;    new_flags(4) <= irq_n; -- BREAK... 
;    new_flags(3) <= d_out;
;    new_flags(2) <= i_out;
;    new_flags(1) <= z_out;
;    new_flags(0) <= c_out;

    ; Check if all flags can be set and cleared
    ; CLC
    lda #$01
    pha
    plp
    clc
    php
    pla
    and #$01
    bne error
    
    ; SEC
    inc work_reg
    lda #$FE
    pha
    plp
    sec
    php
    pla
    and #$01
    beq error

    ; CLI
    inc work_reg
    lda #$04
    pha
    plp
    cli
    php
    pla
    and #$04
    bne error
    
    ; SEI
    inc work_reg
    lda #$FB
    pha
    plp
    sei
    php
    pla
    and #$04
    beq error
    
    ; CLV
    inc work_reg
    lda #$40
    pha
    plp
    clv
    php
    pla
    and #$40
    bne error
    
    ; Set overflow
    inc work_reg
    clc
    lda #$40
    adc #$60
    php
    pla
    and #$40
    beq error
    
    ; CLD
    inc work_reg
    lda #$08
    pha
    plp
    cld
    php
    pla
    and #$08
    bne error
    
    ; SED
    inc work_reg
    lda #$F7
    pha
    plp
    sed
    php
    pla
    and #$08
    beq error
    
    jmp continue

; Exit with error
error:
    lda #$00
    sta $FFF8
    rts
    
continue:
; Now check if TXS TSX work
    inc work_reg
    ldx #$BA
    lda #$00    ; sets zero flag
    txs
    ; flags should not have changed, hence zero should be set
    bne error
    lda #$12
    pha
    lda #$00    ; sets zero flag
    bne error   ; really zero? :D
    tsx         ; this does set the flags
    beq error   ; should not occur, because s should be B9
    
    txa
    cmp #$b9
    bne error
    
    ; now check if the byte was written in memory
    lda $01BA
    cmp #$12
    bne error
    
; DEY
    inc work_reg
    ldy #$00
    sec
    dey
    bcc error
    bpl error
    beq error
    cpy #$FF
    bne error
    
    clc
    dey
    bcs error
    cpy #$FE
    bne error
    bmi error
    
; INY
    inc work_reg
    ldy #$44
    sec
    iny
    bcc error
    bmi error
    beq error
    cpy #$45
    bne error
    tya
    cmp #$45
    bne error
    
; Break
    brk
    nop

; ASL
    ; test no carry in
    inc work_reg
    lda #$01
    sec
    asl a
    bcs error
    beq error
    bmi error
    cmp #$02
    bne error
    
    ; test carry out
    lda #$80
    asl a
    bcc error
    bne error
    bmi error
    
;   ROL
    inc work_reg
    lda #$01
    sec
    rol a
    cmp #$03
    bne error
    
; ROR
    inc work_reg
    lda #$03
    sec
    ror a
    bcc error2
    bpl error2
    cmp #$81
    bne error2
    
; LSR
    inc work_reg
    lda #$02
    sec
    lsr a
    bcs error2
    bmi error2
    cmp #$01
    bne error2

; NOP
    ldy #$00
nop_loop:
    php
    nop
    nop
    nop
    pla
    tsx
    cmp $0100,x
    bne error2
    iny
    cpy #8
    bne nop_loop
    
; test JSR, too, see if it influences the flags
    lda #$FF
    pha
    plp
    jsr check_flags
    jsr check_flags

    lda #$00
    ; clear the stack a bit
    pha
    pha
    pha
    pha
    pha
    pla
    pla
    pla
    pla
    plp ; clear all flags
    
    sec ; set carry
    jsr dummy_sub
    bcc error2

; CMP ($nn,X)
    lda #$78
    sta $1234
    lda #$34
    sta my_pnt+41
    lda #$12
    sta my_pnt+42
    ldx #41
    lda (<my_pnt,x)
    cmp #$78
    bne error2

    lda #$55
    sta $FFF8
    rts

error2:
    lda #$00
    sta $FFF8
    rts

check_flags:
    php
    tay
    tsx
    cmp $0101,X
    bne error2
    pla
    tya
    pha
    plp
    rts

dummy_sub:
    rts
        
nmi_vector:
    pha
    lda #$03
    sta $FFF9
    pla
    rti

reset_vector:
    jmp start
    
irq_vector:
    pha
    php
    pla
    and #$10
    bne do_break
    ; else IRQ
    lda #$02
    sta $FFF9
    pla
    rti
do_break:
    lda #$01
    sta $FFF9
    pla
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
    