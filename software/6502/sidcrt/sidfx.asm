;-----------------------------------------------------------------------
; FILE sidfx.asm
;
; Written by Wilfred Bos
; SIDFX support routines by Lotus/Ancients
;
; Copyright (c) 2018 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   SIDFX routines for controlling the SIDFX hardware.
;-----------------------------------------------------------------------

; ZERO PAGE ADDRESSES
TEMP = $aa

; detectSidFx
;   output:
;   - AC = detection result (output 0 = no SIDFX, 1 = SIDFX detected)
detectSidFx     ldy #$0f
-               jsr sciSyn      ; bring SIDFX SCI state machine into a known state
                dey
                bpl -

                ldy #$03        ; send login "PNP"
-               lda loginData,y
                jsr sciPut
                dey
                bpl -

                ldy #$03
-               jsr sciGet      ; Read vendor ID (4 bytes)
                cmp vendorId,y
                bne noSidFx
                dey
                bpl -

sidFxFound      lda #$01
                rts

noSidFx         lda #$00
                rts

; configureSidFx
;   input:
;   - AC = required SID model (0 = 8580, 1 = 6581, 4 = stereo)
;          when stereo, bit 0-2 will be interpreted like:
;          100 = stereo, SID 2 @ $D420
;          101 = stereo, SID 2 @ $D500
;          110 = stereo, SID 2 @ $DE00
;   output:
;   - AC = SID model of selected SID (0 = 8580, 1 = 6581, 2 = unknown)
;          if switch 1 is not set to software control:
;            - the output will be the SID model of forced selected SID
;          if switch 1 is set to software control:
;            - if the required SID model cannot be found on SID#1, then it will configure the playback to SID#2 and returns the model of it
;            - if the required SID model is found on SID#1, then it will configure the playback to SID#1 and returns the model of it
;            - if the required SID model is stereo, it will return the SID model of SID#1 and set the correct address of SID#2 socket
configureSidFx  pha
                jsr regUnHide

                jsr getSidFxModel
                tax
                pla
                jsr setSidFxModel
                txa
                tay
                jsr regHide
                tya
                rts

; setSidFxModel
;   input:
;   - AC = required SID model (0 = 8580, 1 = 6581, 4 = stereo; SID2@D420, 5 = stereo; SID2@D500, 6 = stereo; SID2@DE00, 255 = unknown/ambigous, use SID1)
;   - XR = SID model first SID (0 = 8580, 1 = 6581, 2 = unknown)
;   - YR = switch 1 position (0 = center (software control), 1 = left (SID1), 2 = right (SID2))
;   output:
;   - XR = SID model of selected SID
setSidFxModel   cpy #$00                ; is switch set to software control?
                bne +

                cmp #$ff                ; use SID#1 no matter what the configuration is?
                beq switchToSid1

                sta TEMP

                and #$04
                bne setPlaybackMode     ; if bit 2 is set, then set the SIDFX to stereo

                cpx TEMP                ; is SID#1 the required SID model?
                beq switchToSid1

                ; switch to SID#2
                lda #$02
                sta TEMP
                jsr setPlaybackMode

                ldy #$02                ; detect SID#2 model
                jsr detectSfxModel
                tax
+               rts

switchToSid1    lda #$01
                sta TEMP

setPlaybackMode lda $d41d
                and #$FF ^ $07
                ora TEMP
                sta $d41d
                rts

; getSidFxModel
;   output:
;   - AC = SID model of SID#1 or forced selected SID (output 0 = 8580, 1 = 6581, 2 = unknown)
;   - YR = switch 1 position (0 = center (software control), 1 = left (SID1), 2 = right (SID2))
getSidFxModel   lda $d41d
                lsr
                lsr
                lsr
                lsr
                and #$03
                tay             ; y = switch 1 position (0 = center (software control), 1 = left (SID1), 2 = right (SID2))

detectSfxModel  lda $d41e
                cpy #$02        ; is switch 1 set to force SID 2?
                bne +
                lsr
                lsr
+               and #$03
                beq sfxUnknown
                eor #$03
                beq sfxUnknown
                lsr
                rts             ; output 0 = 8580, 1 = 6581

sfxUnknown      lda #$02        ; output 2 = unknown
                rts

sciGet          ldx #$00        ; break loop after 256 tries
-               bit $d41f       ; read SCI ready flag
                bmi +           ; wait until data ready
                dex
                beq -
+
sciSyn          ldx #$0f        ; delay
-               dex
                bpl -
                lda #$00        ; "nop" SCI command

sciPut          ldx #$07        ; transfer 8 bits (MSB first)
                stx $d41e       ; bring SCI sync signal low
-               pha             ; save data byte
                sta $d41f       ; transmit bit 7
                lda $d41f       ; receive bit 0
                ror             ; push bit 0 to carry flag
                pla             ; restore data byte
                rol             ; shift transmitted bit out and received bit in
                dex             ; next bit
                bpl -           ; done?
                stx $d41e       ; bring SCI sync signal high
                rts

regUnHide       lda #$c0        ; unhide register map
                bne +
regHide         lda #$c1        ; hide register map
+               pha
                jsr sciPut
                pla
                eor #$c0 ^ $45
                jsr sciPut
                jmp sciSyn      ; wait for registers to become ready

vendorId        .byte $58, $12, $4c, $45
loginData       .byte $50, $4e, $50, $80