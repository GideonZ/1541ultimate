; GMod2 EEPROM stimulus for gmod2_eeprom_dirty_test.py.
;
; A GMod2 cartridge saves into a serial EEPROM driven bit by bit through its
; register at $DE00. Bit 6 of that register is the EEPROM's chip select and at
; the same time switches the ROM off (all_carts_v5.vhd:326-332), so the code
; that drives the EEPROM cannot run from the cartridge. The ROM at $8000 copies
; it into RAM and jumps into it, the way a GMod2 game does.
;
; The EEPROM runtime is the repository's own, so this drives the hardware
; exactly as the reference code in software/6502 does.
;
; The harness asks for the write rather than having it happen at boot, so it
; can read the EEPROM's state before and after one.
;
;   $0400   $01 once the word has been written
;   $0402   written by the harness to ask for the word
;   $0405   $01 once the routine is up

DONE        = $0400
GO          = $0402
RUNNING     = $0405
PAYLOAD     = $0900
WORD_HIGH   = $00               ; the top two bits of the EEPROM word address
WORD_LOW    = 100               ; and its low eight
VALUE_HIGH  = $a5
VALUE_LOW   = $5a

        * = $8000

        .word coldstart         ; cold start vector
        .word coldstart         ; warm start vector
        .byte $c3, $c2, $cd, $38, $30   ; CBM80, so the KERNAL starts the cartridge

coldstart
        sei
        ldx #$ff
        txs
        cld
        lda #$00
        sta DONE

        ldx #$00                ; two pages, because the runtime is longer than one
copy    lda payload_rom,x
        sta PAYLOAD,x
        lda payload_rom + $100,x
        sta PAYLOAD + $100,x
        inx
        bne copy
        jmp PAYLOAD

payload_rom
        .logical PAYLOAD

        lda #$00
        sta GO
        lda #$01
        sta RUNNING
wait    lda GO
        beq wait

        jsr eeprom_reset
        jsr eeprom_ewen

        ldy #WORD_LOW
        ldx #WORD_HIGH
        jsr eeprom_write_begin
        lda #VALUE_HIGH
        jsr eeprom_write_byte
        lda #VALUE_LOW
        jsr eeprom_write_byte
        jsr eeprom_write_end

        lda #$01
        sta DONE
spin    jmp spin

        .include "../../../../software/6502/unsorted/eeprom.tas"

        .here
        .fill $a000 - *, $ff
