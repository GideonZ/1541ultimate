;-----------------------------------------------------------------------
; FILE memalloc.asm
;
; Written by Wilfred Bos
;
; Copyright (c) 2009 - 2019 Wilfred Bos / Gideon Zweijtzer
;
; DESCRIPTION
;   Calculates the locations in memory for the player, screen and charrom
;   and for advanced player.
;-----------------------------------------------------------------------

; CONSTANTS
SIZE_CHARROM = $04
SIZE_SCREEN = $04
SIZE_PLAYER = $02

; ZERO PAGE ADDRESSES
LOAD_ADDRESS = $ba        ; hi-byte of load address of SID
LOAD_END_ADDRESS = $bb    ; hi-byte of load end address of SID (rounded up)

TEMP = $bc

BANK = $bd
LOOP_INDEX = $be
LOOP_END_INDEX = $bf
LOOP_SIZE = LOOP_END_INDEX              ; safe to be the same location

PAGE_START = $aa
PAGE_SIZE = $ab
PAGE_END = $ac

ZERO_PAGE_ADDRESSES_MEMALLOC = [
    LOAD_ADDRESS,
    LOAD_END_ADDRESS,
    TEMP,
    BANK,
    LOOP_INDEX,
    LOOP_END_INDEX,
    LOOP_SIZE,
    PAGE_START,
    PAGE_SIZE,
    PAGE_END
]

; =========== PLAYER, SCREEN AND CHARROM LOCATION CALCULATION ==================

calcPlayerLocations
                jsr readLoadAddresses

                jsr isRsid
                bne noBasic
                jsr isBasic
                bne noBasic

                lda LOAD_END_ADDRESS
                cmp #$ce
                beq +
                bcs ++
                ; load end address is lower or equal to $CE00
+               lda #$ce            ; player
                jmp ++

+               lda #$04            ; player
+               ldx #$fc            ; screen    ; always map BASIC screen to avoid BASIC programs doing screen writes
                ldy #$f8            ; charrom
                jmp setLocations

noBasic         ; first check areas where default charrom can be used
                ldy #$10            ; charrom ($10 = default location)

                lda LOAD_END_ADDRESS
                cmp #$80
                beq +
                bcs ++
                ; load end address is lower or equal to $8000
+               lda #$9e            ; player
                ldx #$8c            ; screen
                jmp setLocations
+
                cmp #$3a
                beq +
                bcs ++
                ; load end address is lower or equal to $3a00
+               lda #$3a            ; player
                ldx #$3c            ; screen
                jmp setLocations
+
                cmp #$ba
                beq +
                bcs ++
                ; load end address is lower or equal to $ba00
+               lda #$c0            ; player
                ldx #$bc            ; screen
                jmp setLocations
+
                lda LOAD_ADDRESS
                cmp #$0a
                beq +
                bcc ++
                ; load address is higher or equal to $0a00
+               lda #$08            ; player
                ldx #$04            ; screen
                jmp setLocations
+
                cmp #$86
                beq +
                bcc ++
                ; load address is higher or equal to $8600
+               lda #$84            ; player
                ldx #$80            ; screen
                jmp setLocations
+
                ; now check areas where char ROM needs to be copied to
                ; when VIC bank is at $4000-$8000 or $C000-$FFFF than char ROM needs to be copied

                lda LOAD_ADDRESS
                cmp #$4a
                beq +
                bcc ++
                ; load address is higher or equal to $4a00
+               lda #$48            ; player
                ldx #$44            ; screen
                ldy #$40            ; charrom
                jmp setLocations
+
                cmp #$ca
                beq +
                bcc ++
                ; load address is higher or equal to $ca00
+               lda #$c8            ; player
                ldx #$c4            ; screen
                ldy #$c0            ; charrom
                jmp setLocations
+
                lda LOAD_END_ADDRESS
                cmp #$76
                beq +
                bcs ++
                ; load end address is lower or equal to $7600
+               lda #$76            ; player
                ldx #$7c            ; screen
                ldy #$78            ; charrom
                jmp setLocations
+
                cmp #$f8
                beq +
                bcs ++
                ; load end address is lower or equal to $f800
                lda LOAD_ADDRESS
                cmp #$06
                beq +
                bcc ++
                ; load address is higher or equal to $0600
+               lda #$04            ; player
                ldx #$fc            ; screen
                ldy #$f8            ; charrom
                jmp setLocations
+
                ; no screen possible
                lda LOAD_ADDRESS
                cmp #$06
                beq +
                bcc ++
                ; load address is higher or equal to $0600
+               lda #$04            ; player
                ldx #$00            ; no screen
                ldy #$00            ; no charrom
                jmp setLocations
+
                cmp #$ce
                beq +
                bcs ++
                ; load address is lower or equal to $ce00
+               lda #$ce            ; player
                ldx #$00            ; no screen
                ldy #$00            ; no charrom
                jmp setLocations
+
                ; no player, screen or charrom possible
                lda #$00            ; no player
                ldx #$00            ; no screen
                ldy #$00            ; no charrom
                jmp setLocations

readLoadAddresses
                ldy #$7f            ; read hi byte of load end address
                jsr readHeader
                sta LOAD_END_ADDRESS

                ldy #$7e            ; read lo byte of load end address
                jsr readHeader
                cmp #$00
                beq +
                inc LOAD_END_ADDRESS
    +
                ldy #$7d            ; read hi byte of load address
                jsr readHeader
                sta LOAD_ADDRESS
                rts

; setLocations: sets the hi-byte for the player, screen and charrom locations
; input:
;   AC: player
;   XR: screen
;   YR: charrom
setLocations    stx SCREEN_LOCATION
                sty CHARROM_LOCATION
                sta PLAYER_LOCATION
                rts

calcSmallestPlayerLocation
                lda #$00
                sta SCREEN_LOCATION
                sta PLAYER_LOCATION
                lda #$10
                sta CHARROM_LOCATION

                jsr readLoadAddresses

                jsr getFreePageSize
                cmp #$02
                bcc +

                ; set player location to start of free page area
                jsr getFreePageBase

                sta PLAYER_LOCATION
                jmp setScreenArea
+
                lda LOAD_ADDRESS
                cmp #$06
                bcc +

                lda #$04
                sta PLAYER_LOCATION
                jmp setScreenArea
+
                lda LOAD_END_ADDRESS
                cmp #$ce
                beq +
                bcs setScreenArea

+               lda #$ce
                sta PLAYER_LOCATION

setScreenArea   lda LOAD_END_ADDRESS
                cmp #$d0
                beq +
                bcs ++

+               lda #$dc
                sta SCREEN_LOCATION
                lda #$d8
                sta CHARROM_LOCATION
                rts

+               lda LOAD_END_ADDRESS
                cmp #$f8
                beq +
                bcs ++

+               lda #$fc
                sta SCREEN_LOCATION
                lda #$f8
                sta CHARROM_LOCATION
+               rts

checkAreaDefaultCharSet
                lda LOOP_INDEX      ; check if PAGE_START is in area
                and #$f0
                sta BANK
                clc
                adc #$0c            ; check if relocation area is set to: bank + $0c00
                cmp PAGE_START
                bne ++

                clc
                adc #$18            ; check if relocation area end is greater or equal than: bank + $0c00 + $1800
                cmp PAGE_END
                beq +
                bcc ++
+
                ; never set screenLocation between $1000-$2000 or $9000-$a000
                lda #$10            ; default charactor ROM
                sta CHARROM_LOCATION

                lda BANK
                clc
                adc #$0c            ; set player location to: bank + $0c00
                sta PLAYER_LOCATION
                clc
                adc #$14            ; set screen location to: bank + $2000
                sta SCREEN_LOCATION
                rts
+
                lda BANK
                clc
                adc #$0c            ; check if relocation area is less than: bank + $0c00
                cmp PAGE_START
                beq +
                bcc defaultNotPossible
+
                lda PAGE_SIZE
                cmp #$06
                beq +
                bcc defaultNotPossible
+
                lda PAGE_START
                cmp LOOP_INDEX
                beq setDefaultCharLoc
                bcs setDefaultCharLoc

                lda LOOP_INDEX
                clc
                adc #$06
                cmp PAGE_END
                beq setDefaultCharLoc
                bcs defaultNotPossible

setDefaultCharLoc
                lda PAGE_START
                cmp LOOP_INDEX
                bcc +
                lda PAGE_START
                sta LOOP_INDEX
+
                jmp setLocationsDefaultChar

defaultNotPossible
                lda PAGE_START
                sta LOOP_INDEX

                lda PAGE_SIZE
                sta LOOP_SIZE

                lda BANK
                clc
                adc #$1e            ; check if relocation area is less than: bank + $1e00
                cmp PAGE_START
                bcc ++

                clc
                adc #$06            ; check if relocation area is greater than: bank + $1e00 + $0600
                cmp PAGE_END
                beq +
                bcs ++
+
                lda BANK
                clc
                adc #$1e
                sta LOOP_INDEX

                LDA PAGE_END
                sec
                sbc LOOP_INDEX
                sta LOOP_SIZE
+
                lda BANK
                clc
                adc #$1e
                cmp LOOP_INDEX
                beq +
                bcs defaultNotPossible2
+
                clc
                adc #$1e
                cmp LOOP_INDEX
                beq +
                bcc defaultNotPossible2
+
                lda LOOP_SIZE
                cmp #$06
                beq +
                bcc defaultNotPossible2
+
                jmp setLocationsDefaultChar

defaultNotPossible2
                rts

setLocationsDefaultChar
                lda #$10            ; default charactor ROM
                sta CHARROM_LOCATION

                lda LOOP_INDEX
                tay
                and #$02
                beq +

                tya
                sta PLAYER_LOCATION
                clc
                adc #SIZE_PLAYER
                sta SCREEN_LOCATION
                rts
+
                tya
                sta SCREEN_LOCATION
                clc
                adc #SIZE_SCREEN
                sta PLAYER_LOCATION
                rts

checkAreaWithCharSet
                lda LOOP_INDEX      ; check if PAGE_START is in area
                cmp PAGE_START
                beq +
                bcc +
                clc
                adc #$0a
                cmp PAGE_END        ; check if PAGE_END in area
                beq +
                bcc +
notPossible     rts

+               lda PAGE_SIZE
                cmp #$0a
                beq +
                bcc notPossible
+               lda LOOP_INDEX
                clc
                adc #$36
                cmp PAGE_START
                beq +
                bcc notPossible
+
                lda PAGE_START
                cmp LOOP_INDEX
                bcc +
                sta LOOP_INDEX
+
                lda LOOP_INDEX
                tay
                and #$06
                bne +

                tya
                sta CHARROM_LOCATION
                clc
                adc #SIZE_CHARROM
                sta SCREEN_LOCATION
                clc
                adc #SIZE_SCREEN
                sta PLAYER_LOCATION
                rts
+
                tya
                and #$02
                bne +
                tya
                sta SCREEN_LOCATION
                clc
                adc #SIZE_SCREEN
                sta CHARROM_LOCATION
                clc
                adc #SIZE_CHARROM
                sta PLAYER_LOCATION
                rts
+
                tya
                clc
                adc #$02
                and #$06
                bne +
                tya
                sta PLAYER_LOCATION
                clc
                adc #SIZE_PLAYER
                sta CHARROM_LOCATION
                clc
                adc #SIZE_CHARROM
                sta SCREEN_LOCATION
                rts
+
                tya
                sta PLAYER_LOCATION
                clc
                adc #SIZE_PLAYER
                sta SCREEN_LOCATION
                clc
                adc #SIZE_SCREEN
                sta CHARROM_LOCATION
                rts

calcPlayerScreenLocForRelocArea
                jsr getFreePageBase
                sta PAGE_START

                and #$01
                sta TEMP

                jsr getFreePageSize
                sta PAGE_SIZE

                cmp #$02
                beq tooSmall
                bcc tooSmall

                ; align to block of $0200
                lda PAGE_START
                clc
                adc TEMP
                sta PAGE_START

                lda PAGE_SIZE
                sec
                sbc TEMP
                sta PAGE_SIZE

                lda PAGE_START
                clc
                adc PAGE_SIZE
                sta PAGE_END

                lda #$04
                sta LOOP_INDEX
                jsr checkAreaDefaultCharSet

                lda PLAYER_LOCATION
                bne playerLocFound

                lda #$80
                sta LOOP_INDEX
                jsr checkAreaDefaultCharSet

                lda PLAYER_LOCATION
                bne playerLocFound

                lda #$40
                sta LOOP_INDEX
                jsr checkAreaWithCharSet

                lda PLAYER_LOCATION
                bne playerLocFound

                lda #$c0
                sta LOOP_INDEX
                jsr checkAreaWithCharSet

                lda PLAYER_LOCATION
                bne playerLocFound

tooSmall        jmp calcSmallestPlayerLocation
playerLocFound
                rts

getFreePageBase ldy #$78            ; get free page base
                jmp readHeader

getFreePageSize ldy #$79            ; get free page size
                jmp readHeader

; =========== ADVANCED PLAYER LOCATION CALCULATION ==================

calcExtraPlayerLocForRelocArea
                jsr getFreePageBase
                sta LOOP_INDEX

                jsr getFreePageSize
                cmp EXTRA_PLAYER_SIZE
                bcc noExtraPlayer
                beq noExtraPlayer

                sec
                sbc EXTRA_PLAYER_SIZE
                clc
                adc LOOP_INDEX
                sta LOOP_END_INDEX

loopFreePages   jsr checkIfAllFit
                bne checkNext

                ; advanced player location found
                lda LOOP_INDEX
                sta EXTRA_PLAYER_LOCATION
                rts

checkNext       inc LOOP_INDEX
                lda LOOP_INDEX
                cmp LOOP_END_INDEX
                bcc loopFreePages
                beq loopFreePages

checkIfAllFit   lda PLAYER_LOCATION
                ldx #$02            ; player size
                jsr checkIfItFits
                bne noFit

                lda SCREEN_LOCATION
                beq +               ; skip when there is no screen space
                ldx #$04            ; screen size
                jsr checkIfItFits
                bne noFit
+
                lda CHARROM_LOCATION
                cmp #$10
                beq +               ; skip when default character set
                ldx #$04            ; charrom size
                jsr checkIfItFits
                bne noFit

+               lda #$00            ; it all fits
noFit           rts

checkIfItFits   stx TEMP
                tay
                cmp EXTRA_PLAYER_SIZE
                bcc ++

                sec
                sbc EXTRA_PLAYER_SIZE
                cmp LOOP_INDEX
                beq +
                bcc ++
+               lda #$00
                rts

+               tya
                clc
                adc TEMP                  ; size of area
                cmp LOOP_INDEX
                beq +
                bcs nofit2
+               lda #$00
                rts

noFit2          lda #$01
                rts

noExtraPlayer   lda #$00                ; advanced player is not possible
                sta EXTRA_PLAYER_LOCATION
                rts

calcExtraPlayerLocation
                jsr readLoadAddresses

                lda #$04
                sta LOOP_INDEX

                lda LOAD_ADDRESS
                cmp EXTRA_PLAYER_SIZE
                bcc noSpaceBeforeLoad
                sec
                sbc EXTRA_PLAYER_SIZE
                cmp LOOP_INDEX          ; check if end index is not lower than begin index
                bcc noSpaceBeforeLoad
                sta LOOP_END_INDEX

                lda LOOP_INDEX
                cmp LOOP_END_INDEX
                beq noSpaceBeforeLoad
                bcs noSpaceBeforeLoad

                jsr findExtraPlayerLoc

                lda EXTRA_PLAYER_LOCATION
                bne +

noSpaceBeforeLoad
                lda LOAD_END_ADDRESS
                sta LOOP_INDEX

                lda #$d0
                sec
                sbc EXTRA_PLAYER_SIZE
                sta LOOP_END_INDEX

                cmp LOOP_INDEX
                bcc noExtraPlayer

                jsr findExtraPlayerLoc
+               rts

findExtraPlayerLoc
loopLocations
                jsr checkIfAllFit
                bne checkNext2

                lda LOOP_INDEX
                clc
                adc EXTRA_PLAYER_SIZE
                cmp #$a0                ; is end of advanced player before BASIC ROM location?
                beq epLocFound
                bcc epLocFound

                ; now check if the advanced player can be installed between $C000-$D000
                cmp #$d0
                beq +
                bcs checkNext2
+
                lda LOOP_INDEX
                cmp #$c0
                beq epLocFound
                bcc checkNext2
epLocFound
                ; advanced player location found
                lda LOOP_INDEX
                sta EXTRA_PLAYER_LOCATION
                rts

checkNext2      inc LOOP_INDEX
                lda LOOP_INDEX
                cmp LOOP_END_INDEX
                bcc loopLocations
                beq loopLocations

                jmp noExtraPlayer
