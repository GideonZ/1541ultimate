; UCI stimulus for easyflash_cartridge_test.py.
;
; Sends the control target's IDENTIFY through the UCI at $DE1C and copies the
; first byte of the answer to UCI_ID. Both waits give up after 65536 reads, so
; without a UCI the byte stays blank and the fragment still ends.
;
; Not a subroutine: it runs inside the routine the cartridge copies to RAM, and
; every path falls through to `done`, where the routine goes on. START_ADDRESS
; is where it runs in RAM, not where it is stored in the ROM.

        .cpu "6502"
        * = START_ADDRESS

        ldx #$00
        ldy #$00
wait_idle
        lda $de1c
        and #$35
        beq request
        dex
        bne wait_idle
        dey
        bne wait_idle
        jmp done

request
        lda #$04                ; control target
        sta $de1d
        lda #$01                ; IDENTIFY
        sta $de1d
        lda #$01                ; push the command
        sta $de1c

        ldx #$00
        ldy #$00
wait_data
        lda $de1c
        and #$20
        bne answer
        dex
        bne wait_data
        dey
        bne wait_data
        jmp done

answer
        lda $de1e               ; first data byte
        sta UCI_ID
        lda #$02                ; acknowledge
        sta $de1c
done
