;-----------------------------------------------------------------------
; FILE basecrt.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2019 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Base routines for starting the SID and MUS cartridges and for loading files.
;-----------------------------------------------------------------------

startNMI        pla                 ; ignore pressing restore key
                tay
                pla
                tax
                pla
                rti

start           sei
                lda #$7f
                sta $dd0d
                lda $dd0d

                lda #$00
                sta $d020
                sta $d021
                sta $d418

                jsr screenOff

                jsr $ff84           ; init I/O devices / CIA timers
                jsr alternative_ff87
                jsr $ff8a           ; restore default IO vectors

                lda $02
                cmp #SID_MODE
                beq sidMode
                cmp #DMA_MODE
                beq dmaMode

                jsr resetSID
                jmp reset

; first write $ff for tunes like:
; /MUSICIANS/T/Tel_Jeroen/Cybernoid.sid
; /MUSICIANS/B/Bjerregaard_Johannes/Stormlord_V2.sid
resetSID        lda #$ff
resetSIDLoop    ldx #$17
-               sta $d400,x
                dex
                bpl -
                tax
                bpl +
                lda #$08
                bpl resetSIDLoop
+
-               bit $d011
                bpl -
-               bit $d011
                bmi -
                eor #$08
                beq resetSIDLoop
                rts

screenOff       lda $d011
                and #$ef
                sta $d011

-               lda $d011
                bmi -
-               lda $d011
                bpl -
-               lda $d011
                bmi -
                rts

dmaMode         jsr resetSID

                lda #$00            ; load PRG file
                sta $02

                jsr $ff81           ; (init screen and VIC-Chip the normal way)
                jmp +

sidMode         jsr alternative_ff81

+               jsr $e453           ; copy BASIC vectors to RAM

                lda $0300
                sta $0334
                lda $0301
                sta $0335
                lda #<basicStarted
                sta $0300
                lda #>basicStarted
                sta $0301
                lda $a001
                sta $0101
                lda $a000
                clc
                adc #$03            ; skip JSR $E453 to avoid overwriting vector at $0300
                sta $0100
                bcc +
                inc $0101
                cli
+               jmp ($0100)

alternative_ff87
                ; jsr $FF87 / $FD50 (init RAM, tape buffer, screen) is replaced by the following block
                lda #$00
                tay
-               sta $0200,y
                iny
                bne -
                ldx #$3c
                ldy #$03
                stx $b2
                sty $b3

                lda #$A0
                sta $0284
                lda #$08
                sta $0282
                lsr
                sta $0288
                rts

alternative_ff81
                ; jsr $FF81 (init screen and VIC-Chip) is replaced by the following block to avoid screen color change
                lda #$03
                sta $9a
                lda #$00
                sta $99
                ldx #$2f
-               lda $ecb8,x
                sta $cfff,x
                cpx #$23            ; don't set $d020 and $d021
                bne +
                dex
                dex
+               cpx #$13            ; don't set d011
                bne +
                dex
+               dex
                bne -

                jsr $e51b
                jmp $ff5e

basicStarted    sei
                lda #<basicStartedIRQ
                sta $0314
                lda #>basicStartedIRQ
                sta $0315

                lda $0334
                sta $0300
                lda $0335
                sta $0301
                jmp ($0300)

initMem         lda $01
                pha

                lda #$34
                sta $01

                lda #$00
                sta $0162

                ldx #$01            ; receive DMA load
                stx $02

-               bit $0162           ; wait till DMA load is ready
                bpl -

                pla
                sta $01

                lda #$00
                sta $02             ; restore value of address $0002 to $00

                lda $0162
                cmp #$BC
                beq run_basic
                cmp #$AA
                bne go_basic
                rts

; WE GET HERE ON INTERRUPT!
run_basic       pla                 ; 3x from the interrupt
                pla
                pla
                jsr turnOffCart
                cli                 ; we just let the interrupt occur again
                lda #1              ; disable cursor blink
                sta $CC
                lda #'R'
                sta $0200
                lda #'U'
                sta $0201
                lda #'N'
                sta $0202
                lda #0
                sta $13
                ldx #3
                jsr $AACA
                jmp $A486

; WE GET HERE ON INTERRUPT!
go_basic        jsr turnOffCart
                jmp $ea31
initMemEnd

basicStartedIRQ
                lda #$31            ; restore IRQ vector
                sta $0314
                lda #$ea
                sta $0315

                lda $dc0d           ; acknowledge IRQ

                lda #<initMem
                ldx #>initMem
                ldy #initMemEnd - initMem
                jsr copyTo0100

                lda $02
                cmp #SID_MODE
                beq clearMemAndLoad
                jmp $0100           ; clear memory and execute DMA load, and do run

reset           lda #<resetRoutine
                ldx #>resetRoutine
                ldy #resetRoutineEnd - resetRoutine
                jmp runAt0100

resetRoutine    inc $8005

                lda $dffd ; Identification register
                cmp #$c9
                bne noUCI

                ; Check if the UCI is busy
                lda $dffc ; Status register
                and #$30  ; State bits
                bne noUCI ; Temporarily unavailable

                lda #$84  ; Control Target (Target #4 without reply)
                sta $dffd ; Command pipe
                lda #$06  ; Command Reset Normal Cartridge
                sta $dffd ; Command pipe
                ; No params
                lda #$01  ; New Command!
                sta $dffc ; Control register

                ; Now wait patiently, note A = 01
-               bne -

noUCI           jsr turnOffCart
                cli
                jmp ($fffc)
resetRoutineEnd

clearMemAndLoad ldx #$ff            ; init stack pointer
                txs
                lda #$fc            ; push reset address on stack
                pha
                lda #$e2 - 1
                pha

                jsr $0100           ; clear memory and execute DMA load

                ; important: retrieve value at $$0165/$0165 after DMA load
                lda $0165           ; get high address of SID header
                sta SID_HEADER_HI   ; store SID header address hi-byte
                lda $0164           ; get low address of SID header
                sta SID_HEADER_LO   ; store SID header address lo-byte
