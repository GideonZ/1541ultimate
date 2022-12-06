        .page  
        .subttl 'addfil.src'          
; add file to directory

addfil  lda  sa         ; save variables 
        pha      
        lda  lindx       
        pha      
        lda  sector      
        pha      
        lda  track       
        pha      
        lda  #irsa       
        sta  sa          
        jsr  curblk     ; use last accessed search
        lda  type        
        pha      
        lda  fildrv      
        and  #1          
        sta  drvnum      
        ldx  jobnum      
        eor  lstjob,x    
        lsr  a   
        bcc  af08       ; same drive as required

        ldx  #1          
        stx  delind     ; look for deleted entry
        jsr  srchst      
        beq  af15       ; all full, new sector 
        bne  af20       ; found one

af08    lda  delsec      
        beq  af10       ; deleted entry not located
        cmp  sector      
        beq  af20       ; sector is resident
        sta  sector      
        jsr  drtrd      ; read sector in
        jmp  af20        

af10    lda  #1         ; find deleted entry
        sta  delind      
        jsr  search      
        bne  af20        
af15    jsr  nxdrbk     ; all full, new sector
        lda  sector      
        sta  delsec      
        lda  #2          
        sta  delind      
af20    lda  delind      
        jsr  setpnt      
        pla      
        sta  type       ; set type
        cmp  #reltyp     
        bne  af25        
        ora  #$80        
af25             
        jsr  putbyt      
        pla      
        sta  filtrk     ; ...table & entry
        jsr  putbyt      
        pla      
        sta  filsec     ; set sector link in...
        jsr  putbyt     ; ...table & entry
        jsr  getact      
        tay      
        lda  filtbl      
        tax      
        lda  #16         
        jsr  trname     ; transfer name
        ldy  #16         
        lda  #0         ; clear # of blocks &...
af30    sta  (dirbuf),y ; ...& replace links
        iny      
        cpy  #27         
        bcc  af30        
        lda  type       ; a relative file ?
        cmp  #reltyp     
        bne  af50       ; no
        ldy  #16        ; yes
        lda  trkss      ; get ss track
        sta  (dirbuf),y ; put in directory
        iny      
        lda  secss      ; get ss sector
        sta  (dirbuf),y ; put in
        iny      
        lda  rec        ; get record size
        sta  (dirbuf),y          
af50    jsr  drtwrt     ; write it out
        pla      
        sta  lindx       
        tax      
        pla      
        sta  sa          
        lda  delsec      
        sta  entsec      
        sta  dsec,x      
        lda  delind      
        sta  entind      
        sta  dind,x      
        lda  type        
        sta  pattyp      
        lda  drvnum      
        sta  fildrv      
        rts      
        .page
        .subttl 'addrel.src'          
;*********************************
;* addrel: add blocks to relative*
;*         file.                 *
;*   vars:                       *
;*   regs:                       *
;*                               *
;*********************************

addrel           
        jsr  setdrn      
        jsr  ssend      ; set up end of file
        jsr  posbuf      
        jsr  dbset       
        lda  ssind       
        sta  r1         ; save ss index
        lda  ssnum       
        sta  r0         ; save ss number
        lda  #0          
        sta  r2         ; clear flag for one block

        lda  #0         ; clear for calculation...
        sta  recptr     ; ...to 1st byte in record
        jsr  fndrel     ; calc ss ptrs
addr1           	;  entry for rel record fix
        jsr  numfre     ; calc available...

        ldy  lindx      ; record span?
        ldx  rs,y        
        dex      
        txa      
        clc      
        adc  relptr      
        bcc  ar10       ; no span

        inc  ssind      ; inc ss ptrs & check
        inc  ssind      ; inc ss ptrs & check
        bne  ar10        
        inc  ssnum       
        lda  #ssioff     
        sta  ssind       
ar10             
        lda  r1          
        clc      
        adc  #2          
        jsr  setssp      

        lda  ssnum       
        cmp  #nssl       
        bcc  ar25       ; valid range

ar20             
        lda  #bigfil     
        jsr  cmderr     ; too many ss's
ar25             
        lda  ssind      ; calc # blocks needed...
        sec     	; ...& check against avail.
        sbc  r1          
        bcs  ar30        
        sbc  #ssioff-1   
        clc      
ar30             
        sta  t3         ; # ss indices
        lda  ssnum       
        sbc  r0          
        sta  t4         ; # ss needed

        ldx  #0         ; clear accum.
        stx  t1          
        stx  t2          
        tax     	; .x=# ss
        jsr  sscalc     ; calc # of blocks needed

        lda  t2          
        bne  ar35        
        ldx  t1          
        dex      
        bne  ar35        

        inc  r2          
ar35             
        cmp  nbtemp+1    
        bcc  ar40       ; ok!!
        bne  ar20        
        lda  nbtemp      
        cmp  t1          
        bcc  ar20       ; not enuf blocks
ar40             
        lda  #1          
        jsr  drdbyt     ; look at sector link
        clc      
        adc  #1         ; +1 is nr
        ldx  lindx       
        sta  nr,x        
        jsr  nxtts      ; get next block...
        jsr  setlnk     ; ...& set link.
        lda  r2          
        bne  ar50       ; add one block

        jsr  wrtout     ; write current last rec
ar45             
        jsr  dblbuf     ; switch bufs
        jsr  sethdr     ; set hdr from t & s
        jsr  nxtts      ; get another
        jsr  setlnk     ; set up link
        jsr  nulbuf     ; clean it out
        jmp  ar55        
ar50             
        jsr  dblbuf     ; switch bufs
        jsr  sethdr     ; set hdr from t & s
        jsr  nulbuf     ; clean buffer
        jsr  nullnk     ; last block =0,lstchr
ar55             
        jsr  wrtout     ; write buffer
        jsr  getlnk     ; get t&s from link
        lda  track       
        pha     	; save 'em
        lda  sector      
        pha      
        jsr  gethdr     ; now get hdr t&s
        lda  sector      
        pha     	; save 'em
        lda  track       
        pha      
        jsr  gsspnt     ; check ss ptr
        tax      
        bne  ar60        

        jsr  newss      ; need another ss
        lda  #ssioff     
        jsr  setssp     ; .a=bt val
        inc  r0         ; advance ss count
ar60             
        pla      
        jsr  putss      ; record t&s...
        pla      
        jsr  putss      ; ...in ss.
        pla     	; get t&s from link
        sta  sector      
        pla      
        sta  track       
        beq  ar65       ; t=0: that's all!!

        lda  r0          
        cmp  ssnum       
        bne  ar45       ; not even done yet

        jsr  gsspnt      
        cmp  ssind       
        bcc  ar45       ; almost done
        beq  ar50       ; one more block left
ar65             
        jsr  gsspnt      
        pha      
        lda  #0          
        jsr  ssdir       
        lda  #0          
        tay      
        sta  (dirbuf),y          
        iny      
        pla      
        sec      
        sbc  #1          
        sta  (dirbuf),y          
        jsr  wrtss      ; write ss
        jsr  watjob      
        jsr  mapout      
        jsr  fndrel      
        jsr  dblbuf     ; get back to leading buffer
        jsr  sspos       
        bvs  ar70        
        jmp  positn      
ar70             
        lda  #lrf        
        jsr  setflg      
        lda  #norec      
        jsr  cmderr      
        .page  
        .subttl 'block.src'

; rom 1.1 additions
; user commands

user    ldy  cmdbuf+1    
        cpy  #'0         
        bne  us10       ; 0 resets pntr

usrint  jmp  burst_routines
	nop		; fill
	nop		; fill
	nop		; fill
	nop		; fill
	nop		; fill
	nop		; fill

;       lda  #<ublock   ; set default block add
;       sta  usrjmp      
;       lda  #>ublock    
;       sta  usrjmp+1    
;       rts      

us10    jsr  usrexc     ; execute code by table
        jmp  endcmd      

usrexc  dey     	; entry is(((index-1)and$f)*2)
        tya      
        and  #$f         
        asl  a   
        tay      

	lda  (usrjmp),y          
        sta  ip          
        iny      
        lda  (usrjmp),y          
        sta  ip+1        

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch53	; *** rom ds 05/21/85 ***
;       jmp  (ip)        

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>
        .page  
;  open direct access buffer
;  from open "#"

opnblk  lda  lstdrv      
        sta  drvnum      
        lda  sa         ; sa is destroyed by this patch
        pha      
        jsr  autoi      ; init disk for proper channel assignment
        pla     	; restore sa
        sta  sa          
        ldx  cmdsiz      
        dex      
        bne  ob10        

        lda  #1         ; get any buffer
        jsr  getrch      
        jmp  ob30        

ob05    lda  #nochnl     
        jmp  cmderr      

ob10    ldy  #1         ; buffer # is requested
        jsr  bp05        
        ldx  filsec      
        cpx  #bfcnt     ; must be less than 6.
        bcs  ob05        

        lda  #0          
        sta  temp        
        sta  temp+1      
        sec      

ob15             
        rol  temp        
        rol  temp+1      
        dex      
        bpl  ob15        

        lda  temp        
        and  bufuse      
        bne  ob05       ; buffer is used
        lda  temp+1      
        and  bufuse+1    
        bne  ob05       ; buf is used

        lda  temp        
        ora  bufuse     ; set buffer as used
        sta  bufuse      
        lda  temp+1      
        ora  bufuse+1    
        sta  bufuse+1    

        lda  #0         ; set up channel
        jsr  getrch      
        ldx  lindx       
        lda  filsec      
        sta  buf0,x      
        tax      
        lda  drvnum      
        sta  jobs,x      
        sta  lstjob,x    

ob30    ldx  sa          
        lda  lintab,x   ; set lindx table
        ora  #$40        
        sta  lintab,x    

        ldy  lindx       
        lda  #$ff        
        sta  lstchr,y    

        lda  #rndrdy     
        sta  chnrdy,y   ; set channel ready

        lda  buf0,y      
        sta  chndat,y   ; buffer # as 1st char
        asl  a   
        tax      
        lda  #1          
        sta  buftab,x    
        lda  #dirtyp+dirtyp      
        sta  filtyp,y   ; set direct file type
        jmp  endcmd      
        .page    

; block commands

block   ldy  #0          
        ldx  #0          
        lda  #'-        ; "-" separates cmd from subcmd
        jsr  parse      ; locate sub-cmd
        bne  blk40       

blk10   lda  #badcmd     
        jmp  cmderr      

blk30   lda  #badsyn     
        jmp  cmderr      

blk40   txa      
        bne  blk30       

        ldx  #nbcmds-1	; find command
        lda  cmdbuf,y    
blk50   cmp  bctab,x     
        beq  blk60       
        dex      
        bpl  blk50       
        bmi  blk10       

blk60            
        txa      
        ora  #$80        
        sta  cmdnum      
        jsr  blkpar     ; parse parms

        lda  cmdnum      
        asl  a   
        tax      
        lda  bcjmp+1,x   
        sta  temp+1      
        lda  bcjmp,x     
        sta  temp        

        jmp  (temp)     ; goto command

bctab    .byte  'AFRWEP'         
nbcmds   =*-bctab        

bcjmp    .word blkalc   ; block-allocate
	 .word blkfre	; block-free
	 .word blkrd    ; block-read
	 .word blkwt    ; block-write
	 .word blkexc   ; block-execute
	 .word blkptr   ; block-pointer

blkpar  ldy  #0         ; parse block parms
        ldx  #0          
        lda  #':         
        jsr  parse       
        bne  bp05       ; found ":"

        ldy  #3         ; else char #3 is beginning
bp05    lda  cmdbuf,y    
        cmp  #'          
        beq  bp10        

        cmp  #29        ; skip character
        beq  bp10        

        cmp  #',         
        bne  bp20        

bp10    iny      
        cpy  cmdsiz      
        bcc  bp05        

        rts     	; that's all

bp20    jsr  aschex      
        inc  f1cnt       
        ldy  f2ptr       
        cpx  #mxfils-1   
        bcc  bp10        

        bcs  blk30      ; bad syntax

;  convert ascii to hex (binary)
;  & store conversion in tables
;  .y= ptr into cmdbuf
aschex  lda  #0          
        sta  temp        
        sta  temp+1      
        sta  temp+3      

        ldx  #$ff        
ah10    lda  cmdbuf,y   ; test for dec #
        cmp  #$40        
        bcs  ah20       ; non-numeric terminates
        cmp  #$30        
        bcc  ah20       ; non-numeric

        and  #$f         
        pha      
        lda  temp+1     ; shift digits (*10)
        sta  temp+2      
        lda  temp        
        sta  temp+1      
        pla      
        sta  temp        
        iny      
        cpy  cmdsiz      
        bcc  ah10       ; still in string

ah20    sty  f2ptr      ; convert digits to...
        clc     	; ...binary by dec table
        lda  #0          

ah30    inx      
        cpx  #3          
        bcs  ah40        

        ldy  temp,x      
ah35    dey      
        bmi  ah30        

        adc  dectab,x    
        bcc  ah35        

        clc      
        inc  temp+3      
        bne  ah35        

ah40    pha      
        ldx  f1cnt       
        lda  temp+3      
        sta  filtrk,x   ; store result in table
        pla      
        sta  filsec,x    
        rts      

dectab   .byte 1,10,100 ; decimal table

;block-free
blkfre  jsr  blktst      
        jsr  frets       
        jmp  endcmd      

;block-allocate

        lda  #1          
        sta  wbam        
blkalc           
        jsr  blktst      

ba10             
        lda  sector      
        pha      
        jsr  getsec      
        beq  ba15       ; none greater on this track
        pla      
        cmp  sector      
        bne  ba30       ; requested sector not avail
        jsr  wused       
        jmp  endcmd      

ba15             
        pla     	; pop stack
ba20             
        lda  #0          
        sta  sector      
        inc  track       
        lda  track       
        cmp  maxtrk      
        bcs  ba40       ; gone all the way

        jsr  getsec      
        beq  ba20        
ba30             
        lda  #noblk      
        jsr  cmder2      
ba40             
        lda  #noblk      
        jsr  cmderr     ; t=0,s=0 :none left


; block read subs
blkrd2  jsr  bkotst     ; test parms
        jmp  drtrd       

getsim  jsr  getpre     ; get byte w/o inc
        lda  (buftab,x)          
        rts      

; block read
blkrd3  jsr  blkrd2      
        lda  #0          
        jsr  setpnt      
        jsr  getsim     ; y=lindx
 

        sta  lstchr,y    
        lda  #rndrdy     
        sta  chnrdy,y    
        rts      
blkrd            
        jsr  blkrd3      
        jsr  rnget1      
        jmp  endcmd      

;user direct read, lstchr=$ff
ublkrd           
        jsr  blkpar      
        jsr  blkrd3      
        lda  lstchr,y    
        sta  chndat,y    
        lda  #$ff        
        sta  lstchr,y    
        jmp  endcmd     ; (rts)

;block-write
blkwt   jsr  bkotst      

        jsr  getpnt      
        tay      
        dey      
        cmp  #2          
        bcs  bw10        
        ldy  #1          

bw10    lda  #0         ; set record size
        jsr  setpnt      
        tya      
        jsr  putbyt      
        txa      
        pha      

bw20    jsr  drtwrt     ; write block
        pla      
        tax      

;       jsr  rnget2      
	jsr  ptch15	; fix for block read *rom ds 01/22/85* 

        jmp  endcmd      

;user dirct write, no lstchr
ublkwt  jsr  blkpar      
        jsr  bkotst      
        jsr  drtwrt      
        jmp  endcmd      

;block-execute
blkexc           
        jsr  killp      ; kill protect
        jsr  blkrd2     ; read block & execute
        lda  #0          

be05    sta  temp        
        ldx  jobnum      
        lda  bufind,x    
        sta  temp+1      
        jsr  be10       ; indirect jsr
        jmp  endcmd      

be10    jmp  (temp)      

;buffer-pointer, set buffer pointer 
blkptr  jsr  buftst      
        lda  jobnum      
        asl  a   
        tax      
        lda  filsec+1    
        sta  buftab,x    
        jsr  getpre      
        jsr  rnget2     ; set up get
        jmp  endcmd      

;test for allocated buffer..
;  ..related to sa
buftst  ldx  f1ptr       
        inc  f1ptr       
        lda  filsec,x    
        tay      
        dey      
        dey      
        cpy  #$c        ;  set limit to # of sas 
        bcc  bt20        

bt15    lda  #nochnl     
        jmp  cmderr      

bt20    sta  sa          
        jsr  fndrch      
        bcs  bt15        
        jsr  getact      
        sta  jobnum      
        rts      

;test block operation parms
bkotst  jsr  buftst      
;
;test for legal block &..
;  ..set up drv, trk, sec
blktst  ldx  f1ptr       
        lda  filsec,x    
        and  #1          
        sta  drvnum      
        lda  filsec+2,x          
        sta  sector      
        lda  filsec+1,x          
        sta  track       
bt05             
        jsr  tschk       
        jmp  setlds     ; (rts)

; rsr 1/19/80 add autoi to #cmd
        .page
	.subttl 'burst'
	*=$8000

signature_lo	*=*+1	; <<< TO BE DETERMINED
signature_hi	*=*+1	; <<< TO BE DETERMINED

	.byte  'S/W - DAVID G SIRACUSA',$0D,"H/W - GREG BERLIN",$0D,'1985',$0D

burst_routines

	lda  cmdsiz     ; check command size
	cmp  #3
	bcc  realus

	lda  cmdbuf+2	; get command
	sta  switch	; save info
	and  #$1f
	tax		; command info
	asl  a
	tay
	lda  cmdtbb,y
	sta  ip
	lda  cmdtbb+1,y
	sta  ip+1
	cpx  #30	; utload ok for 1541 mode
	beq  1$

	lda  pota1
	and  #$20	; 1/2 Mhz ?
	beq  realus	; 1541 mode...ignore

1$	lda  fastsr	; clear clock & error return
	and  #$eb
	sta  fastsr

	lda  cmdctl,x   ; most sig bit set set error recover
	sta  cmdbuf+2	; save info here

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch65	
;	jmp  (ip)	; *** rom ds 04/25/86 ***

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

realus  lda  #<ublock   ; set default block add
        sta  usrjmp      
        lda  #>ublock    
        sta  usrjmp+1    
unused  rts      

	.page

; command tables and information

cmdctl   .byte  $80,$81,$90,$91,$b0,$b1,$f0,$f1,$00,$01,$B0,$01,$00,$01,$00,$01
         .byte  $80,$81,$90,$91,$b0,$b1,$f0,$f1,$00,$01,$B0,$01,$00,$01,$00,$80

cmdtbb   .word    fstrd  	 ; fast read drv #0 - 0000
	 .word    ndkrd 	 ; fast read drv #1 - 0001

	 .word    fstwrt         ; fast write drv #0 - 0010
	 .word    ndkwrt	 ; fast write drv #1 - 0011

         .word    fstsek         ; seek disk drv #0 - 0100
         .word    ndkrd          ; seek disk drv #1 - 0101

         .word    fstfmt         ; format disk drv #0 - 0110
         .word    fstfmt         ; format disk drv #1 - 0111

         .word    cpmint	 ; interleave disk drv #0 - 1000
         .word    cpmint	 ; interleave disk drv #1 - 1001

	 .word    querdk	 ; query disk format - 1010
         .word    ndkrd          ; seek disk drv #1 - 1011

	 .word    inqst		 ; return disk status - 1100
	 .word    ndkrd		 ; return disk status - 1101

	 .word    duplc1	 ; backup drv0 to drv1 - 1110
	 .word    duplc1	 ; backup drv1 to drv0 - 1111

; *****************************************************************
	
	 .word    fstrd  	 ; fast read drv #0 - 0000
	 .word    ndkrd 	 ; fast read drv #1 - 0001

	 .word    fstwrt         ; fast write drv #0 - 0010
	 .word    ndkwrt	 ; fast write drv #1 - 0011

         .word    fstsek         ; seek disk drv #0 - 0100
         .word    ndkrd          ; seek disk drv #1 - 0101

         .word    fstfmt
         .word    fstfmt

         .word    unused
         .word    unused

	 .word    querdk	 ; query disk format - 1010
         .word    ndkrd          ; seek disk drv #1 - 1011

         .word    unused
         .word    unused

         .word    chgutl
         .word    fstload
        .page 
        .subttl 'close.src'   
; close the file associated with sa

close            
        lda  #0          
        sta  wbam        
        lda  sa          
        bne  cls10      ; directory close
        lda  #0          
        sta  dirlst     ; clear dir list
        jsr  frechn      
cls05            
        jmp  freich      
cls10            
        cmp  #$f         
        beq  clsall     ; close cmd chanl
        jsr  clschn     ; close channel
        lda  sa          
        cmp  #2          
        bcc  cls05       

        lda  erword      
        bne  cls15      ; last command had an error
        jmp  endcmd      
cls15            
        jmp  scren1      

clsall           
        lda  #14         
        sta  sa          
cls20            
        jsr  clschn      
        dec  sa          
        bpl  cls20       
        lda  erword      
        bne  cls25      ;  last command had an error
        jmp  endcmd      
cls25            
        jmp  scren1      

clschn           
        ldx  sa          
        lda  lintab,x    
        cmp  #$ff        
        bne  clsc28      
        rts      
clsc28           
        and  #$f         
        sta  lindx       

        jsr  typfil      
        cmp  #dirtyp     
        beq  clsc30     ; direct channel
        cmp  #reltyp     
        beq  clsrel      

        jsr  fndwch     ; look for write channel
        bcs  clsc31      

        jsr  clswrt     ; close seq write
        jsr  clsdir     ; close directory
clsc30           
        jsr  mapout     ; write bam
clsc31           
        jmp  frechn      

clsrel           
        jsr  scrub       
        jsr  dblbuf      
        jsr  ssend       
        ldx  ssnum       
        stx  t4          
        inc  t4          
        lda  #0          
        sta  t1          
        sta  t2          
        lda  ssind       
        sec      
        sbc  #ssioff-2   
        sta  t3          
        jsr  sscalc      
        ldx  lindx       
        lda  t1          
        sta  nbkl,x      
        lda  t2          
        sta  nbkh,x      
        lda  #dyfile     
        jsr  tstflg      
        beq  clsr1       
        jsr  clsdir      
clsr1   jmp  frechn      

; close a write chanl

clswrt          	; close seq write file
        ldx  lindx       
        lda  nbkl,x      
        ora  nbkh,x      
        bne  clsw10     ; at least 1 block written

        jsr  getpnt      
        cmp  #2          
        bne  clsw10     ; at least 1 byte written

        lda  #cr         
        jsr  putbyt      
clsw10           
        jsr  getpnt      
        cmp  #2          
        bne  clsw20     ; not mt buffer

        jsr  dblbuf     ; switch bufs

        ldx  lindx       
        lda  nbkl,x      
        bne  clsw15      
        dec  nbkh,x      
clsw15           
        dec  nbkl,x      

        lda  #0          
clsw20           
        sec      
        sbc  #1         ; back up 1
        pha     	; save it
        lda  #0          
        jsr  setpnt      
        jsr  putbyt     ; tlink=0
        pla     	; lstchr count
        jsr  putbyt      

        jsr  wrtbuf     ; write out last buffer
        jsr  watjob     ; finish job up
        jmp  dblbuf     ; make sure both bufs ok

; directory close on open write file

clsdir  ldx  lindx      ; save lindx
        stx  wlindx     ; &sa
        lda  sa          
        pha      
        lda  dsec,x     ; get directory sector
        sta  sector      
        lda  dind,x     ; get sector offset
        sta  index       
        lda  filtyp,x   ; drv # in filtyp
        and  #1          
        sta  drvnum      
        lda  dirtrk      
        sta  track       
        jsr  getact     ; allocate a buffer
        pha      
        sta  jobnum      
        jsr  drtrd      ; read directory sector
        ldy  #0          
        lda  bufind,x   ; .x is job#
        sta  r0+1        
        lda  index       
        sta  r0          
        lda  (r0),y      
        and  #$20        
        beq  clsd5       
        jsr  typfil      
        cmp  #reltyp     
        beq  clsd6       

        lda  (r0),y      
        and  #$8f       ; replace file
        sta  (r0),y      
        iny      
        lda  (r0),y      
        sta  track       
        sty  temp+2      
        ldy  #27        ; extract replacement link
        lda  (r0),y     ;  to last sector
        pha      
        dey      
        lda  (r0),y      
        bne  clsd4       
        sta  track       
        pla      
        sta  sector      
        lda  #$67        
        jsr  cmder2      
clsd4            
        pha      
        lda  #0          
        sta  (r0),y      
        iny      
        sta  (r0),y      
        pla      
        ldy  temp+2      
        sta  (r0),y      
        iny      
        lda  (r0),y      
        sta  sector      
        pla      
        sta  (r0),y      
        jsr  delfil     ; delete old file
        jmp  clsd6      ; set close bit
clsd5            
        lda  (r0),y      
        and  #$f         
        ora  #$80        
        sta  (r0),y      
clsd6   ldx  wlindx      
        ldy  #28        ; set # of blocks
        lda  nbkl,x      
        sta  (r0),y      
        iny      
        lda  nbkh,x      
        sta  (r0),y      
        pla      
        tax      
        lda  #write     ; write directory sector
        ora  drvnum      
        jsr  doit        
        pla      
        sta  sa          
        jmp  fndwch     ; restore lindx
	.SUBTTL 'COM.SRC'
	.PAGE
;**********************************************
;*                                            *
;*   COMMODORE BUSINESS MACHINES SOFTWARE     *
;*                                            *
;**********************************************
;*                                            *
;*    11     55555    77777     11            *
;*     1     5           7       1            *
;*     1     5555       7        1            *
;*     1         5      7        1            *
;*   11111   5555       7      11111          *
;*                                            *
;*    FFFFFF   AAAAAA   SSSSSS   TTTTTT       *
;*    F        A    A   S          T          *
;*    FFFF     AAAAAA   SSSSSS     T          *
;*    F        A    A        S     T          *
;*    F        A    A   SSSSSS     T          *
;*                                            *
;*  SSSSS  EEEEE  RRRRR  IIIII  AAAAA  L      *
;*  S      E      R   R    I    A   A  L      *
;*  SSSSS  EEEE   RRR      I    AAAAA  L      *
;*      S  E      R  R     I    A   A  L      *
;*  SSSSS  EEEEE  R   R  IIIII. A   A  LLLLL  *
;*                                            *
;*  DDDD   IIIII  SSSSS  K    K		      *
;*  D   D    I    S      K   K  	      *
;*  D   D    I    SSSSS  KKK		      *
;*  D   D    I        S  K   K  	      *
;*  DDDD   IIIII  SSSSS  K    K  	      *
;*                                            *
;*                                            *
;**********************************************
;*                                            *
;*   DISK OPERATING SYSTEM AND CONTROLLER     *
;*   ROUTINES.                                *
;*                                            *
;*   COPYRIGHT (C) 1986 BY                    *
;*   COMMODORE BUSINESS MACHINES (CBM)        *
;*                                            *
;*   					DS    *
;*                                            *
;**********************************************
;
;**********************************************
;*   THIS SOFTWARE IS FURNISHED FOR USE IN    *
;*  THE 1571 FAST SERIAL FLOPPY DISK DRIVE.   *
;*                                            *
;*   COPIES THEREOF MAY NOT BE PROVIDED OR    *
;*  MADE AVAILABLE FOR USE ON ANY OTHER       *
;*  SYSTEM.                                   *
;*                                            *
;*   THE INFORMATION IN THIS DOCUMENT IS      *
;*  SUBJECT TO CHANGE WITHOUT NOTICE.         *
;*                                            *
;*   NO RESPONSIBILITY IS ASSUMED FOR         *
;*  RELIABILITY OF THIS SOFTWARE.             *
;*                                            *
;**********************************************
	.page   
	.subttl 'copall.src'

; set up subroutine

pups1   lda  #0          
        sta  rec         
        sta  drvcnt      
        sta  filtrk      
        sta  filtrk+1    
        lda  fildrv+1   ; get drive number
        and  #1         ; only
        sta  drvnum      
        ora  #1          
        sta  delsec     ; nonzero
        lda  filtbl+1   ; fn1=fn2
        sta  filtbl      
        rts      
	.nlist
;.end
	.page   'copy  all'     
;
; copy disk to disk routines
;
;cpydtd  lda  filtbl+1   ; save in temp
;        sta  temp        
;        ldy  #40        ; 40 char buffer
;        ldx  cmdsiz     ; prep to move
;        sty  cmdsiz     ; end of filename2
;movlp1  dey      
;        dex      
;        lda  cmdbuf,x   ; mov fn lifo
;        sta  cmdbuf,y    
;        cpx  temp       ; actual f2 val
;        bne  movlp1      
;        sty  filtbl+1   ; pointer to f2
;movlp2  jsr  optsch      
;        jsr  pups1      ; setup first pass
;        jsr  ffst       ; first match
;        bpl  fixit      ; entry found?
;        bmi  endit      ; no
;;
;exlp0   pla     ; pull needed vars
;        sta  dirsec      
;        pla      
;        sta  filtbl+1    
;        pla      
;        sta  lstbuf      
;        pla      
;        sta  filcnt      
;        pla      
;        sta  index       
;        pla      
;        sta  found       
;        pla      
;        sta  delind      
;        pla      
;        sta  drvflg      
;;
;exlp1   jsr  pups1      ; set up vars
;        jsr  ffre       ; next match
;        bpl  fixit      ; found one?
;endit   jmp  endcmd     ; no! so bye
;;
;fixit   lda  drvflg     ; push needed vars
;        pha      
;        lda  delind      
;        pha      
;        lda  found       
;        pha      
;        lda  index       
;        pha      
;        lda  filcnt      
;        pha      
;        lda  lstbuf      
;        pha      
;        lda  filtbl+1    
;        pha      
;        lda  dirsec      
;        pha      
;;
;exlp2   jsr  trfnme     ; transfer name
;        lda  #1         ; fake out lookup
;        sta  f1cnt       
;        sta  f2cnt       
;        jsr  lookup      
;        lda  #1          
;        sta  f1cnt       
;        lda  #2         ; real
;        sta  f2cnt       
;        jsr  cy         ; copy it
;        jmp  exlp0      ; next one folks
;;
;; transfer name (dirbuf) to cmdbuf
;;
;trfnme  ldy  #3         ; both indexes
;        sty  filtbl     ; begining of filename1
;trf0    lda  (dirbuf),y         ; move it
;        sta  cmdbuf,y    
;        iny      
;        cpy  #19        ; all 16 chars passed?
;        bne  trf0        
;        rts      
	.list
	.page  
	.subttl 'copset.src'

; dskcpy check for type
; and parses special case

dskcpy           
        lda  #$e0       ; kill bam buffer
        sta  bufuse      
        jsr  clnbam     ; clr tbam
        jsr  bam2x      ; get bam lindx in .x
        lda  #$ff        
        sta  buf0,x     ; mark bam out-of-memory
        lda  #$0f        
        sta  linuse     ; free all lindxs
        jsr  prscln     ; find ":"
        bne  dx0000      
        jmp  duplct     ; bad command error, cx=x not allowed
	.nlist
;
;jsr prseq
;
;lda #'* ;cpy all
;ldx #39 ;put at buffer end
;stx filtbl+1
;sta cmdbuf,x ;place *
;inx
;stx cmdsiz
;ldx #1 ;set up cnt's
;stx f1cnt
;inx
;stx f2cnt
;jmp movlp2 ;enter routine
;
	.list
dx0000  jsr  tc30       ; normal parse
dx0005  jsr  alldrs     ; put drv's in filtbl
        lda  image      ; get parse image
        and  #%01010101 ; val for patt copy
        bne  dx0020     ; must be concat or normal
        ldx  filtbl     ; chk for *
        lda  cmdbuf,x    
        cmp  #'*         
        bne  dx0020      
	.nlist
;ldx #1 ;set cnt's
;  no pattern matching allowed
;stx f1cnt
;inx
;stx f2cnt
;jmp cpydtd ;go copy
	.list
dx0010  lda  #badsyn    ; syntax error
        jmp  cmderr      
dx0020  lda  image      ; chk for normal
        and  #%11011001          
        bne  dx0010      
        jmp  copy        
	.nlist
;.end
;prseq            
;        lda  #'=        ; special case
;        jsr  parse       
;        bne  x0020       
;x0015   lda  #badsyn     
;        jmp  cmderr      
;x0020   lda  cmdbuf,y    
;        jsr  tst0v1      
;        bmi  x0015       
;        sta  fildrv+1   ; src drv
;        dey      
;        dey      
;        lda  cmdbuf,y    
;        jsr  tst0v1      
;        bmi  x0015       
;        cmp  fildrv+1   ; cannot be equal
;        beq  x0015       
;        sta  fildrv     ; dest drv
;        rts      
	.list
        .page  
        .subttl 'dskintsf.src'       

; error display routine
; blinks the (error #)+1 in all three leds

pezro   ldx  #0         ; error #1 for zero page
	.byte skip2     ; skip next two bytes
perr    ldx  temp       ; get error #
        txs     	; use stack as storage reg.
pe20    tsx     	; restore error #
pe30    lda  #led0+led1          
        ora  ledprt      
        jmp  pea7a       

; turn on led !!!!patch so ddrb led is output!!!!

rea7d   tya     	; clear inner ctr !!!!patch return!!!!
pd10    clc      
pd20    adc  #1         ; count inner ctr
        bne  pd20        
        dey     	; done ?
        bne  pd10       ; no

        lda  ledprt      
        and  #$ff-led0-led1      
        sta  ledprt     ; turn off all leds
pe40            	; wait
        tya     	; clear inner ctr
pd11    clc      
pd21    adc  #1         ; count inner ctr
        bne  pd21        
        dey     	; done ?
        bne  pd11       ; no

        dex     	; blinked # ?
        bpl  pe30       ; no - blink again
        cpx  #$fc       ; waited between counts ?
        bne  pe40       ; no
        beq  pe20       ; always - all again

dskint  sei      
        cld      
        ldx  #$66	; *,atnout,clk,*,*,side,fsdir,trk0
        jmp  patch5     ; *** rom ds 8/18/83 ***
dkit10  inx		; fill		

;*********************************
;
; power up diagnostic
;
;*********************************

        ldy  #0          
        ldx  #0          
pu10    txa     	; fill z-page accend pattern
        sta  $0,x        
        inx      
        bne  pu10        
pu20    txa     	; check pattern by inc...
        cmp  $0,x       ; ...back to orig #
        bne  pezro      ; bad bits
pu30             
	inc  $0,x       ; bump contents
        iny      
        bne  pu30       ; not done

        cmp  $0,x       ; check for good count
        bne  pezro      ; something's wrong

        sty  $0,x       ; leave z-page zeroed
        lda  $0,x       ; check it
        bne  pezro      ; wrong

        inx     	; next!
        bne  pu20       ; not all done


; test 32k byte rom 

; enter x=start page
; exit if ok

rm10    inc  temp       ; next error #
	ldx  #127	; 128 pages
        stx  ip+1       ; save page, start x=0
	inx		; **** rom ds 04/22/86 ***
	lda  #0          
        sta  ip         ; zero lo indirect
	ldy  #2		; skip signature bytes
        clc      
rt10    inc  ip+1       ; do it backwards
rt20    adc  (ip),y     ; total checksum in a
        iny      
        bne  rt20        

        dex      
        bne  rt10        

        adc  #255        ; add in last carry
	sta  ip+1
        bne  perr2      ; no - show error number

; **** rom ds 04/22/86 ***

	nop		 ; fill
	nop		 ; fill
	nop		 ; fill

;----------------------------------

; test all common ram

cr20    lda  #$01       ; start of 1st block
cr30    sta  ip+1       ; save page #
        inc  temp       ; bump error #

; enter x=# of pages in block
; ip ptr to first page in block
; exit if ok

ramtst  ldx  #7         ; save page count
ra10    tya     	; fill with adr sensitive pattern
        clc      
        adc  ip+1        
        sta  (ip),y      
        iny      
        bne  ra10        
        inc  ip+1        
        dex      
        bne  ra10        
        ldx  #7         ; restore page count
ra30    dec  ip+1       ; check pattern backwards
ra40    dey      
        tya     	; gen pattern again
        clc      
        adc  ip+1        
        cmp  (ip),y     ; ok ?
        bne  perr2      ; no - show error #
        eor  #$ff       ; yes - test inverse pattern
        sta  (ip),y      
        eor  (ip),y     ; ok ?
        sta  (ip),y     ; leave memory zero
        bne  perr2      ; no - show error #
        tya      
        bne  ra40        
        dex      
        bne  ra30        

        beq  diagok      

perr2   jmp  perr        

diagok  
;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>         

	jmp  ptch31	; *** rom ds 05/01/85 ***
;       ldx  #topwrt     
;       txs      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>         

rtch31  lda  ledprt     ; clear leds
        and  #$ff-led0-led1      
        sta  ledprt      

        lda  #3         ; neg edge of atn & edge on wp
        sta  pcr1        
        lda  #%10000010 ; set,_t1,_t2,_cb1,_cb2,_sr,ca1,_ca2(wps)
        sta  ifr1        
	sta  ier1        
        lda  pb         ; compute primary addr
        and  #%01100000 ; pb5 and pb6 are unused lines
        asl  a          ; shift to lower
        rol  a   
        rol  a   
        rol  a   
        ora  #$48       ; talk address
        sta  tlkadr      
        eor  #$60       ; listen address
        sta  lsnadr      

; initialize buffer pntr table

inttab  ldx  #0          
        ldy  #0          
intt1   lda  #0          
        sta  buftab,x    
        inx      
        lda  bufind,y    
        sta  buftab,x    
        inx      
        iny      
        cpy  #bfcnt      
        bne  intt1       

        lda  #<cmdbuf   ; set pntr to cmdbuf
        sta  buftab,x    
        inx      
        lda  #>cmdbuf    
        sta  buftab,x    
        inx      
        lda  #<errbuf   ; set pntr to errbuf
        sta  buftab,x    
        inx      
        lda  #>errbuf    
        sta  buftab,x    

        lda  #$ff        
        ldx  #maxsa      
dskin1  sta  lintab,x    
        dex      
        bpl  dskin1      

        ldx  #mxchns-1   
dskin2           
        sta  buf0,x     ; set buffers as unused
        sta  buf1,x      
        sta  ss,x        
        dex      
        bpl  dskin2      

        lda  #bfcnt     ; set buffer pointers
        sta  buf0+cmdchn         
        lda  #bfcnt+1    
        sta  buf0+errchn         
        lda  #$ff        
        sta  buf0+blindx         
        sta  buf1+blindx         

        lda  #errchn     
        sta  lintab+errsa        
        lda  #cmdchn+$80         
        sta  lintab+cmdsa        
        lda  #lxint     ; lindx 0 to 5 free
        sta  linuse      

        lda  #rdylst     
        sta  chnrdy+cmdchn       
        lda  #rdytlk     
        sta  chnrdy+errchn       
        lda  #$e0        
        sta  bufuse      
        lda  #$ff        
        sta  bufuse+1    

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  ptch72	; *** rom ds 05/20/86 ***
	nop
;       lda  #1          
;       sta  wpsw        

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        sta  wpsw+1      
        jsr  usrint     ; init user jmp
        jsr  lruint      

;**********************************
;
; controller initialization
;
;**********************************

        jsr  ptch10     ; *** rom ds 05/01/85 controller init ***
;       jsr  cntint

; set indirect vectors

        lda  #<diagok    
        sta  vnmi        
        lda  #>diagok    
        sta  vnmi+1      

        lda  #6	        ; set up sector offset *** rom ds 01/22/85 ***
        sta  secinc      
        lda  #5          
        sta  revcnt     ; set up recovery count

;*
;*******************************
;*
;*    seterr
;*    set up power on error msg
;*
;*******************************
;*
;*
seterr  lda  #$73        
        jsr  errts0      


;must be contiguous to .file idle

;********************************
; init the serial bus
;
;********************************

;-------rom -05 8/18/83-----------------
        lda  #$00       ;  data hi, clock hi,atna hi
        sta  pb          
        lda  #%00011010 ;  atna,clkout,datout
        sta  ddrb1       
;---------------------------------------
    	jsr  ptch29	; *rom ds 02/01/85*
;       jsr  boot        
	.page
	.subttl 'duplct1.src'

;  controller format disk

jformat lda  #71	
	sta  maxtrk	; double sided
	lda  #3          
        jsr  seth        
        ldx  #3         ; job #3
	lda  #0
	sta  fmtsid	; side zero first
        lda  #$f0       ; format cmd
        sta  tsttrk     ; init speed var
        sta  jobs,x     ; give job to controller
        jsr  stbctl     ; wake him up
        cmp  #2         ; error?
        bcs  1$		; br, error

;read track one sector zero

	ldy  #3		; retries
4$      lda  #1         ; track 1
        sta  hdrs+6     ; *
        lda  #0         ; sector 0
        sta  hdrs+7     ; *
        lda  #$80       ; read
        sta  jobs,x     ; give job to controller
        jsr  stbctl     ; wake him up
        cmp  #2         ; error?
        bcc  5$		; br, ok...
	
	dey
	bpl  4$		; try 3 times
	bcs  1$		; bra

5$	lda  #1
	sta  fmtsid	; side one second
        lda  #$f0       ; format cmd
        sta  tsttrk     ; init speed var
        sta  jobs,x     ; give job to controller
        jsr  stbctl     ; wake him up
        cmp  #2         ; error?
        bcs  1$		; br, error

;read track thirty-six sector zero

	ldy  #3		; retries
6$      lda  #36        ; track 36
        sta  hdrs+6     ; *
        lda  #0         ; sector 0
        sta  hdrs+7     ; *
        lda  #$80       ; read
        sta  jobs,x     ; give job to controller
        jsr  stbctl     ; wake him up
        cmp  #2         ; error?
	bcs  3$		; br, bad

	rts		; ok
	
3$	dey
	bpl  6$		; keep trying

1$      ldx  #0         ; set for offset for buffer to det. trk & sect.
	bit  jobrtn	; return on error ?
	stx  jobrtn	; clr
	bpl  7$

	rts		; back to caller
7$	jmp  error
        .page 
        .subttl 'duplct.src'

; duplicate disk

duplct           
        lda  #badcmd     
        jmp  cmderr      

; transfer format code to buffer 0
;  & start controller formatting

format           
        lda  #$4c        
        sta  bufs+$300   

        lda  #<formt     
        sta  bufs+$301   
        lda  #>formt     
        sta  bufs+$302   

        lda  #3          
        jsr  seth        
        lda  drvnum      
        ora  #exec       
        sta  jobs+3      
fmt105  lda  jobs+3      
        bmi  fmt105      

        cmp  #2          
        bcc  fmt110      

        lda  #3          
        ldx  #0          
        jmp  error       
fmt110  rts      
	.page  
	.subttl 'erproc.src'          
; error processing 

; controller errors
;  0  (1)  no error
; 20  (2)  can't find block header
; 21  (3)  no synch character
; 22  (4)  data block not present
; 23  (5)  checksum error in data
; 24  (16) byte decoding error
; 25  (7)  write-verify error
; 26  (8)  write w/ write protect on
; 27  (9)  checksum error in header
; 28  (10) data extends into next block
; 29  (11) disk i.d. mismatch

; command errors
; 30  general syntax
; 31  invalid command
; 32  long line
; 33  invalid filname
; 34  no file given
; 39  command file not found

; 50  record not present
; 51  overflow in record
; 52  file too large

; 60  file open for write
; 61  file not open
; 62  file not found
; 63  file exists
; 64  file type mismatch
; 65  no block
; 66  illegal track or sector
; 67  illegal system t or s

; 70  no channels available
; 71  directory error
; 72  disk full
; 73  cbm dos v3.0
; 74  drive not ready

;  1  files scratched response

badsyn   =$30    
badcmd   =$31    
longln   =$32    
badfn    =$33    
nofile   =$34    
nocfil   =$39    
norec    =$50    
recovf   =$51    
bigfil   =$52    
filopn   =$60    
filnop   =$61    
flntfd   =$62    
flexst   =$63    
mistyp   =$64    
noblk    =$65    
badts    =$66    
systs    =$67    
nochnl   =$70    
direrr   =$71    
dskful   =$72    
cbmv2    =$73    
nodriv   =$74    
	.page    
; error message table
;   leading errror numbers,
;   text with 1st & last chars 
;   or'ed with $80,
;   tokens for key words are
;   less than $10 (and'ed w/ $80)

errtab          ; " OK"
	.byte    0,$a0,'O',$cb   
;"read error"
	.byte    $20,$21,$22,$23,$24,$27         
	.byte    $d2,'EAD',$89   
;" file too large"
	.byte    $52,$83,' TOO LARG',$c5       
;" record not present"
	.byte    $50,$8b,6,' PRESEN',$d4        
;"overflow in record"
	.byte    $51,$cf,'VERFLOW '     
	.byte    'IN',$8b        
;" write error"
	.byte    $25,$28,$8a,$89         
;" write protect on"
	.byte    $26,$8a,' PROTECT O',$ce      
;" disk id mismatch"
	.byte    $29,$88,' ID',$85      
;"syntax error"
	.byte    $30,$31,$32,$33,$34     
	.byte    $d3,'YNTAX',$89         
;" write file open"
	.byte    $60,$8a,3,$84   
;" file exists"
	.byte    $63,$83,' EXIST',$d3   
;" file type mismatch"
	.byte    $64,$83,' TYPE',$85    
;"no block"
	.byte    $65,$ce,'O BLOC',$cb   
;"illegal track or sector"
	.byte   $66,$67,$c9,'LLEGAL TRACK'     
	.byte   ' OR SECTO',$d2       
;" file not open"
	.byte    $61,$83,6,$84   
;" file not found"
	.byte    $39,$62,$83,6,$87       
;" files scratched"
	.byte    1,$83,'S SCRATCHE',$c4         
;"no channel"
	.byte    $70,$ce,'O CHANNE',$cc         
;"dir error"
	.byte    $71,$c4,'IR',$89        
;" disk full"
	.byte    $72,$88,' FUL',$cc     
;"cbm dos v3.0 1571"
	.byte   $73,$c3,'BM DOS V3.0 157',$b1        
;"drive not ready"
	.byte   $74,$c4,'RIVE',6,' READ',$d9   

; error token key words
;   words used more than once
;"error"
	.byte    9,$c5,'RRO',$d2         
;"write"
	.byte    $a,$d7,'RIT',$c5        
;"file"
	.byte    3,$c6,'IL',$c5          
;"open"
	.byte    4,$cf,'PE',$ce          
;"mismatch"
	.byte    5,$cd,'ISMATC',$c8      
;"not"
	.byte    6,$ce,'O',$d4   
;"found"
	.byte    7,$c6,'OUN',$c4         
;"disk"
	.byte    8,$c4,'IS',$cb          
;"record"
	.byte   $b,$d2,'ECOR',$c4       
etend    =*      
	.page    
; controller error entry
;   .a= error #
;   .x= job #
error   
;<><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch46	; *** rom ds 03/31/85 ***
;	pha      
;       stx  jobnum      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><>

rtch46  txa      
	asl  a   
	tax      
	lda  hdrs,x     ; 4/12********* track,sector
	sta  track       
	lda  hdrs+1,x   ; 4/12*********
	sta  sector      

	pla      
	and  #$f        ; convert controller...
	beq  err1       ; ...errors to dos errors
	cmp  #$f        ; check nodrive error
	bne  err2        

	lda  #nodriv     
	bne  err3       ; bra
err1             
	lda  #6         ; code=16-->14
err2    ora  #$20        
	tax      
	dex      
	dex      
	txa      
err3             
	pha      
	lda  cmdnum      
	cmp  #val        
	bne  err4        
	lda  #$ff        
	sta  cmdnum      
	pla      
	jsr  errmsg      
	jsr  initdr     ; init for validate
	jmp  cmder3      
err4             
	pla      
cmder2           
	jsr  errmsg      
cmder3           
	jsr  clrcb      ; clear cmdbuf
	lda  #0          
	sta  wbam       ; clear after error
	jsr  erron      ; set error led
	jsr  freich     ; free internal channel
	lda  #0         ; clear pointers
	sta  buftab+cbptr        
	ldx  #topwrt     
	txs     	;  purge stack
	lda  orgsa       
	and  #$f         
	sta  sa          
	cmp  #$f         
	beq  err10       
	sei      
	lda  lsnact      
	bne  lsnerr      
	lda  tlkact      
	bne  tlkerr      

	ldx  sa          
	lda  lintab,x    
	cmp  #$ff        
	beq  err10       
	and  #$f         
	sta  lindx       
	jmp  tlerr       


; talker error recovery
;  if command channel, release dav
;  if data channel, force not ready
;   and release channel
tlkerr           
	jsr  fndrch      
;       jsr iterr 		; *** rom - 05 fix 8/18/83 ***
	.byte  $ea,$ea,$ea      ; fill in 'jsr'
	bne  tlerr      	; finish

; listener error recovery
;  if command channel, release rfd
;  if data channel, force not ready
;  and release channel
lsnerr           
	jsr  fndwch      
;       jsr ilerr 		; *** rom - 05 fix 8/18/83 ***
	.byte  $ea,$ea,$ea      ; fill in 'jsr'
tlerr            
	jsr  typfil      
	cmp  #reltyp     
	bcs  err10       
	jsr  frechn      
err10            

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  xidle		; *** rom ds 01/22/84 ***
;       jmp  idle        

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	.page    
; convert hex to bcd
hexdec  tax      
;<><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch67	; *** rom ds 05/15/86 ***
;       lda  #0          
;       sed      

hex0    cpx  #0          
	beq  hex5        
	clc      
	adc  #1          
	dex      
	jmp  hex0        

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><>

hex5    cld      

; convert bcd to ascii dec
;  return bcd in .x
;  store ascii in (temp),y
bcddec  tax      
	lsr  a   
	lsr  a   
	lsr  a   
	lsr  a   
	jsr  bcd2        
	txa      
bcd2             
	and  #$f         
	ora  #$30        
	sta  (cb+2),y    
	iny      
	rts      

; transfer error message to
;  error buffer

okerr            
	jsr  erroff      
	lda  #0          
errts0           
	ldy  #0          
	sty  track       
	sty  sector      

errmsg           
	ldy  #0          
	ldx  #<errbuf    
	stx  cb+2        
	ldx  #>errbuf    
	stx  cb+3        
	jsr  bcddec     ; convert error #
	lda  #',         
	sta  (cb+2),y    
	iny      
	lda  errbuf      
	sta  chndat+errchn       
	txa     	; error # in .x
	jsr  ermove     ; move message

ermsg2  lda  #',         
	sta  (cb+2),y    
	iny      
	lda  track       
	jsr  hexdec     ; convert track #
	lda  #',         
	sta  (cb+2),y    
	iny      
	lda  sector     ; convert sector #
	jsr  hexdec      
	dey      
	tya      
	clc      
	adc  #<errbuf   ; set last char
	sta  lstchr+errchn      
	inc  cb+2        
	lda  #rdytlk     
	sta  chnrdy+errchn       
	rts      

;**********************************;
;*    ermove - move error message *;
;*      from errtab to errbuf.    *;
;*      fully recursive for token *;
;*      word prosessing.          *;
;*   input: .a= bcd error number  *;
;**********************************;

ermove           
	tax     	; save .a
	lda  r0         ; save r0,r0+1
	pha      
	lda  r0+1        
	pha      
	lda  #<errtab   ; set pointer to table
	sta  r0          
	lda  #>errtab    
	sta  r0+1        
	txa     	; restore .a
	ldx  #0         ; .x=0 for indirect
e10              
	cmp  (r0,x)     ; ?error # = table entry?
	beq  e50        ; yes, send message

	pha     	; save error #
	jsr  eadv2      ; check & advance ptr
	bcc  e30        ; more #'s to check
e20              
	jsr  eadv2      ; advance past this message
	bcc  e20         
e30              
	lda  r0+1       ; check ptr
	cmp  #>etend     
	bcc  e40        ; <, continue
	bne  e45        ; >, quit

	lda  #<etend     
	cmp  r0          
	bcc  e45        ; past end of table
e40              
	pla     	; restore error #
	jmp  e10        ; check next entry
e45              
	pla     	; pop error #
	jmp  e90        ; go quit

e50             	; the number has been located
	jsr  eadv1       
	bcc  e50        ; advance past other #'s
e55              
	jsr  e60         
	jsr  eadv1       
	bcc  e55         

	jsr  e60        ; check for token or last word
e90              
	pla     	; all finished
	sta  r0+1       ; restore r0
	pla      
	sta  r0          
	rts      

e60              
	cmp  #$20       ; (max token #)+1
	bcs  e70        ; not a token
	tax      
	lda  #$20       ; implied leading space
	sta  (cb+2),y    
	iny      
	txa     	; restore token #
	jsr  ermove     ; add token word to message
	rts      
e70              
	sta  (cb+2),y   ; put char in message
	iny      
	rts      

;error advance & check

eadv1           	; pre-increment
	inc  r0         ; advance ptr
	bne  ea10        
	inc  r0+1        
ea10             
	lda  (r0,x)     ; get current entry
	asl  a          ; .c=1 is end or beginning
	lda  (r0,x)      
	and  #$7f       ; mask off bit7
	rts      

eadv2           	; post-increment
	jsr  ea10       ; check table entry
	inc  r0          
	bne  ea20        
	inc  r0+1        
ea20             
	rts      
        .page  
	.subttl 'fastld.src'

fstload jsr  spout	; output
	jsr  set_fil	; setup filename for parser
	bcs  9$

        jsr  autoi	; init mechanism
	lda  nodrv	; chk status
	bne  9$		; no drive status

	lda  fastsr	; set error recovery flag on
	ora  #$81	; & eoi flag
	sta  fastsr	
	jsr  findbuf	; check for buffer availabilty

	lda  cmdbuf
	cmp  #'*	; load last ?
	bne  7$
	
	lda  prgtrk	; any file ?
	beq  7$

	pha		; save track
	lda  prgsec
	sta  filsec	; update
	pla
	jmp  1$

7$	lda  #0          
	tay
	tax		; clear .a, .x, .y
        sta  lstdrv     ; init drive number
	sta  filtbl	; set up for file name parser
        jsr  onedrv     ; select drive
	lda  f2cnt
	pha	
	lda  #1
	sta  f2cnt
	lda  #$ff
	sta  r0		; set flag
	jsr  lookup	; locate file
	pla		 
	sta  f2cnt	; restore var
	lda  fastsr
	and  #$7f	; clr error recovery flag
	sta  fastsr
	bit  switch	; seq flag set ?
	bmi  8$

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

; 	lda  pattyp	; is it a program file ?
;	cmp  #2
	jsr  ptch56	; *** rom ds 07/15/85 ***
	nop		; fill

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	bne  6$		; not prg

8$      lda  filtrk     ; check if found. err if not
        bne  1$		; br, file found

6$    	ldx  #%00000010	; file not found
	.byte skip2
9$	ldx  #%00001111	; no drive
        jmp  sys_err	

1$      sta  prgtrk	; save for next
	pha		; save track
	jsr  set_buf	; setup buffer allocation
	pla		; get track
	ldx  channel	; get channel offset
	sta  hdrs,x	; setup track
        lda  filsec     ; & sector
	sta  prgsec	; for next time
        sta  hdrs+1,x

	lda  #read	; read job
	sta  cmdbuf+2	; save read cmd
	sta  ctl_cmd	

2$	cli		; let controller run
	ldx  jobnum	; get job #
	lda  ctl_cmd	; get cmd
	sta  jobs,x	; send cmd
	jsr  stbctr	; whack the controller in the head
	cpx  #2		; error ?
	bcc  5$
	
	jmp  ctr_err

5$	sei
	ldy  #0
	lda  (dirbuf),y	; check status
	beq  end_of_file	

	lda  fastsr	; clear flag
	and  #$fe
	sta  fastsr

	jsr  handsk	; handshake error to the host

	ldy  #2
3$	lda  (dirbuf),y
	tax		; save data in .x
	jsr  handsk	; handshake it to the host
	iny
	bne  3$

	ldx  channel	; jobnum * 2
	lda  (dirbuf),y ; .y = 0
	cmp  hdrs,x	; same as previous track ?
	beq  4$

	ldy  #read
	.byte skip2
4$	ldy  #fread	; fast read
	sty  ctl_cmd	; command to seek then read
	sta  hdrs,x	; next track
	ldy  #1		; sector entry
	lda  (dirbuf),y	
	sta  hdrs+1,x	; next sector	
	jmp  2$


end_of_file
	ldx  #$1f	; eof
	jsr  handsk	; handshake it to the host

	lda  #1
	bit  fastsr	; first time through ?
	beq  1$	        ; br, nope

	tay		; .y = 1
	lda  (dirbuf),y	; number of bytes
	sec
	sbc  #3
	sta  ctl_dat	; save it
	tax		; send it
	jsr  handsk	; handshake it to the host

	iny		; next
	lda  (dirbuf),y	; address low
	tax
	jsr  handsk	; handshake it to the host

	iny
	lda  (dirbuf),y	; address high
	tax
	jsr  handsk	; handshake it to the host
	ldy  #4		; skip addresses
	bne  3$		; bra

1$      ldy  #1
	lda  (dirbuf),y	; number of bytes
	tax
	dex
	stx  ctl_dat	; save here
	jsr  handsk	; handshake it to the host

	ldy  #2		; start at data
3$	lda  (dirbuf),y
	tax
	jsr  handsk	; handshake it to the host
	iny
	dec  ctl_dat	; use it as a temp
	bne  3$

	lda  #0
	sta  sa
	jsr  close	; close channel	(faux)
	jmp  endcmd
;
;
;
; *************************
; ***** ERROR HANDLER *****
; *************************

ctr_err sei		; no irq's
	stx  ctl_dat	; save status here
	jsr  handsk	; handshake it to the host
	lda  #0
	sta  sa
	jsr  close	; close channel (faux)
	ldx  jobnum
	lda  ctl_dat	; get error
	jmp  error	; error out.....	

sys_err sei
	stx  ctl_dat	; save error
	ldx  #2		; file not found 
	jsr  handsk	; give it to him
	lda  #0
	sta  sa
	jsr  close	; close channel (faux)
	lda  ctl_dat	; get error back
	cmp  #2
	beq  1$

	lda  #nodriv	; no active drive
	.byte skip2
1$	lda  #flntfd	; file not found
	jmp  cmderr	; never more...
;
;
;
; *************************************
; ***** FIND INTERNAL READ BUFFER *****
; *************************************

findbuf lda  #0
	sta  sa		; psydo-load 
	lda  #1		; 1 buffer
	jsr  getrch	; find a read channel
	tax
	lda  bufind,x	; get buffer
	sta  dirbuf+1	; set it up indirect
	rts	
;
;
;
; **************************************
; ***** SETUP INTERNAL READ BUFFER *****
; **************************************

set_buf lda  dirbuf+1	; index to determine job
	sec
	sbc  #3
	sta  jobnum	; save in jobnum
	asl  a
	sta  channel	; save channel off
	lda  #0
	sta  dirbuf	; even page boundary
	rts
;
;
;
; *************************************
; ***** FAST LOAD FILENAME PARSER *****
; *************************************

set_fil ldy  #3		; default .y
	lda  cmdsiz	; delete burst load command
	sec
	sbc  #3
	sta  cmdsiz	; new command size

	lda  cmdbuf+4   ; drv # given ?
	cmp  #':
	bne  1$

	lda  cmdbuf+3
	tax		; save 
	and  #'0
	cmp  #'0        ; 0:file ?
	bne  1$

	cpx  #'1	; chk for error
	beq  4$			

1$	lda  cmdbuf+3   ; drv # given ?
	cmp  #':
	bne  2$

	dec  cmdsiz
	iny

2$	ldx  #0		; start at cmdbuf+0
3$      lda  cmdbuf,y	; extract file-name
	sta  cmdbuf,x
	iny
	inx
	cpx  cmdsiz	; done ?
	bne  3$		; delete cmd from buffer

	clc
	.byte skip1
4$	sec		; error
	rts



handsk			; .x contains data
1$	lda  pb		; debounce
        cmp  pb
        bne  1$

	and  #$ff	; set/clr neg flag
        bmi  3$		; br, attn low 

        eor  fastsr     ; wait for state chg
        and  #4          
        beq  1$

        stx  sdr	; send it
        lda  fastsr      
        eor  #4         ; change state of clk
        sta  fastsr      

        lda  #8
2$	bit  icr	; wait transmission time
        beq  2$

        rts      

3$	jmp  ptch30	; bye-bye the host wants us
	.page  
	.subttl 'fastutl.src'
;NOTE: ALL BURST COMMANDS ARE SENT VIA KERNAL I/O CALLS.
;
;BURST CMD ONE - READ
;
;     BYTE  BIT 7       6       5       4       3       2       1       0  
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	T	E	B	S	0	0	0       N
;----------------------------------------------------------------------------
;       03	               DESTINATION TRACK
;----------------------------------------------------------------------------
;       04		       DESTINATION SECTOR
;----------------------------------------------------------------------------
;       05		       NUMBER OF SECTORS
;----------------------------------------------------------------------------
;       06		       NEXT TRACK (OPTIONAL)
;----------------------------------------------------------------------------
;
;
;   RANGE:
;
;	MFM ALL VALUES ARE DETERMINED UPON THE PARTICULAR DISK FORMAT.
;	GCR SEE SOFTWARE SPECIFICATIONS.
;
;
;SWITCHES:
;
;	T - TRANSFER DATA (1=NO TRANSFER)
;	E - IGNORE ERROR (1=IGNORE)
;	B - BUFFER TRANSFER ONLY (1=BUFFER TRANSFER ONLY)
;	S - SIDE SELECT (MFM ONLY)
;	N - DRIVE NUMBER
;
;
;
;PROTOCOL:
;
;	BURST HANDSHAKE.
;
;
;
;CONVENTIONS:
;
;	CMD ONE MUST BE PRECEEDED  WITH CMD 3 OR CMD 6 ONCE  AFTER
;       A NEW DISK IS INSERTED.
;
;
;OUTPUT:
;	ONE  BURST  STATUS  BYTE PRECEEDING BURST DATA WILL BE SENT  
;	FOR  EVERY  SECTOR  TRANSFERED.    ON  AN  ERROR  CONDITION  
;	DATA  WILL NOT BE SENT UNLESS THE E BIT IS SET.
	.page
;BURST CMD TWO - WRITE
;
;     BYTE  BIT 7       6       5       4       3       2       1       0 
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	T	E	B	S	0	0	1       N
;----------------------------------------------------------------------------
;       03	               DESTINATION TRACK
;----------------------------------------------------------------------------
;       04		       DESTINATION SECTOR
;----------------------------------------------------------------------------
;       05		       NUMBER OF SECTORS
;----------------------------------------------------------------------------
;       06		       NEXT TRACK (OPTIONAL)
;----------------------------------------------------------------------------
;
;
;   RANGE:
;
;	MFM ALL VALUES ARE DETERMINED UPON THE PARTICULAR DISK FORMAT.
;	GCR SEE SOFTWARE SPECIFICATIONS.
;
; 
;SWITCHES:
;
;	T - TRANSFER DATA (1=NO TRANSFER)
;	E - IGNORE ERROR (1=IGNORE)
;	B - BUFFER TRANSFER ONLY (1=BUFFER TRANSFER ONLY)
;	S - SIDE SELECT (MFM ONLY)
;	N - DRIVE NUMBER
;
;
;
;PROTOCOL:
;
;	BURST HANDSHAKE.
;
;
;
;CONVENTIONS:
;
;	CMD TWO MUST BE PRECEEDED  WITH CMD 3 OR CMD 6 ONCE  AFTER 
;	A NEW DISK IS INSERTED.
;
;
;INPUT:
;	HOST MUST TRANSFER BURST DATA.
;
;
;OUTPUT:
;	ONE BURST STATUS  BYTE FOLLOWING EACH WRITE OPERATION.
	.page
;BURST CMD THREE - INQUIRE DISK
;
;
;     BYTE  BIT 7       6       5       4       3       2       1       0  
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	X	X	X	S	0	1	0       N
;----------------------------------------------------------------------------
;
;
; 
;SWITCHES:
;
;	S - SIDE SELECT (MFM ONLY)
;	N - DRIVE NUMBER
;
;
;OUTPUT:
;	ONE BURST STATUS  BYTE FOLLOWING THE INQUIRE OPERATION.
	.page
;BURST CMD FOUR - FORMAT MFM
;
;
;     BYTE  BIT 7       6       5       4       3       2       1       0 
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	P	I	D	S	0	1	1       N
;----------------------------------------------------------------------------
;       03      M=1      T                LOGICAL STARTING SECTOR
;----------------------------------------------------------------------------
;       04	     INTERLEAVE            (OPTIONAL DEF-0)
;----------------------------------------------------------------------------
;       05           SECTOR SIZE	 * (OPTIONAL 01 for 256 Byte Sectors)
;----------------------------------------------------------------------------
;       06           LAST TRACK NUMBER     (OPTIONAL DEF-39)
;----------------------------------------------------------------------------
;       07           NUMBER OF SECTORS     (OPTIONAL DEPENDS ON BYTE 05)
;----------------------------------------------------------------------------
;       08	    LOGICAL STARTING TRACK (OPTIONAL DEF-0)
;----------------------------------------------------------------------------
;       09           STARTING TRACK OFFSET (OPTIONAL DEF-0)
;----------------------------------------------------------------------------
;       0A           FILL BYTE             (OPTIONAL DEF-$E5)
;----------------------------------------------------------------------------
;       0B-??        SECTOR TABLE          (OPTIONAL T-BIT SET)
; ----------------------------------------------------------------------------
;
;SWITCHES:
;
;	P - PARTIAL FORMAT (1=PARTIAL)
;	I - INDEX ADDRESS MARK WRITTEN (1=WRITTEN)
;	D - DOUBLE SIDED FLAG (1=FORMAT DOUBLE SIDED)
;	S - SIDE SELECT 
;	T - SECTOR TABLE INCLUDED (1=INCLUDED, ALL OTHER PARMS 
;	    MUST BE INCLUDED)
;	N - DRIVE NUMBER
;
;
;PROTOCOL:
;
;	CONVENTIONAL.
;
;
;
;CONVENTIONS:
;
;	AFTER FORMATTING CMD 3 MUST BE SENT ON CONVENTIONAL FORMAT
;	OR CMD 6 ON A NON-CONVENTIONAL FORMAT.
;
;
;OUTPUT:
;        NONE.  STATUS WILL BE UPDATED WITHIN THE DRIVE.  
;
; * 00 - 128 byte sectors
;   01 - 256 byte sectors
;   02 - 512 byte sectors
;   03 - 1024 byte sectors
	.page
;BURST CMD FOUR - FORMAT GCR (NO DIRECTORY)
;		
;
;     BYTE  BIT 7       6       5       4       3       2       1       0 
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	X	X	X	X	0	1	1       N
;----------------------------------------------------------------------------
;       03      M=0
;----------------------------------------------------------------------------
;       04		       ID LOW
;----------------------------------------------------------------------------
;       05		       ID HIGH
;----------------------------------------------------------------------------
;
; 
;SWITCHES:
;
;	N - DRIVE NUMBER
;	X - DON'T CARE.
;
;PROTOCOL:
;
;	CONVENTIONAL.
;
;
;CONVENTIONS:
;	
;	AFTER FORMATTING CMD 3 MUST BE SENT.
;
;	NOTE: THIS  COMMAND  DOES NOT WRITE THE BAM OR THE
;	DIRECTORY ENTRIES  ( DOUBLE SIDED FLAG WILL NOT BE 
;	ON TRACK 18, SECTOR 0).  IT  IS SUGGESTED THAT THE 
;	CONVENTIONAL 'NEW' FORMAT COMMAND BE USED.
;
;OUTPUT:
;        NONE.  STATUS WILL BE UPDATED WITHIN THE DRIVE.  
	.page
;BURST CMD FIVE - SECTOR INTERLEAVE 
;		
;
;     BYTE  BIT 7       6       5       4       3       2       1       0 
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	W	X	X	0	1	0	0       N
;----------------------------------------------------------------------------
;       04		       INTERLEAVE 
;----------------------------------------------------------------------------
;
; 
;SWITCHES:
;
;	W - WRITE SWITCH
;	N - DRIVE NUMBER
;	X - DON'T CARE
;
;PROTOCOL:
;
;	CONVENTIONAL, UNLESS THE W-BIT IS SET.
;
;
;CONVENTIONS:
;
;	THIS IS A SOFT INTERLEAVE USED FOR MULTI-SECTOR BURST 
;	READ AND WRITE.
;
;OUTPUT:
;        NONE (W=0), INTERLEAVE BURST BYTE (W=1)
	.page
;BURST CMD SIX - QUERY DISK FORMAT
;		
;
;     BYTE  BIT 7       6       5       4       3       2       1       0 
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	F	X	T	S	1	0	1       N
;----------------------------------------------------------------------------
;       03   	            OFFSET      (OPTIONAL F-BIT SET)
;----------------------------------------------------------------------------
;
; 
;SWITCHES:
;
;	F - FORCE FLAG (F=1, WILL STEP THE HEAD WITH THE 
;	T - END SECTOR TABLE (T=1, SEND)
;	N - DRIVE NUMBER
;	X - DON'T CARE.
;
;PROTOCOL:
;
;	CONVENTIONAL.
;
;
;CONVENTIONS:
;
;	THIS IS A METHOD OF DETERMINING THE FORMAT OF THE DISK ON 
;	ANY PARTICULAR TRACK.
;
;
;OUTPUT:
;       BURST STATUS BYTE (IF THERE WAS AN ERROR OR IF THE 
;			   FORMAT IS GCR NO BYTES WILL FOLLOW)
;	BURST STATUS BYTE (IF THERE WAS AN ERROR IN COMPILING 
;		           MFM FORMAT INFORMATION NO BYTES
;			   WILL FOLLOW)
;	NUMBER OF SECTORS (THE NUMBER OF SECTORS ON A PARTICULAR
;			   TRACK)
;	LOGICAL TRACK	  (THE LOGICAL TRACK NUMBER FOUND IN THE 
;			   DISK HEADER)
;	MINIMUM SECTOR 	  (THE LOGICAL SECTOR WITH THE LOWEST
;			   VALUE ADDRESS)
;	MAXIMUM SECTOR	  (THE LOGICAL SECTOR WITH THE HIGHEST
;			   VALUE ADDRESS)
;	CP/M INTERLEAVE	  (THE HARD INTERLEAVE FOUND ON A PARTICULAR
;			   TRACK)
;
	.page
;BURST CMD SEVEN - INQUIRE STATUS
;		
;
;     BYTE  BIT 7       6       5       4       3       2       1       0 
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	W	C	X	0	1	1	0       N
;----------------------------------------------------------------------------
;       03                  	NEW STATUS (W-BIT CLEAR)
;----------------------------------------------------------------------------
; 
;SWITCHES:
;
;	W - WRITE SWITCH 
;	C - CHANGE (C=1 & W=0 - FORCE LOGIN DISK,
;		    C=1 & W=1 - RETURN WHETHER DISK WAS LOGGED 
;		    IE. $XB ERROR OR OLD STATUS)
;	N - DRIVE NUMBER
;	X - DON'T CARE
;
;PROTOCOL:
;	
;	BURST.
;
;
;CONVENTIONS:
;
;	THIS IS A METHOD OF READING OR WRITING CURRENT STATUS.
;
;
;OUTPUT:
;        NONE (W=0), BURST STATUS BYTE (W=1)
;		
;
	.page
;BURST CMD EIGHT - BACKUP DISK
;
;     BYTE  BIT 7       6       5       4       3       2       1       0 
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	?	?	?	?	1	1	1       ?
;----------------------------------------------------------------------------
; 
;SWITCHES:
;
;	? - GOT ME 
;
	.page
;BURST CMD NINE - CHGUTL UTILITY
;		
;
;     BYTE  BIT 7       6       5       4       3       2       1       0 
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	X	X	X	1	1	1	1       0
;----------------------------------------------------------------------------
;       03         UTILITY COMMANDS: 'S', 'R', 'T', 'M', 'H', #DEV
;----------------------------------------------------------------------------
;       04         COMMAND PARAMETER 
;----------------------------------------------------------------------------
;SWITCHES:
;
;	X - DON'T CARE.
;
;
;
;UTILITY COMMANDS:
;
;	'S' - DOS SECTOR INTERLEAVE.
;	'R' - DOS RETRIES.
;	'T' - ROM SIGNATURE ANALYSIS.
;	'M' - MODE SELECT.
;	'H' - HEAD SELECT.
;	'V' - WRITE WITH VERIFY.
;      #DEV - DEVICE #.
;
;
;NOTE: BYTE 02 IS EQUIVALENT TO A '>'
;	
;EXAMPLES:
;	"U0>S"+CHR$(SECTOR-INTERLEAVE)
;	"U0>R"+CHR$(RETRIES)
;	"U0>T"
;	"U0>M1"=1571 MODE, "U0>M0"=1541 MODE
;	"U0>H0"=SIDE ZERO, "U0>H1"=SIDE ONE (1541 MODE ONLY)
;	"U0>V0"=VERIFY ON, "U0>V1"=NO VERIFY (1571 MODE ONLY)
;	"U0>"+CHR$(#DEV), WHERE #DEV = 4-30
	.page
;BURST CMD TEN - FASTLOAD UTILITY
;		
;
;     BYTE  BIT 7       6       5       4       3       2       1       0 
;============================================================================
;       00	0	1	0	1	0	1	0	1
;----------------------------------------------------------------------------
;       01	0	0	1	1	0	0	0	0
;----------------------------------------------------------------------------
;       02	P	X	X	1	1	1	1       1
;----------------------------------------------------------------------------
;       03                    FILE NAME
;----------------------------------------------------------------------------
; 
;SWITCHES:
;
;	P - SEQUENTIAL FILE BIT (P=1, DOES NOT HAVE TO BE A PROGRAM 
;				      FILE)
;	X - DON'T CARE.
;
;PROTOCOL:
;	
;	BURST.
;
;
;OUTPUT:
;        BURST STATUS BYTE PRECEEDING EACH SECTOR TRANSFERED.
;	
;STATUS IS AS FOLLOWS:
;
;	0000000X ............. OK
;     *	00000010 ............. FILE NOT FOUND
;    **	00011111 ............. EOI
;	
;* VALUES BETWEEN THE RANGE 3-15 SHOULD BE CONSIDERED A FILE READ ERROR.
;** EOI STATUS BYTE, FOLLOWED BY NUMBER OF BYTES TO FOLLOW.
	.page
;STATUS BYTE BREAK DOWN
;		
;
;      BIT 7       6       5       4       3       2       1       0 
;============================================================================
;	 MODE	   DN	  SECTOR SIZE	   [  CONTROLLER  STATUS   ]
;----------------------------------------------------------------------------
; 
; Values Represented are in Binary
;
;	MODE - 1=MFM, 0=GCR
;	  DN - DRIVE NUMBER
;
; SECTOR SIZE - (MFM ONLY)
;	       00 .... 128 BYTE SECTORS
;	       01 .... 256 BYTE SECTORS	
;	       10 .... 512 BYTE SECTORS
;	       11 .... 1024 BYTE SECTORS
;
; CONTROLLER STATUS (GCR)
;	     000X .... OK
;            0010 .... SECTOR NOT FOUND
;	     0011 .... NO SYNC
;	     0100 .... DATA BLOCK NOT FOUND
;	     0101 .... DATA BLOCK CHECKSUM ERROR
;	     0110 .... FORMAT ERROR
;	     0111 .... VERIFY ERROR
;	     1000 .... WRITE PROTECT ERROR
;	     1001 .... HEADER BLOCK CHECKSUM ERROR
;	     1010 .... DATA EXTENDS INTO NEXT BLOCK
;	     1011 .... DISK ID MISMATCH/ DISK CHANGE
;	     1100 .... RESERVED
;	     1101 .... RESERVED
;	     1110 .... SYNTAX ERROR
;	     1111 .... NO DRIVE PRESENT
;
; CONTROLLER STATUS (MFM)
;	     000X .... OK
;            0010 .... SECTOR NOT FOUND
;	     0011 .... NO ADDRESS MARK
;	     0100 .... RESERVED
;	     0101 .... DATA CRC ERROR
;	     0110 .... FORMAT ERROR
;	     0111 .... VERIFY ERROR
;	     1000 .... WRITE PROTECT ERROR
;	     1001 .... HEADER BLOCK CHECKSUM ERROR
;	     1010 .... RESERVED
;	     1011 .... DISK CHANGE
;	     1100 .... RESERVED
;	     1101 .... RESERVED
;	     1110 .... SYNTAX ERROR
;	     1111 .... NO DRIVE PRESENT
	.page
;
;EXAMPLE BURST ROUTINES WRITTEN ON C 128
;	*=$1800
;
;  ROUTINE TO READ N-BLOCKS OF DATA
;  COMMAND CHANNEL MUST BE OPEN ON DRIVE
;  OPEN 15,8,15
;  BUFFER AND CMD_BUF, AND CMD_LENGTH MUST BE SETUP
;  PRIOR TO CALLING THIS COMMAND.
;
;serial  = $0a1c	; fast serial flag
;d2pra   = $dd00
;clkout  = $10
;d1icr   = $dc0d
;d1sdr   = $dc0c
;stat	 = $fa		
;buffer  = $fb		; $fb & $fc
;
;	lda  #15	; logical file number
;	ldx  #8		; device number
;	ldy  #15	; secondary address
;	jsr  setlfs	; setup logical file	
;
;	lda  #0		; noname
;	jsr  setnam	; setup file name
;
;	jsr  open	; open logical channel
;
; after the command channel is open subsequent calls should be from 'read'
;
;read   lda  #$00
;	sta  stat	; clear status
;	lda  serial
;	and  #%10111111	; clear bit 6 fast serial flag
;	sta  serial	
;	ldx  #15
;	jsr  chkout	; open channel for output
;	ldx  #0
;	ldy  cmd_length	; length of the command
;1$	lda  cmd_buf,x	; get command
;	jsr  bsout	; send the command
;	inx
;	dey
;	bne  1$
;
;	jsr  clrchn	; send eoi
;	bit  serial	; check speed of drive
;	bvc  error	; slow serial drive
;	
;	sei
;	bit  d1icr	; clear interrupt control reg
;	ldx  cmd_buf+5	; get # of sectors
;	lda  d2pra	; read serial port
;	eor  #clkout	; change state of clock
;	sta  d2pra	; store back
;	
;read_itlda  #8
;1$	bit  d1icr	; wait for byte
;	beq  1$	
;	
;	lda  d2pra	; read serial port
;	eor  #clkout	; change state of clock
;	sta  d2pra	; store back
;
;       Code to check status byte.
;
;   1) This code will check for mode
;      whether GCR or MFM.  
;   2) Verify sector size.
;   3) Check for error, if ok then continue.
;      On error, check error switch if set continue 
;      otherwise abort.
;   4) Verify switches
;
;	lda  d1sdr	; get data from serial data reg
;	sta  stat	; save status
;	and  #15
;	cmp  #2		; just check for (3)
;	bcs  error
;
;	ldy  #0		; even page
;top_rd	lda  #8
;1$	bit  d1icr	; wait for byte
;	beq  1$
;	
;	lda  d2pra	; toggle clock
;	eor  #clkout	
;	sta  d2pra
;
;	lda  d1sdr	; get data
;	sta  (buffer),y	; save data
;	iny
;	bne  top_rd	; continue for buffer size
;
;	dex
;	beq  done_read  ; done ?
;
;	inc  buffer+1	; next buffer
;	jmp  read_it	
;
;done_read
;	clc
;	.byte  $24
;error	sec
;	rts		; return to sender
;
;cmd_buf .byte 'U0',0,0,0,0,0
;
	.page
;	*=$1800
;
;  ROUTINE TO WRITE N-BLOCKS OF DATA
;  COMMAND CHANNEL MUST BE OPEN ON DRIVE
;  OPEN 15,8,15
;  BUFFER AND CMD_BUF, AND CMD_LENGTH MUST BE SETUP
;  PRIOR TO CALLING THIS COMMAND.
;
;serial  = $0a1c	; fast serial flag
;d2pra   = $dd00
;clkout  = $10
;old_clk = $fd
;clkin   = $40
;d1icr   = $dc0d
;d1sdr   = $dc0c
;stat    = $fa		
;buffer  = $fb		; $fb & $fc
;mmureg	 = $d505
;d1timh	 = $dc05	; timer a high
;d1timl  = $dc04	; timer a low
;d1cra   = $dc0e	; control reg a
;
;	lda  #15	; logical file number
;	ldx  #8		; device number
;	ldy  #15	; secondary address
;	jsr  setlfs	; setup logical file	
;	jsr  setnam	; setup file name
;
;	jsr  open	; open logical channel
;
;
; after the command channel is open subsequent calls should be from 'write'
;
;write	lda  #$00
;	sta  stat
;	lda  serial
;	and  #%10111111	; clear bit 6 fast serial flag
;	sta  serial	
;	ldx  #15
;	jsr  chkout	; open channel for output
;	
;	ldx  #0
;	ldy  cmd_length	; length of the command
;1$	lda  cmd_buf,x	; get command
;	jsr  bsout	; send the command
;	inx
;	dey
;	bne  1$
;
;	jsr  clrchn	; send eoi
;
;	bit  serial	; check speed of drive
;	bvc  error
;
;	sei
;	lda  #clkin
;	sta  old_clk	; clock starts high
;
;	ldy  #0		; even page
;	ldx  cmd_buf+5	; get # of sectors
;writ_itjsr  spout	; serial port out
;1$	lda  d2pra	; check clock
;	cmp  d2pra	; debounce
;	bne  1$
;
;	eor  old_clk
;	and  #clkin
;	beq  1$
;
;	lda  old_clk	; change status of old clock
;	eor  #clkin
;	sta  old_clk
;
;	lda  (buffer),y	; send data
;	sta  d1sdr	
;
;********* OPTIONAL **********
;
;	lda  #8		; put code here or before spin
;2$	bit  d1icr	; wait for transmission time
;	beq  2$
;
;*****************************
;
;	iny
;	bne  1$		; continue for buffer size
;
; talker turn around
;
;  WAIT CODE HERE (OPTIONAL)
;  
;	jsr  spin	; serial port input
;	bit  d1icr	; clear pending
;	jsr  clklo	; set clk low, tell him we are ready
;	
;	lda  #8
;3$	bit  d1icr 	; wait for status byte
;	beq  3$
;	
;	lda  d1sdr	; get data from serial data reg
;	sta  stat	; save status
;	jsr  clkhi	; set clock high	
;	
;       Code to check status byte.
;
;   1) This code will check for mode
;      whether GCR or MFM.  
;   2) Verify sector size.
;   3) Check for error, if ok then continue.
;      On error, check error switch if set continue 
;      otherwise abort.
;   4) Verify switches
;
;	lda  stat	; retrieve status
;	and  #15
;	cmp  #2		; just chk for error only (3)
;	bcs  error	; finish
;
;	dex
;	beq  done_wr    ; done ?
;
;	inc  buffer+1	; next buffer
;	jmp  wrt_it
;
;done_wr 
;	cli
;	.byte $24
;error	sec
;	rts 
;
;cmd_buf .byte 'U0',2,0,0,0,0
;
; subroutines
;
; NOTE: spout and spin are C128 kernal vectors (refer to C128 users manual)
;
;
;spout	lda  mmureg	; change serial direction to output
;	ora  #$08
;	sta  mmureg
;	lda  #$7f
;	sta  d1icr	; no irq's
;	lda  #$00
;	sta  d1timh	
;	lda  #$03
;	sta  d1timl	; low 6 us bit (fastest)
;	lda  d1cra
;	and  #$80	; keep TOD
;	ora  #$55
;	sta  d1cra	; setup CRA for output
;	bit  d1icr	; clr pending
;	rts
;
;spin   lda  d1cra
;	and  #$80	; save TOD
;	ora  #$08	; change fast serial for input
;	sta  d1cra	; input for 6526
;	lda  mmureg
;	and  #$f7
;	sta  mmureg	; mmu serial direction in
;	rts
;
;clklo	lda  d2pra	; set clock low
;	ora  #clkout
;	sta  d2pra
;	rts
;	
;clkhi	lda  d2pra	; set clock high
;	and  #$ff-clkout
;	sta  d2pra
;	rts
;	
; ****** END EXAMPLES ******
;
	.page
; (1) FAST READ 

fstrd   sta  cmd	; save command for dos
	sta  ctl_cmd	; save command for stbctt
	
        lda  ifr1	; disk change
        lsr  a
	bcc  frd_00	; br, ok

	ldx  #%00001011 ; no channel
	.byte skip2
ndkrd	ldx  #%01001111 ; no drive

fail	jsr  upinst     ; update dkmode
finbad	jsr  statqy	; wait for state handshake
final	cpx  #2
	bcs  exbad

	rts

exbad   txa             ; retrieve status
	and  #15	; bits 0-3 only
	ldx  #0         ; jobnum
        jmp  error      ; controller error entry
  

frd_00	jsr  spout       

	bit  dkmode     ; gcr or mfm ?
        bpl  rdgcr


; 	MFM READ 

	lda  #9         ; read cmd
        jmp prcmd       

rdgcr	jsr  autoi      ; check for auto init

frd_01	cli             ; let controller run
	lda  switch	; check for B
	and  #%00100000
	bne  bfonly

        lda  cmdbuf+3   ; get track
        sta  hdrs        
        lda  cmdbuf+4   ; get sector
        sta  hdrs+1      
        ldx  #0         ; job #0
        lda  ctl_cmd    ; read ($80) or fread ($88)
        sta  jobs,x      
	jsr  stbctt     ; hit controller hard

        sei      	; disable irqs
	jsr  upinst	; update controller status

	bit  switch	; check E
	bvs  igerr	

        cpx  #2         ; error
        bcs  fail

igerr   jsr  hskrd      ; handshake on state of clkin

	lda  switch	; check B
	bmi  notran

bfonly  ldy  #0         ; even page
frd_02	lda  $0300,y    ; get data
        sta  ctl_dat    ; setup data
        jsr  hskrd      ; handshake on state
        iny      
        bne  frd_02

notran	dec  cmdbuf+5   ; any more sectors ?
        beq  exrd        

        jsr  sektr      ; next sector
        jmp  frd_01	; more to do

exrd    cli
        jmp  chksee     ; next track ?

	.page
; (2) FAST WRITE 

fstwrt  
	sta  cmd        ; save command for dos
        lda  ifr1	; disk change
        lsr  a
	bcc  fwrt_0	; br, ok

	ldx  #%00001011	; no channel
	.byte  skip2
ndkwrt	ldx  #%01001111	; no drv 1
	stx  ctl_dat	; save status
	lda  switch	; set internal switch
        ora  #%00001000
	sta  switch

fwrt_0  bit  dkmode     ; gcr or mfm ?
        bpl  wrtgcr


;	MFM WRITE

	lda  #10
	jmp  prcmd	

wrtgcr	jsr  autoi      ; chk for autoi
	
	lda  switch	; transmision required
	bmi  notrx

fwrt_1	sei             ; no irqs
	ldy  #0         ; even page
fwrt_2  lda  pb		; debounce
        eor  #clkout    ; toggle state of clock
	bit  icr	; clear pending
        sta  pb
fwrt_4	lda  pb
        bpl  fwrt_3	; br, attn not low 

	jsr  tstatn     ; chk for atn

fwrt_3  lda  icr	; wait for byte
        and  #8
        beq  fwrt_4	; chk for attn low only

        lda  sdr	; get data
        sta  $0300,y    ; put away data
        iny      
        bne  fwrt_2	; more ?

	jsr  clkhi	; release clock
notrx   cli             ; let controller run
	lda  switch	; check for buffer transfer only
	and  #%00100000
	bne  nowrt

	lda  switch	; check internal
	and  #%00001000
	beq  notrx0	; br, ok...

	ldx  ctl_dat	; get error
	jmp  fail	; abort

notrx0  lda  cmdbuf+3   ; get track
        sta  hdrs        
        lda  cmdbuf+4   ; get sector
        sta  hdrs+1      
	ldx  #0         ; job #0
	lda  #write	; get write job
        sta  jobs,x      
        jsr  stbctt     ; wack controller in the head

        sei             ; no irqs ok !
        jsr  spout      ; go output
	jsr  upinst	; update status
        jsr  hskrd	; send it
	jsr  burst	; wait for reverse
	jsr  spinp
        cli             ; irqs ok now
        
	bit  switch	; check error abort switch
	bvs  nowrt

        cpx  #2         ; error on job ?
        bcs  fwrt_5	; abort ?

nowrt   dec  cmdbuf+5   ; more sectors ?
        beq  fwrt_6

        jsr  sektr      ; increment sector
        jmp  fwrt_1

fwrt_5	jmp  exbad

fwrt_6  cli		; done
	jmp  chksee	; next track ?

	.page
; (3) FAST SEEK

fstsek  lda  cmdbuf+2	; check drive number
	and  #1
        bne  3$		; drive 1 - error

	lda  #1
	sta  ifr1	; new disk
	lda  #5		; read address cmd
	jsr  prcmd	; give it to controller
	
	ldx  mfmcmd	; get status
	cpx  #2		; ok ?
	bcc  2$

	ldx  #0
	stx  dkmode 	; force gcr,drv0,ok
	lda  #seek	; seek

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  ptch0c	; *** rom ds 12/08/86 ***, set track
;	sta  cmd	; give command to dos

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	sta  jobs,x	; give job to controller
	jsr  stbctt	; bang the controller
	.byte  skip2	; return status
3$	ldx  #%01001111 ; no drv 1
2$	jmp  fail	; update status

	.page
; (4) BURST FORMAT MFM/GCR

fstfmt  lda  cmdbuf+2   ; check drive number
	and  #1
	bne  1$

	lda  cmdbuf+3	; format gcr or mfm ?
	bpl  2$


;       FORMAT IN MFM

	lda  #8		; format disk in mfm
	jmp  prcmd

2$	lda  #0
	sta  dkmode     ; force GCR,DRV0
	sta  nodrv	; drive ok
	lda  cmdbuf+4
	sta  dskid	; low id
	lda  cmdbuf+5
	sta  dskid+1	; high id
	
	jsr  clrchn	; close all channels
	lda  #1
	sta  track	; track one
	lda  #$ff
	sta  jobrtn	; set error recovery
	jsr  ptch55	; *** rom ds 06/18/85 ***, format

	tax		; save error
	.byte skip2
1$	ldx  #%01001111
	jsr  upinst	; update status
	jmp  final	; finish up ...
	
	.page
; (5) SOFT INTERLEAVE

cpmint  sei
	bit  switch	; read ?
	bpl  1$		; br, write

	jsr  spout  	; serial port output
	lda  cpmit	; get current interleave
	sta  ctl_dat	; send it
	jmp  hskrd

1$	ldx  cmdsiz      
        cpx  #4
        bcs  2$

	ldx  #%00001110 ; syntax
	jsr  upinst	; update dkmode

        lda  #badcmd     
        jmp  cmderr      

2$	lda  cmdbuf+3
	sta  cpmit      ; save
	rts
	.page
; (6) QUERY DISK FORMAT

querdk  jsr  fstsek	; check disk format
	bit  dkmode	; mfm or gcr ?
	bpl  2$
	
	lda  #13
	jsr  prcmd	; generate sector table

	ldx  mfmcmd	; status ok ?
	cpx  #2
	bcs  1$

	jsr  maxmin	; determine low/high sector
	jsr  fn_int	; determine interleave
	txa		; hard interleave 
	pha		; save it

1$	sei		; no irq's
	jsr  spout	; serial port output
	lda  dkmode	; get status and send it
	sta  ctl_dat
	jsr  hskrd	; transmit status

	ldx  mfmcmd	; was status ok ?
	cpx  #2
	bcs  3$

	lda  cpmsek	; get number of sectors
	sta  ctl_dat	
	jsr  hskrd	; send it

	lda  cmd_trk	; get logical track number
	sta  ctl_dat
	jsr  hskrd	; send track

	lda  minsek
	sta  ctl_dat	; send min.
	jsr  hskrd	; wait for handshake

	lda  maxsek	; send max.
	sta  ctl_dat
	jsr  hskrd	; wait for handshake

	pla		; get interleave
	sta  ctl_dat	; send interleave

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch63	; *** rom ds 01/23/86 ***, add sector table
;	jsr  hskrd	; wait for handshake

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

2$	rts	
3$	pla		; restore stack
	jmp  exbad
	.page
; (7) QUERY/INIT STATUS

inqst   bit  switch	; read/write op ?
	bpl  wrstat
	
	bit  switch	; wp latch ?
	bvc  statqy

	lda  ifr1
	lsr  a
	bcc  statqy	; ok, give old status

	lda  dkmode
	and  #$f0	; clr old
	ora  #$0b	; disk change
	sta  dkmode	; updated status	

statqy  sei 		; disable irqs
	jsr  spout      ; serial port out
	lda  dkmode 	; get status
	sta  ctl_dat	
	jsr  hskrd	; 6526 will send it	

	lda  #0
	sta  erword
	
exchnl  jsr  spinp	; serial port input
exchn2	cli
	rts

wrstat	lda  cmdbuf+3	; new value
	sta  dkmode	
	bit  switch	; wp latch ?
	bvc  1$

	lda  #1
	sta  ifr1	; new disk

1$	rts
	.page
; (8) BACKUP DISK

duplc1  ldx  #%00001110
	jsr  upinst	; update dkmode
	
	lda  #badcmd
	jmp  cmderr

	.page
;       SUBROUTINES

chksee  lda  cmdsiz     ; chk for next track
        cmp  #7          
        bcc  3$

	lda  hdrs	; where are we ?
	tay		; save
	sbc  #1		; one less			
	asl  a
	sta  cur_trk	
	cpy  #36
	php		; save status
	ldy  cmdbuf+6	; destination
        sty  drvtrk
	dey
	sty  cmd_trk	; initial detent
	cpy  #35
	ror  a		; rotate carry into neg 
	plp
	and  #$80	; set/clr neg
	bcc  1$		; br, we are on side zero
	
	bmi  2$		; br, we are on side one & want to goto side one
	
	clc
	lda  cmd_trk	; setup for pete seke
	adc  #35	; select side
	sta  cmd_trk	
	bmi  2$		; bra more or less

1$	bpl  2$		; br, we are on side zero & want to goto side zero

	sec	
	lda  cmd_trk
	sbc  #35	; select side
	sta  cmd_trk	
2$	jmp  seke	; do it 
3$	rts	

;******************************************************

upinst  stx  ctl_dat
	lda  dkmode     ; update main status w/ controller status
        and  #%11110000 ; clear old controller status
        ora  ctl_dat    ; or in controller status
        sta  dkmode     ; updated
	sta  ctl_dat
	rts

;******************************************************

hsktst  jsr  tstatn     ; test for atn
hskrd
1$	lda  pb		; debounce
        cmp  pb
        bne  1$

	and  #$ff	; set/clr neg flag
        bmi  hsktst     ; br, attn low 

        eor  fastsr     ; wait for state chg
        and  #4          
        beq  1$

hsksnd  lda  ctl_dat    ; retrieve data

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch68	; *** rom ds 05/16/86 ***
;       sta  sdr	; send it

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        lda  fastsr      
        eor  #4         ; change state of clk
        sta  fastsr      
        lda  #8
1$	bit  icr	; wait transmission time
        beq  1$

        rts      

;******************************************************

sektr   lda  cmdbuf+3   ; get track
	cmp  #36	; side one ?
	bcc  1$

	sbc  #35	; get offset
1$	tax
	lda  num_sec-1,x
	tax
	dex
        stx  ctl_dat    ; save
	clc
        lda  cmdbuf+4   ; get sector
        adc  cpmit      ; add interleave
        cmp  ctl_dat    ; less than maxsec ?
        bcc  gtsec       
        sbc  ctl_dat    ; rap around
        beq  oksec      ; special case on max
        sec      
        sbc  #1         ; less one
	.byte skip2
oksec   lda  ctl_dat      
gtsec   sta  cmdbuf+4   ; new sector
        lda  #fread     ; exread job
        sta  ctl_cmd      
        rts      

;******************************************************

stbctr	ldx  jobnum
	php
	cli		; let controller run free
	jsr  stbctl     ; strobe controller ( hit him hard )
        cmp  #2         ; was there an error ?
        bcc  1$		; br, nope

	jsr  stbret	; let DOS retry
	lda  jobs,x	; get error

1$	tax     	; return status in .x
	plp		; restore status
	rts

;******************************************************

stbctt	ldx  #0		; buffer zero
	php
	sei		; no irq's during i/o access
	lda  ledprt
	ora  #8
	sta  ledprt	; led on
	cli		; let controller run free
	jsr  stbctl     ; strobe controller ( hit him hard )
        cmp  #2         ; was there an error ?
        bcc  1$		; br, nope
	
	jsr  stbret	; let DOS retry

1$	sei
	lda  ledprt	
	and  #$ff-8
	sta  ledprt	; led off
	lda  jobs,x	; get error return
	tax     	; return status in .x
	plp		; restore original status
	rts

;******************************************************

stbret	lda  #$ff       ; micro-step if you can
	sta  jobrtn	; set error recovery on
	stx  jobnum	; set job #
	lda  cmdbuf+2	; get command
	sta  ctl_cmd	; reset command
	sta  cmd	; set for dos
	sta  lstjob,x	; set last job
	sta  jobs,x	; -04 fix 02/24/86 ds, give it to the controller
	jsr  stbctl	; wait for it
	jmp  watjob	; let dos clean it up

;******************************************************

burtst	jsr  tstatn     ; test for atn
burst
1$	lda  pb		; debounce
        cmp  pb
        bne  1$		; same ?

	and  #$ff	; set/clr neg flag
        bmi  burtst	; br, attn low 

        eor  fastsr     ; wait for state chg
        and  #4          
        beq  1$
	
	lda  fastsr
	eor  #4
	sta  fastsr     ; state change
	rts

;******************************************************
	.page   
	.subttl 'fndrel.src'        
;********************************
;*
;* find relative file
;*
;* version 2.5
;*
;*
;* inputs
;*  recl   - 1byte=lo record #
;*  rech   - 1byte=hi record #
;*  rs 	   - 1byte=record size
;*  recptr - 1byte=first byte
;*            wanted from record
;*
;* outputs
;*  ssnum  - 1byte=side sector #
;*  ssind  - 1byte=index into ss
;*  relptr - 1byte=ptr to first
;*            byte wanted
;*
;********************************

fndrel  jsr  mulply     ; result=rn*rs+rp
        jsr  div254     ; divide by 254
        lda  accum+1    ; save remainder
        sta  relptr      
        jsr  div120     ; divide by 120
        inc  relptr      
        inc  relptr      
        lda  result     ; save quotient
        sta  ssnum       
        lda  accum+1    ; save remainder
        asl  a          ; calc index into ss
        clc      
        adc  #16        ; skip link table
        sta  ssind       
        rts      



; multiply

; result=recnum*rs+recptr

;  destroys a,x

mulply  jsr  zerres     ; result=0
        sta  accum+3    ; a=0
        ldx  lindx      ; get index
        lda  recl,x      
        sta  accum+1     
        lda  rech,x      
        sta  accum+2     
        bne  mul25      ; adjust for rec #1 &...
        lda  accum+1    ; ...#0 = 1st rec
        beq  mul50       
mul25   lda  accum+1     
        sec      
        sbc  #1          
        sta  accum+1     
        bcs  mul50       
        dec  accum+2     
mul50            
        lda  rs,x       ; copy recsiz
        sta  temp        
mul100  lsr  temp       ; do an add ?
        bcc  mul200     ; no
        jsr  addres     ; result=result+accum+1,2,3
mul200  jsr  accx2      ; 2*(accum+1,2,3)
        lda  temp       ; done ?
        bne  mul100     ; no
        lda  recptr     ; add in last bit
        clc      
        adc  result      
        sta  result      
        bcc  mul400     ; skip no carry
        inc  result+1    
        bne  mul400      
        inc  result+2    
mul400  rts      



; divide

; result=quotient ,accum+1=remainder

;  destroys a,x

div254  lda  #254       ; divide by 254
	.byte  skip2	; skip two bytes
div120  lda  #120       ; divide by 120
        sta  temp       ; save divisor
        ldx  #3         ; swap accum+1,2,3 with
div100  lda  accum,x    ; result,1,2
        pha      
        lda  result-1,x          
        sta  accum,x     
        pla      
        sta  result-1,x          
        dex      
        bne  div100      
        jsr  zerres     ; result=0
div150  ldx  #0          
div200  lda  accum+1,x  ; divide by 256
        sta  accum,x     
        inx      
        cpx  #4         ; done ?
        bcc  div200     ; no
        lda  #0         ; zero hi byte
        sta  accum+3     
        bit  temp       ; a div120 ?
        bmi  div300     ; no
        asl  accum      ; only divide by 128
        php     	; save carry
        lsr  accum      ; normalize
        plp     	; restore carry
        jsr  acc200     ; 2*(x/256)=x/128
div300  jsr  addres     ; total a quotient
        jsr  accx2      ; a=2*a
        bit  temp       ; a div120 ?
        bmi  div400     ; no
        jsr  accx4      ; a=4*(2*a)=8*a
div400  lda  accum      ; add in remainder
        clc      
        adc  accum+1     
        sta  accum+1     
        bcc  div500      
        inc  accum+2     
        bne  div500      
        inc  accum+3     
div500  lda  accum+3    ; test < 256
        ora  accum+2     
        bne  div150     ; crunch some more
        lda  accum+1    ; is remainder < divisor
        sec      
        sbc  temp        
        bcc  div700     ; yes
        inc  result     ; no - fix result
        bne  div600      
        inc  result+1    
        bne  div600      
        inc  result+2    
div600  sta  accum+1    ; new remainder
div700  rts      



; zero result

zerres  lda  #0          
        sta  result      
        sta  result+1    
        sta  result+2    
        rts      


; multiply accum by 4

accx4   jsr  accx2       

; multiply accum by 2

accx2   clc      
acc200  rol  accum+1     
        rol  accum+2     
        rol  accum+3     
        rts      



; add accum to result

;  result=result+accum+1,2,3

addres  clc      
        ldx  #$fd        
add100  lda  result+3,x          
        adc  accum+4,x   
        sta  result+3,x          
        inx      
        bne  add100      
        rts      
        .page  
;     10/25/84 ds

gcrtb3  .byte    $ff    ; 0 
	.byte    $ff    ; 1  
	.byte    $ff    ; 2  
	.byte    $ff    ; 3
	.byte    $ff    ; 4
	.byte    $ff    ; 5  
	.byte    $ff    ; 6  
	.byte    $ff    ; 7
	.byte    $ff    ; 8  
	.byte    $ff    ; 9  
	.byte    $ff    ; a  
	.byte    $ff    ; b
	.byte    $ff    ; c  
	.byte    $ff    ; d  
	.byte    $ff    ; e  
	.byte    $ff    ; f
	.byte    $ff    ; 10  
	.byte    $ff    ; 11  
	.byte    $ff    ; 12  
	.byte    $ff    ; 13
	.byte    $ff    ; 14  
	.byte    $ff    ; 15  
	.byte    $ff    ; 16  
	.byte    $ff    ; 17  
	.byte    $ff    ; 18
	.byte    $ff    ; 19  
	.byte    $ff    ; 1a  
	.byte    $ff    ; 1b  
	.byte    $ff    ; 1c  
	.byte    $ff    ; 1d  
	.byte    $ff    ; 1e  
	.byte    $ff    ; 1f  
	.byte    $ff    ; 20 
	.byte    $ff    ; 21 
	.byte    $ff    ; 22
	.byte    $ff    ; 23  
	.byte    $08    ; 24  
	.byte    $08    ; 25  
	.byte    $08    ; 26  
	.byte    $08    ; 27  

	.byte    $00    ; 28  
        .byte    $00    ; 29  
	.byte    $00    ; 2a  
	.byte    $00    ; 2b
	.byte    $01    ; 2c  
	.byte    $01    ; 2d  
	.byte    $01    ; 2e  
	.byte    $01    ; 2f
	.byte    $ff    ; 30
	.byte    $ff    ; 31
	.byte    $ff    ; 32  
	.byte    $ff    ; 33  
	.byte    $0c    ; 34  
	.byte    $0c    ; 35  
	.byte    $0c    ; 36  
	.byte    $0c    ; 37
	.byte    $04    ; 38
	.byte    $04    ; 39  
	.byte    $04    ; 3a  
	.byte    $04    ; 3b
	.byte    $05    ; 3c  
	.byte    $05    ; 3d  
	.byte    $05    ; 3e  
	.byte    $05    ; 3f
	.byte    $ff    ; 40  
	.byte    $ff    ; 41
	.byte    $ff    ; 42  
	.byte    $ff    ; 43  
	.byte    $ff    ; 44
	.byte    $ff    ; 45  
	.byte    $ff    ; 46  
	.byte    $ff    ; 47  
	.byte    $02    ; 48
	.byte    $02    ; 49  
	.byte    $02    ; 4a
	.byte    $02    ; 4b
	.byte    $03    ; 4c
	.byte    $03    ; 4d  
	.byte    $03    ; 4e
	.byte    $03    ; 4f

	.byte    $ff    ; 50  
	.byte    $ff    ; 51
	.byte    $ff    ; 52
	.byte    $ff    ; 53  
	.byte    $0f    ; 54
	.byte    $0f    ; 55  
	.byte    $0f    ; 56
	.byte    $0f    ; 57

	.byte    $06    ; 58  
	.byte    $06    ; 59  
	.byte    $06    ; 5a
	.byte    $06    ; 5b
	.byte    $07    ; 5c
	.byte    $07    ; 5d  
	.byte    $07    ; 5e  
	.byte    $07    ; 5f  

	.byte    $ff    ; 60  
	.byte    $ff    ; 61
	.byte    $ff    ; 62
	.byte    $ff    ; 63  
	.byte    $09    ; 64
	.byte    $09    ; 65  
	.byte    $09    ; 66
	.byte    $09    ; 67

	.byte    $0a    ; 68  
	.byte    $0a    ; 69  
	.byte    $0a    ; 6a
	.byte    $0a    ; 6b
	.byte    $0b    ; 6c
	.byte    $0b    ; 6d  
	.byte    $0b    ; 6e
	.byte    $0b    ; 6f

	.byte    $ff    ; 70  
	.byte    $ff    ; 71
	.byte    $ff    ; 72
	.byte    $ff    ; 73  
	.byte    $0d    ; 74
	.byte    $0d    ; 75  
	.byte    $0d    ; 76
	.byte    $0d    ; 77

	.byte    $0e    ; 78  
	.byte    $0e    ; 79  
	.byte    $0e    ; 7a
	.byte    $0e    ; 7b
	.byte    $ff    ; 7c
	.byte    $ff    ; 7d
	.byte    $ff    ; 7e  
	.byte    $ff    ; 7f  

	.byte    $ff    ; 80  
	.byte    $ff    ; 81
	.byte    $ff    ; 82  
	.byte    $ff    ; 83  
	.byte    $ff    ; 84  
	.byte    $ff    ; 85  
	.byte    $ff    ; 86  
	.byte    $ff    ; 87  

	.byte    $ff    ; 88  
	.byte    $ff    ; 89  
	.byte    $ff    ; 8a  
	.byte    $ff    ; 8b  
	.byte    $ff    ; 8c  
	.byte    $ff    ; 8d  
	.byte    $ff    ; 8e  
	.byte    $ff    ; 8f

	.byte    $ff    ; 90  
	.byte    $ff    ; 91
	.byte    $ff    ; 92  
	.byte    $ff    ; 93  
	.byte    $ff    ; 94  
	.byte    $ff    ; 95  
	.byte    $ff    ; 96  
	.byte    $ff    ; 97  

	.byte    $ff    ; 98  
	.byte    $ff    ; 99  
	.byte    $ff    ; 9a  
	.byte    $ff    ; 9b  
	.byte    $ff    ; 9c  
	.byte    $ff    ; 9d  
	.byte    $ff    ; 9e  
	.byte    $ff    ; 9f

	.byte    $ff    ; a0
	.byte    $ff    ; a1
	.byte    $ff    ; a2  
	.byte    $ff    ; a3  
	.byte    $08    ; a4  
	.byte    $08    ; a5  
	.byte    $08    ; a6  
	.byte    $08    ; a7  

	.byte    $00    ; a8  
	.byte    $00    ; a9  
	.byte    $00    ; aa  
	.byte    $00    ; ab  
	.byte    $01    ; ac  
	.byte    $01    ; ad  
	.byte    $01    ; ae  
	.byte    $01    ; af  

	.byte    $ff    ; b0  
	.byte    $ff    ; b1
	.byte    $ff    ; b2  
	.byte    $ff    ; b3  
	.byte    $0c    ; b4  
	.byte    $0c    ; b5  
	.byte    $0c    ; b6  
	.byte    $0c    ; b7  

	.byte    $04    ; b8  
	.byte    $04    ; b9  
	.byte    $04    ; ba  
	.byte    $04    ; bb  
	.byte    $05    ; bc  
	.byte    $05    ; bd
	.byte    $05    ; be  
	.byte    $05    ; bf  

	.byte    $ff    ; c0  
	.byte    $ff    ; c1
	.byte    $ff    ; c2  
	.byte    $ff    ; c3  
	.byte    $ff    ; c4  
	.byte    $ff    ; c5  
	.byte    $ff    ; c6  
	.byte    $ff    ; c7

	.byte    $02    ; c8  
	.byte    $02    ; c9  
	.byte    $02    ; ca  
	.byte    $02    ; cb  
	.byte    $03    ; cc  
	.byte    $03    ; cd  
	.byte    $03    ; ce  
	.byte    $03    ; cf

	.byte    $ff    ; d0  
	.byte    $ff    ; d1
	.byte    $ff    ; d2  
	.byte    $ff    ; d3  
	.byte    $0f    ; d4  
	.byte    $0f    ; d5  
	.byte    $0f    ; d6  
	.byte    $0f    ; d7  

	.byte    $06    ; d8  
	.byte    $06    ; d9  
	.byte    $06    ; da  
	.byte    $06    ; db  
	.byte    $07    ; dc  
	.byte    $07    ; dd  
	.byte    $07    ; de  
	.byte    $07    ; df  

	.byte    $ff    ; e0  
	.byte    $ff    ; e1
	.byte    $ff    ; e2  
	.byte    $ff    ; e3  
	.byte    $09    ; e4
	.byte    $09    ; e5
	.byte    $09    ; e6
	.byte    $09    ; e7

	.byte    $0a    ; e8  
	.byte    $0a    ; e9  
	.byte    $0a    ; ea  
	.byte    $0a    ; eb  
	.byte    $0b    ; ec  
	.byte    $0b    ; ed  
	.byte    $0b    ; ee
	.byte    $0b    ; ef

	.byte    $ff    ; f0  
	.byte    $ff    ; f1
	.byte    $ff    ; f2  
	.byte    $ff    ; f3  
	.byte    $0d    ; f4  
	.byte    $0d    ; f5  
	.byte    $0d    ; f6  
	.byte    $0d    ; f7  

	.byte    $0e    ; f8  
	.byte    $0e    ; f9  
	.byte    $0e    ; fa  
	.byte    $0e    ; fb  
	.byte    $ff    ; fc  
	.byte    $ff    ; fd  
 	.byte    $ff    ; fe  
	.byte    $ff    ; ff  
	.page  
;     10/25/84 ds

gcrtb4  .byte    $ff    ; 0 
	.byte    $ff    ; 1  
	.byte    $ff    ; 2  
	.byte    $ff    ; 3
	.byte    $ff    ; 4
	.byte    $ff    ; 5  
	.byte    $ff    ; 6  
	.byte    $ff    ; 7
	.byte    $ff    ; 8  
	.byte    $08    ; 9  
	.byte    $00    ; a  
	.byte    $01    ; b
	.byte    $ff    ; c  
	.byte    $0c    ; d  
	.byte    $04    ; e  
	.byte    $05    ; f
	.byte    $ff    ; 10  
	.byte    $ff    ; 11  
	.byte    $02    ; 12  
	.byte    $03    ; 13
	.byte    $ff    ; 14  
	.byte    $0f    ; 15  
	.byte    $06    ; 16  
	.byte    $07    ; 17  
	.byte    $ff    ; 18
	.byte    $09    ; 19  
	.byte    $0a    ; 1a  
	.byte    $0b    ; 1b  
	.byte    $ff    ; 1c  
	.byte    $0d    ; 1d  
	.byte    $0e    ; 1e  
	.byte    $ff    ; 1f  
	.byte    $ff    ; 20 
	.byte    $ff    ; 21 
	.byte    $ff    ; 22
	.byte    $ff    ; 23  
	.byte    $ff    ; 24  
	.byte    $ff    ; 25  
	.byte    $ff    ; 26  
	.byte    $ff    ; 27  
	.byte    $ff    ; 28  
        .byte    $08    ; 29  
	.byte    $00    ; 2a  
	.byte    $01    ; 2b
	.byte    $ff    ; 2c  
	.byte    $0c    ; 2d  
	.byte    $04    ; 2e  
	.byte    $05    ; 2f
	.byte    $ff    ; 30
	.byte    $ff    ; 31
	.byte    $02    ; 32  
	.byte    $03    ; 33  
	.byte    $ff    ; 34  
	.byte    $0f    ; 35  
	.byte    $06    ; 36  
	.byte    $07    ; 37
	.byte    $ff    ; 38
	.byte    $09    ; 39  
	.byte    $0a    ; 3a  
	.byte    $0b    ; 3b
	.byte    $ff    ; 3c  
	.byte    $0d    ; 3d  
	.byte    $0e    ; 3e  
	.byte    $ff    ; 3f
	.byte    $ff    ; 40  
	.byte    $ff    ; 41
	.byte    $ff    ; 42  
	.byte    $ff    ; 43  
	.byte    $ff    ; 44
	.byte    $ff    ; 45  
	.byte    $ff    ; 46  
	.byte    $ff    ; 47  
	.byte    $ff    ; 48
	.byte    $08    ; 49  
	.byte    $00    ; 4a
	.byte    $01    ; 4b
	.byte    $ff    ; 4c
	.byte    $0c    ; 4d  
	.byte    $04    ; 4e
	.byte    $05    ; 4f

	.byte    $ff    ; 50  
	.byte    $ff    ; 51
	.byte    $02    ; 52
	.byte    $03    ; 53  
	.byte    $ff    ; 54
	.byte    $0f    ; 55  
	.byte    $06    ; 56
	.byte    $07    ; 57

	.byte    $ff    ; 58  
	.byte    $09    ; 59  
	.byte    $0a    ; 5a
	.byte    $0b    ; 5b
	.byte    $ff    ; 5c
	.byte    $0d    ; 5d  
	.byte    $0e    ; 5e  
	.byte    $ff    ; 5f  

	.byte    $ff    ; 60  
	.byte    $ff    ; 61
	.byte    $ff    ; 62
	.byte    $ff    ; 63  
	.byte    $ff    ; 64
	.byte    $ff    ; 65  
	.byte    $ff    ; 66
	.byte    $ff    ; 67

	.byte    $ff    ; 68  
	.byte    $08    ; 69  
	.byte    $00    ; 6a
	.byte    $01    ; 6b
	.byte    $ff    ; 6c
	.byte    $0c    ; 6d  
	.byte    $04    ; 6e
	.byte    $05    ; 6f

	.byte    $ff    ; 70  
	.byte    $ff    ; 71
	.byte    $02    ; 72
	.byte    $03    ; 73  
	.byte    $ff    ; 74
	.byte    $0f    ; 75  
	.byte    $06    ; 76
	.byte    $07    ; 77

	.byte    $ff    ; 78  
	.byte    $09    ; 79  
	.byte    $0a    ; 7a
	.byte    $0b    ; 7b
	.byte    $ff    ; 7c
	.byte    $0d    ; 7d
	.byte    $0e    ; 7e  
	.byte    $ff    ; 7f  

	.byte    $ff    ; 80  
	.byte    $ff    ; 81
	.byte    $ff    ; 82  
	.byte    $ff    ; 83  
	.byte    $ff    ; 84  
	.byte    $ff    ; 85  
	.byte    $ff    ; 86  
	.byte    $ff    ; 87  

	.byte    $ff    ; 88  
	.byte    $08    ; 89  
	.byte    $00    ; 8a  
	.byte    $01    ; 8b  
	.byte    $ff    ; 8c  
	.byte    $0c    ; 8d  
	.byte    $04    ; 8e  
	.byte    $05    ; 8f

	.byte    $ff    ; 90  
	.byte    $ff    ; 91
	.byte    $02    ; 92  
	.byte    $03    ; 93  
	.byte    $ff    ; 94  
	.byte    $0f    ; 95  
	.byte    $06    ; 96  
	.byte    $07    ; 97  

	.byte    $ff    ; 98  
	.byte    $09    ; 99  
	.byte    $0a    ; 9a  
	.byte    $0b    ; 9b  
	.byte    $ff    ; 9c  
	.byte    $0d    ; 9d  
	.byte    $0e    ; 9e  
	.byte    $ff    ; 9f

	.byte    $ff    ; a0
	.byte    $ff    ; a1
	.byte    $ff    ; a2  
	.byte    $ff    ; a3  
	.byte    $ff    ; a4  
	.byte    $ff    ; a5  
	.byte    $ff    ; a6  
	.byte    $ff    ; a7  

	.byte    $ff    ; a8  
	.byte    $08    ; a9  
	.byte    $00    ; aa  
	.byte    $01    ; ab  
	.byte    $ff    ; ac  
	.byte    $0c    ; ad  
	.byte    $04    ; ae  
	.byte    $05    ; af  

	.byte    $ff    ; b0  
	.byte    $ff    ; b1
	.byte    $02    ; b2  
	.byte    $03    ; b3  
	.byte    $ff    ; b4  
	.byte    $0f    ; b5  
	.byte    $06    ; b6  
	.byte    $07    ; b7  

	.byte    $ff    ; b8  
	.byte    $09    ; b9  
	.byte    $0a    ; ba  
	.byte    $0b    ; bb  
	.byte    $ff    ; bc  
	.byte    $0d    ; bd
	.byte    $0e    ; be  
	.byte    $ff    ; bf  

	.byte    $ff    ; c0  
	.byte    $ff    ; c1
	.byte    $ff    ; c2  
	.byte    $ff    ; c3  
	.byte    $ff    ; c4  
	.byte    $ff    ; c5  
	.byte    $ff    ; c6  
	.byte    $ff    ; c7

	.byte    $ff    ; c8  
	.byte    $08    ; c9  
	.byte    $00    ; ca  
	.byte    $01    ; cb  
	.byte    $ff    ; cc  
	.byte    $0c    ; cd  
	.byte    $04    ; ce  
	.byte    $05    ; cf

	.byte    $ff    ; d0  
	.byte    $ff    ; d1
	.byte    $02    ; d2  
	.byte    $03    ; d3  
	.byte    $ff    ; d4  
	.byte    $0f    ; d5  
	.byte    $06    ; d6  
	.byte    $07    ; d7  

	.byte    $ff    ; d8  
	.byte    $09    ; d9  
	.byte    $0a    ; da  
	.byte    $0b    ; db  
	.byte    $ff    ; dc  
	.byte    $0d    ; dd  
	.byte    $0e    ; de  
	.byte    $ff    ; df  

	.byte    $ff    ; e0  
	.byte    $ff    ; e1
	.byte    $ff    ; e2  
	.byte    $ff    ; e3  
	.byte    $ff    ; e4
	.byte    $ff    ; e5
	.byte    $ff    ; e6
	.byte    $ff    ; e7

	.byte    $ff    ; e8  
	.byte    $08    ; e9  
	.byte    $00    ; ea  
	.byte    $01    ; eb  
	.byte    $ff    ; ec  
	.byte    $0c    ; ed  
	.byte    $04    ; ee
	.byte    $05    ; ef

	.byte    $ff    ; f0  
	.byte    $ff    ; f1
	.byte    $02    ; f2  
	.byte    $03    ; f3  
	.byte    $ff    ; f4  
	.byte    $0f    ; f5  
	.byte    $06    ; f6  
	.byte    $07    ; f7  

	.byte    $ff    ; f8  
	.byte    $09    ; f9  
	.byte    $0a    ; fa  
	.byte    $0b    ; fb  
	.byte    $ff    ; fc  
	.byte    $0d    ; fd  
 	.byte    $0e    ; fe  
	.byte    $ff    ; ff  
        .page  
        .subttl 'gcr.tables'      

*=*+256-<*		; even page boundary

gcrtbh  .byte    $ff    ; -d
        .byte    $ff    ; -c
        .byte    $ff    ; -b
        .byte    $ff    ; -a
        .byte    $ff    ; -9
        .byte    $ff    ; -8
        .byte    $ff    ; -7
        .byte    $ff    ; -6
        .byte    $ff    ; -5
        .byte    $08    ; -4  h
	.byte    $00    ; -3  h
	.byte    $01    ; -2  h
	.byte    $ff    ; -1
gcrtba  .byte    $0c    ; 0  h
gcrtbf  .byte    $04    ; 1  h
gcrtbd  .byte    $05    ; 2  h
	.byte    $ff    ; 4
	.byte    $ff	; 3  
	.byte    $02    ; 5  h
	.byte    $03    ; 6  h
	.byte    $ff    ; 7
	.byte    $0f    ; 8  h
	.byte    $06    ; 9  h
	.byte    $07    ; a  h
	.byte    $ff    ; b
	.byte    $09    ; c  h
	.byte    $0a    ; d  h
	.byte    $0b    ; e  h
	.byte    $ff    ; f
gcrtbe  .byte    $0d    ; 10  h
	.byte    $0e    ; 11  h
	.byte    $80    ; 12  c
	.byte    $ff    ; 13
	.byte    $00    ; 14  c
	.byte    $00    ; 15  e
	.byte    $10    ; 16  c
	.byte    $40    ; 17  e
	.byte    $ff    ; 18
	.byte    $20    ; 19  e
	.byte    $c0    ; 1a  c
	.byte    $60    ; 1b  e
	.byte    $40    ; 1c  c
gcrtbg  .byte    $a0    ; 1d  e
	.byte    $50    ; 1e  c
	.byte    $e0    ; 1f  e
	.byte    $ff    ; 20
	.byte    $ff    ; 21
	.byte    $ff    ; 22
	.byte    $02    ; 23  d
	.byte    $20    ; 24  c
	.byte    $08    ; 25  f
	.byte    $30    ; 26  c
	.byte    $ff    ; 27
	.byte    $ff    ; 28
        .byte    $00    ; 29  f
	.byte    $f0    ; 2a  c
	.byte    $ff    ; 2b
	.byte    $60    ; 2c  c
	.byte    $01    ; 2d  f
	.byte    $70    ; 2e  c
	.byte    $ff    ; 2f
	.byte    $ff    ; 30
	.byte    $ff    ; 31
	.byte    $90    ; 32  c
	.byte    $03    ; 33  d
	.byte    $a0    ; 34  c
	.byte    $0c    ; 35  f
	.byte    $b0    ; 36  c
	.byte    $ff    ; 37
	.byte    $ff    ; 38
	.byte    $04    ; 39  f
	.byte    $d0    ; 3a  c
	.byte    $ff    ; 3b
	.byte    $e0    ; 3c  c
	.byte    $05    ; 3d  f
	.byte    $80    ; 3e  g
	.byte    $ff    ; 3f
	.byte    $90    ; 40  g
	.byte    $ff    ; 41
	.byte    $08    ; 42  b
	.byte    $0c    ; 43  b
	.byte    $ff    ; 44
	.byte    $0f    ; 45  b
	.byte    $09    ; 46  b
	.byte    $0d    ; 47  b
	.byte    $80    ; 48  a
	.byte    $02    ; 49  f
	.byte    $ff    ; 4a
	.byte    $ff    ; 4b
	.byte    $ff    ; 4c
	.byte    $03    ; 4d  f
	.byte    $ff    ; 4e
	.byte    $ff    ; 4f
	.byte    $00    ; 50  a
	.byte    $ff    ; 51
	.byte    $ff    ; 52
	.byte    $0f    ; 53  d
	.byte    $ff    ; 54
	.byte    $0f    ; 55  f
	.byte    $ff    ; 56
	.byte    $ff    ; 57
	.byte    $10    ; 58  a
	.byte    $06    ; 59  f
	.byte    $ff    ; 5a
	.byte    $ff    ; 5b
	.byte    $ff    ; 5c
	.byte    $07    ; 5d  f
	.byte    $00    ; 5e  g
	.byte    $20    ; 5f  g
	.byte    $a0    ; 60  g
	.byte    $ff    ; 61
	.byte    $ff    ; 62
	.byte    $06    ; 63  d
	.byte    $ff    ; 64
	.byte    $09    ; 65  f
	.byte    $ff    ; 66
	.byte    $ff    ; 67
	.byte    $c0    ; 68  a
	.byte    $0a    ; 69  f
	.byte    $ff    ; 6a
	.byte    $ff    ; 6b
	.byte    $ff    ; 6c
	.byte    $0b    ; 6d  f
	.byte    $ff    ; 6e
	.byte    $ff    ; 6f
	.byte    $40    ; 70  a
	.byte    $ff    ; 71
	.byte    $ff    ; 72
	.byte    $07    ; 73  d
	.byte    $ff    ; 74
	.byte    $0d    ; 75  f
	.byte    $ff    ; 76
	.byte    $ff    ; 77
	.byte    $50    ; 78  a
	.byte    $0e    ; 79  f
	.byte    $ff	; 7a
	.byte    $ff	; 7b
	.byte    $ff	; 7c
	.byte    $ff	; 7d
	.byte    $10    ; 7e  g
	.byte    $30    ; 7f  g
	.byte    $b0    ; 80  g
	.byte    $ff    ; 81
	.byte    $00    ; 82  b
	.byte    $04    ; 83  b
	.byte    $02    ; 84  b
	.byte    $06    ; 85  b
	.byte    $0a    ; 86  b
	.byte    $0e    ; 87  b
	.byte    $80    ; 88  a
	.byte    $ff    ; 89
	.byte    $ff    ; 8a
	.byte    $ff    ; 8b
	.byte    $ff    ; 8c
	.byte    $ff    ; 8d
	.byte    $ff    ; 8e
	.byte    $ff    ; 8f
	.byte    $20    ; 90  a
	.byte    $ff    ; 91
	.byte    $08    ; 92  d
	.byte    $09    ; 93  d
	.byte    $80    ; 94  e
	.byte    $10    ; 95  e
	.byte    $c0    ; 96  e
	.byte    $50    ; 97  e
	.byte    $30    ; 98  a
	.byte    $30    ; 99  e
	.byte    $f0    ; 9a  e
	.byte    $70    ; 9b  e
	.byte    $90    ; 9c  e
	.byte    $b0    ; 9d  e
	.byte    $d0    ; 9e  e
	.byte    $ff    ; 9f
	.byte    $ff    ; a0
	.byte    $ff    ; a1
	.byte    $00    ; a2  d
	.byte    $0a    ; a3  d
	.byte    $ff    ; a4
	.byte    $ff    ; a5
	.byte    $ff    ; a6
	.byte    $ff    ; a7
	.byte    $f0    ; a8  a

stbctl  brk     	; a9  *** start code stbctl
        nop     	; aa
stbknt  lda  $00,x      ; ab,ac
        bmi  stbknt     ; ad,ae
        rts     	; af  *** end code stbctl

	.byte    $60    ; b0  a
	.byte    $ff    ; b1
	.byte    $01    ; b2  d
	.byte    $0b    ; b3  d
	.byte    $ff    ; b4
	.byte    $ff    ; b5
	.byte    $ff    ; b6
	.byte    $ff    ; b7
	.byte    $70    ; b8  a
	.byte    $ff    ; b9
	.byte    $ff    ; ba
	.byte    $ff    ; bb
	.byte    $ff    ; bc
	.byte    $ff    ; bd
	.byte    $c0    ; be  g
	.byte    $f0    ; bf  g
	.byte    $d0    ; c0  g
	.byte    $ff    ; c1
	.byte    $01    ; c2  b
	.byte    $05    ; c3  b
	.byte    $03    ; c4  b
	.byte    $07    ; c5  b
	.byte    $0b    ; c6  b
	.byte    $ff    ; c7
	.byte    $90    ; c8  a
	.byte    $ff    ; c9
	.byte    $ff    ; ca
	.byte    $ff    ; cb
	.byte    $ff    ; cc
	.byte    $ff    ; cd
	.byte    $ff    ; ce
	.byte    $ff    ; cf
	.byte    $a0    ; d0  a
	.byte    $ff    ; d1
	.byte    $0c    ; d2  d
	.byte    $0d    ; d3  d
	.byte    $ff    ; d4
	.byte    $ff    ; d5
	.byte    $ff    ; d6
	.byte    $ff    ; d7
	.byte    $b0    ; d8  a
	.byte    $ff    ; d9
	.byte    $ff    ; da
	.byte    $ff    ; db
	.byte    $ff    ; dc
	.byte    $ff    ; dd
	.byte    $40    ; de  g
	.byte    $60    ; df  g
	.byte    $e0    ; e0  g
	.byte    $ff    ; e1
	.byte    $04    ; e2  d
	.byte    $0e    ; e3  d
	.byte    $ff    ; e4
	.byte    $ff    ; e5
	.byte    $ff    ; e6
	.byte    $ff    ; e7
	.byte    $d0    ; e8  a
	.byte    $ff    ; e9
	.byte    $ff    ; ea
	.byte    $ff    ; eb
	.byte    $ff    ; ec
	.byte    $ff    ; ed
	.byte    $ff    ; ee
	.byte    $ff    ; ef
	.byte    $e0    ; f0  a
	.byte    $ff    ; f1
	.byte    $05    ; f2  d
	.byte    $ff    ; f3
	.byte    $ff    ; f4
	.byte    $ff    ; f5
	.byte    $ff    ; f6
	.byte    $ff    ; f7
	.byte    $ff    ; f8
	.byte    $ff    ; f9
	.byte    $ff    ; fa
	.byte    $ff    ; fb
	.byte    $ff    ; fc
	.byte    $ff    ; fd
 	.byte    $50    ; fe  g
	.byte    $70    ; ff  g
        .page  
;     11/14/84 ds

gcrtb2  .byte    $ff    ; 0 
	.byte    $ff    ; 1  
	.byte    $ff    ; 2  
	.byte    $ff    ; 3
	.byte    $ff    ; 4
	.byte    $ff    ; 5  
	.byte    $ff    ; 6  
	.byte    $ff    ; 7
	.byte    $ff    ; 8  
	.byte    $ff    ; 9  
	.byte    $ff    ; a  
	.byte    $ff    ; b
	.byte    $ff    ; c  
	.byte    $ff    ; d  
	.byte    $ff    ; e  
	.byte    $ff    ; f
	.byte    $ff    ; 10  
	.byte    $ff    ; 11  
	.byte    $80    ; 12  
	.byte    $80    ; 13
	.byte    $00    ; 14  
	.byte    $00    ; 15  
	.byte    $10    ; 16  
	.byte    $10    ; 17  
	.byte    $ff    ; 18
	.byte    $ff    ; 19  
	.byte    $c0    ; 1a  
	.byte    $c0    ; 1b  
	.byte    $40    ; 1c  
	.byte    $40    ; 1d  
	.byte    $50    ; 1e  
	.byte    $50    ; 1f  
	.byte    $ff    ; 20 
	.byte    $ff    ; 21 
	.byte    $ff    ; 22
	.byte    $ff    ; 23  
	.byte    $20    ; 24  
	.byte    $20    ; 25  
	.byte    $30    ; 26  
	.byte    $30    ; 27  
	.byte    $ff    ; 28  
        .byte    $ff    ; 29  
	.byte    $f0    ; 2a  
	.byte    $f0    ; 2b
	.byte    $60    ; 2c  
	.byte    $60    ; 2d  
	.byte    $70    ; 2e  
	.byte    $70    ; 2f
	.byte    $ff    ; 30
	.byte    $ff    ; 31
	.byte    $90    ; 32  
	.byte    $90    ; 33  
	.byte    $a0    ; 34  
	.byte    $a0    ; 35  
	.byte    $b0    ; 36  
	.byte    $b0    ; 37
	.byte    $ff    ; 38
	.byte    $ff    ; 39  
	.byte    $d0    ; 3a  
	.byte    $d0    ; 3b
	.byte    $e0    ; 3c  
	.byte    $e0    ; 3d  
	.byte    $ff    ; 3e  
	.byte    $ff    ; 3f
	.byte    $ff    ; 40  
	.byte    $ff    ; 41
	.byte    $ff    ; 42  
	.byte    $ff    ; 43  
	.byte    $ff    ; 44
	.byte    $ff    ; 45  
	.byte    $ff    ; 46  
	.byte    $ff    ; 47  
	.byte    $ff    ; 48
	.byte    $ff    ; 49  
	.byte    $ff    ; 4a
	.byte    $ff    ; 4b
	.byte    $ff    ; 4c
	.byte    $ff    ; 4d  
	.byte    $ff    ; 4e
	.byte    $ff    ; 4f

	.byte    $ff    ; 50  
	.byte    $ff    ; 51
	.byte    $80    ; 52
	.byte    $80    ; 53  
	.byte    $00    ; 54
	.byte    $00    ; 55  
	.byte    $10    ; 56
	.byte    $10    ; 57

	.byte    $ff    ; 58  
	.byte    $ff    ; 59  
	.byte    $c0    ; 5a
	.byte    $c0    ; 5b
	.byte    $40    ; 5c
	.byte    $40    ; 5d  
	.byte    $50    ; 5e  
	.byte    $50    ; 5f  

	.byte    $ff    ; 60  
	.byte    $ff    ; 61
	.byte    $ff    ; 62
	.byte    $ff    ; 63  
	.byte    $20    ; 64
	.byte    $20    ; 65  
	.byte    $30    ; 66
	.byte    $30    ; 67

	.byte    $ff    ; 68  
	.byte    $ff    ; 69  
	.byte    $f0    ; 6a
	.byte    $f0    ; 6b
	.byte    $60    ; 6c
	.byte    $60    ; 6d  
	.byte    $70    ; 6e
	.byte    $70    ; 6f

	.byte    $ff    ; 70  
	.byte    $ff    ; 71
	.byte    $90    ; 72
	.byte    $90    ; 73  
	.byte    $a0    ; 74
	.byte    $a0    ; 75  
	.byte    $b0    ; 76
	.byte    $b0    ; 77

	.byte    $ff    ; 78  
	.byte    $ff    ; 79  
	.byte    $d0    ; 7a
	.byte    $d0    ; 7b
	.byte    $e0    ; 7c
	.byte    $e0    ; 7d
	.byte    $ff    ; 7e  
	.byte    $ff    ; 7f  

	.byte    $ff    ; 80  
	.byte    $ff    ; 81
	.byte    $ff    ; 82  
	.byte    $ff    ; 83  
	.byte    $ff    ; 84  
	.byte    $ff    ; 85  
	.byte    $ff    ; 86  
	.byte    $ff    ; 87  

	.byte    $ff    ; 88  
	.byte    $ff    ; 89  
	.byte    $ff    ; 8a  
	.byte    $ff    ; 8b  
	.byte    $ff    ; 8c  
	.byte    $ff    ; 8d  
	.byte    $ff    ; 8e  
	.byte    $ff    ; 8f

	.byte    $ff    ; 90  
	.byte    $ff    ; 91
	.byte    $80    ; 92  
	.byte    $80    ; 93  
	.byte    $00    ; 94  
	.byte    $00    ; 95  
	.byte    $10    ; 96  
	.byte    $10    ; 97  

	.byte    $ff    ; 98  
	.byte    $ff    ; 99  
	.byte    $c0    ; 9a  
	.byte    $c0    ; 9b  
	.byte    $40    ; 9c  
	.byte    $40    ; 9d  
	.byte    $50    ; 9e  
	.byte    $50    ; 9f

	.byte    $ff    ; a0
	.byte    $ff    ; a1
	.byte    $ff    ; a2  
	.byte    $ff    ; a3  
	.byte    $20    ; a4  
	.byte    $20    ; a5  
	.byte    $30    ; a6  
	.byte    $30    ; a7  

	.byte    $ff    ; a8  
	.byte    $ff    ; a9  
	.byte    $f0    ; aa  
	.byte    $f0    ; ab  
	.byte    $60    ; ac  
	.byte    $60    ; ad  
	.byte    $70    ; ae  
	.byte    $70    ; af  

	.byte    $ff    ; b0  
	.byte    $ff    ; b1
	.byte    $90    ; b2  
	.byte    $90    ; b3  
	.byte    $a0    ; b4  
	.byte    $a0    ; b5  
	.byte    $b0    ; b6  
	.byte    $b0    ; b7  

	.byte    $ff    ; b8  
	.byte    $ff    ; b9  
	.byte    $d0    ; ba  
	.byte    $d0    ; bb  
	.byte    $e0    ; bc  
	.byte    $e0    ; bd
	.byte    $ff    ; be  
	.byte    $ff    ; bf  

	.byte    $ff    ; c0  
	.byte    $ff    ; c1
	.byte    $ff    ; c2  
	.byte    $ff    ; c3  
	.byte    $ff    ; c4  
	.byte    $ff    ; c5  
	.byte    $ff    ; c6  
	.byte    $ff    ; c7

	.byte    $ff    ; c8  
	.byte    $ff    ; c9  
	.byte    $ff    ; ca  
	.byte    $ff    ; cb  
	.byte    $ff    ; cc  
	.byte    $ff    ; cd  
	.byte    $ff    ; ce  
	.byte    $ff    ; cf

	.byte    $ff    ; d0  
	.byte    $ff    ; d1
	.byte    $80    ; d2  
	.byte    $80    ; d3  
	.byte    $00    ; d4  
	.byte    $00    ; d5  
	.byte    $10    ; d6  
	.byte    $10    ; d7  

	.byte    $ff    ; d8  
	.byte    $ff    ; d9  
	.byte    $c0    ; da  
	.byte    $c0    ; db  
	.byte    $40    ; dc  
	.byte    $40    ; dd  
	.byte    $50    ; de  
	.byte    $50    ; df  

	.byte    $ff    ; e0  
	.byte    $ff    ; e1
	.byte    $ff    ; e2  
	.byte    $ff    ; e3  
	.byte    $20    ; e4
	.byte    $20    ; e5
	.byte    $30    ; e6
	.byte    $30    ; e7

	.byte    $ff    ; e8  
	.byte    $ff    ; e9  
	.byte    $f0    ; ea  
	.byte    $f0    ; eb  
	.byte    $60    ; ec  
	.byte    $60    ; ed  
	.byte    $70    ; ee
	.byte    $70    ; ef

	.byte    $ff    ; f0  
	.byte    $ff    ; f1
	.byte    $90    ; f2  
	.byte    $90    ; f3  
	.byte    $a0    ; f4  
	.byte    $a0    ; f5  
	.byte    $b0    ; f6  
	.byte    $b0    ; f7  

	.byte    $ff    ; f8  
	.byte    $ff    ; f9  
	.byte    $d0    ; fa  
	.byte    $d0    ; fb  
	.byte    $e0    ; fc  
	.byte    $e0    ; fd  
 	.byte    $ff    ; fe  
	.byte    $ff    ; ff  
        .page 
        .subttl 'getact.src'          
;*********************************
;* getact: get active buffer #   *
;*   vars: buf0,buf1,lindx       *
;*   regs: out: .a= act buffer # *
;*              .x= lindx        *
;*   flags:     .n=1: no act-buf *
;*********************************

getact           
        ldx  lindx       
        lda  buf0,x      
        bpl  ga1         
        lda  buf1,x      
ga1              
        and  #$bf       ;  strip dirty bit
        rts      

;*********************************
;* gaflg: get active buffer #;   *
;*        set lbused & flags.    *
;*   regs: out: .a= act buffer # *
;*              .x= lindx        *
;*   flags:     .n=1: no act-buf *
;*              .v=1: dirty buf  *
;*********************************

gaflgs           
        ldx  lindx       
ga2     stx  lbused     ; save buf #
        lda  buf0,x      
        bpl  ga3         

        txa      
        clc      
        adc  #mxchns+1   
        sta  lbused      
        lda  buf1,x      
ga3              
        sta  t1          
        and  #$1f        
        bit  t1          
        rts      

;******************************

; get channels inactive
; buffer number.

;    input parameters:
;        lindx - channel #

;    output parameters:
;        a <== inactive buffer #
;           or
;        a <== $ff if no
;            inactive buffer.

;******************************

getina  ldx  lindx       
        lda  buf0,x      
        bmi  gi10        
        lda  buf1,x      
gi10    cmp  #$ff        
        rts      

;*****************************
;**********  p u t i n a  ****
;*****************************

; put inactive buffer

;    input paramters:
;        a = buffer #

;    output paramters:
;        none

;*****************************

putina  ldx  lindx       
        ora  #$80        
        ldy  buf0,x      
        bpl  pi1         
        sta  buf0,x      
        rts      
pi1     sta  buf1,x      
        rts      
        .page 
        .subttl 'idlesf.src'       

; idle loop, waiting for something to do
idle             
        cli      

;  release all bus lines

        lda  pb         ; clock and data high
        and  #$e5       ; clock high
        sta  pb         ; data high,atna hi

        lda  cmdwat     ; test for pending command
        beq  idl1       ; no command waiting

        lda  #0          
        sta  cmdwat      

	nop		; *** rom ds -06 *** / fill
	nop		; *** rom ds -06 *** / fill
;       sta  nmiflg     ; clear debounce

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch19	; *rom ds 01/23/85*
;	jsr  parsxq     ; parse and xeq command

rtch19			; return

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

idl1    cli     	; test for drive running or openfile
        lda  atnpnd      
        beq  idl01       

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch30	; *rom ds 02/04/85*
;       jmp  atnsrv     ; service atn irq

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

idl01            
        cli      
        lda  #14         
        sta  temp+3      
        lda  #0         ; if file open, turn on act led
        sta  temp        
        sta  temp+1      
idl2    ldx  temp+3     ; look thru lintab
        lda  lintab,x   ; for active file
        cmp  #$ff        
        beq  idl3        

        and  #$3f        
        sta  lindx       
	jsr  getact      
        tax      
        lda  lstjob,x   ; determine which drv it is on
        and  #1          
        tax      
        inc  temp,x      
idl3    dec  temp+3     ; set flag indicating drv
        bpl  idl2       ; has file open
        ldy  #bfcnt-1   ; look thru job que for
idl4    lda  jobs,y     ; for jobs still running
        bpl  idl5        
        and  #1          
        tax      
        inc  temp,x     ; set flag indicating drive
idl5    dey     	; is active
        bpl  idl4        
        sei      

; dont allow irq when reading ledprt 

        lda  ledprt      
        and  #$ff-led0   
        pha      
        lda  drvnum      
        sta  r0          
        lda  #0          
        sta  drvnum      
        lda  temp        
        beq  idl7        

        lda  wpsw        
        beq  idl6        

	jsr  cldchn      

idl6             
        pla     	; turn on led if drive flag
        ora  #led0      ;  if not 0
        pha      
idl7             
        inc  drvnum      
        lda  temp+1      
        beq  idl9        

        lda  wpsw+1      
        beq  idl8        

        jsr  cldchn      
idl8             
        pla      
        ora  #led1       
        pha      
idl9             
        lda  r0          
        sta  drvnum      
        pla      
        ldx  erword      
        beq  idl12      ; no error flashing

        lda  ledprt     ; use current leds
        cpx  #$80        
        bne  idl10      ; not ist time

        jmp  idl11       

idl10            
        ldx  timer1      
        bmi  idl12      ; still timing

        ldx  #$a0       ; count 8 msec
        stx  timer1      
idl11            
        dec  erword     ; count units of 8 msec
        bne  idl12      ; keep counting

        eor  erled      ; toggle led
        ldx  #16        ; count 16 units
        stx  erword      
idl12            
        sta  ledprt     ; set leds
        jmp  idl1       ; back to top of lop
	.page  
	.subttl 'irq1541.src'   
irq	pha
	txa
	pha
	tya
	pha

	lda  icr
	and  #8
	beq  1$		; fast serial request

	bit  lock	; locked ?
	bmi  1$

	lda  pota1	; change to 2 Mhz
	ora  #$20
	sta  pota1
	lda  #<jirq
	sta  irqjmp
	lda  #>jirq
	sta  irqjmp+1	; re-vector irq

	lda  #tim2	; 8 ms irq's at 2 Mhz - controller irq's
	sta  t1hl2
	sta  t1hc2	; adjust timers for 2 Mhz 
	lda  #0
	sta  nxtst	; not a vector
	jmp  irq_0

1$	lda  ifr1
        and  #2          
        beq  2$		;  not atn

        jsr  atnirq     ;  handle atn request

2$	lda  ifr2       ;  test if timer
        asl  a   
        bpl  3$		;  not timer

        jsr  lcc        ;  goto controller

3$ 	tsx
	lda  $0104,x	; check processor break flag
	and  #$10
	beq  4$

	jsr  lcc

4$	pla     	; restore .y, .x, .a
        tay      
        pla      
        tax      
        pla      
        rti      
        .page  
	.subttl 'irq1571.src'

jirq	pha     	; save .a,.x,.y
        txa      
        pha      
        tya      
        pha      

        lda  icr	; chk for fast byte
	and  #8
	beq  irq_1

irq_0	lda  fastsr      
        ora  #$40       ; set byte in flag
        sta  fastsr      
	bne  irq_2	; bra

irq_1	lda  ifr1	; test for atn
	and  #2
	beq  1$

	bit  pa1	; clear (ca1)
	lda  #1         ; handle atn request
        sta  atnpnd     ; flag atn is pending

1$	tsx     	; check break flag
        lda  $0104,x    ; check processor status
        and  #$10        
        beq  2$

        jsr  jlcc       ; forced irq do controller job

2$	lda  ifr2	; test if timer
	asl  a
	bpl  3$

        jsr  jlcc       ; goto controller

3$
irq_2	pla     	; restore .y,.x,.a
        tay      
        pla      
        tax      
        pla      
        rti      
	.page  
	.subttl 'irq.src'   
;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

sysirq  jmp  (irqjmp)	;  irq vector ***rom ds 02/01/85***

;	pha		;  save .a
;	txa		;  save .x
;	pha

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        tya      
        pha      	;  save .y

        lda  ifr1       ;  test if atn
        and  #2          
        beq  1$		;  not atn

        jsr  atnirq     ;  handle atn request

1$	lda  ifr2       ;  test if timer
        asl  a   
        bpl  2$		;  not timer

        jsr  lcc        ;  goto controller

2$	pla     	;  restore .y,.x,.a
        tay      
        pla      
        tax      
        pla      
        rti      
        .page  
        .subttl 'jobssf.src'       

; use lastjob for drive #
; cmd is used for job command

setljb           
        lda  lstjob,x    
        and  #1          
        ora  cmd         

; set job up and check t&s
;  .a=command for jobs
;  .x=job number

setjob           
        pha      
        stx  jobnum      
        txa      
        asl  a   
        tax      
        lda  hdrs+1,x   ; 4/12***********
        sta  cmd        ; save sector
        lda  hdrs,x     ; 4/12***********
        beq  tserr       

        cmp  maxtrk      
        bcs  tserr      ; track too large

        tax      
        pla     	; check for write
        pha      
        and  #$f0        
        cmp  #write      
        bne  sjb1       ; not write,skip check
        pla      
        pha      
        lsr  a   
        bcs  sjb2       ; drive 1

        lda  dskver     ; get version #
        bcc  sjb3        
sjb2             
        lda  dskver+1   ; get drive 1 ver#
sjb3             
        beq  sjb4       ; no # is ok, too
        cmp  vernum      
        bne  vnerr      ; not same vernum #

sjb4             
        txa     	; restore track #
        jsr  maxsec      
        cmp  cmd         
        beq  tserr       
        bcs  sjb1       ; sector is ok!


; illegal track and sector

tserr            
        jsr  hed2ts      
tser1            
        lda  #badts      
        jmp  cmder2      

hed2ts           
        lda  jobnum      
        asl  a   
        tax      
        lda  hdrs,x     ; 4/12***********
        sta  track       
        lda  hdrs+1,x   ; 4/12***********
        sta  sector      
        rts      


tschk            
        lda  track       
        beq  tser1       
        cmp  maxtrk      
        bcs  tser1       

        jsr  maxsec      
        cmp  sector      
        beq  tser1       
        bcc  tser1       
        rts      

vnerr            
        jsr  hed2ts      
        lda  #cbmv2     ; write to wrong version
        jmp  cmder2      

sjb1             
        ldx  jobnum      
        pla      
        sta  cmd         
        sta  jobs,x      
        sta  lstjob,x    
        rts      


; do job in .a, set up error count
; and lstjob. return when job done ok
; jmp to error if error returns

doread           
        lda  #read       
        bne  dojob      ; bra
dowrit           
        lda  #write      
dojob            
        ora  drvnum      
        ldx  jobnum      

doit    sta  cmd         
doit2   lda  cmd         
        jsr  setjob      

; wait until job(.x) is done
; return when done

watjob  jsr  tstjob      
        bcs  watjob      
        pha     	; clr jobrtn flag
        lda  #0          
        sta  jobrtn      
        pla      
        rts      


;test if job(.x) is done yet
;if not done return
;if ok then return else redo it

tstjob  lda  jobs,x      
        bmi  notyet      
        cmp  #2          
        bcc  ok          

        cmp  #8         ; check for wp switch on
        beq  tj10        

        cmp  #11        ; check for id mismatch
        beq  tj10        

        cmp  #$f        ; check for nodrive
        bne  recov       
tj10    bit  jobrtn      
        bmi  ok          
        jmp  quit2       

ok      clc     	; c=0 finished ok or quit
        rts      

notyet  sec     	; c=1 not yet
        rts      


recov            
        tya     	; save .y
        pha      
        lda  drvnum     ; save drive #
        pha      
        lda  lstjob,x    
        and  #1          
        sta  drvnum     ; set active drive #

        tay      
        lda  ledmsk,y    
        sta  erled       

        jsr  dorec       
        cmp  #2          
        bcs  rec01       
        jmp  rec95       
rec01            

        lda  lstjob,x   ; original job
        and  #$f0       ; mask job code
        pha     	; save it
        cmp  #write      
        bne  rec0       ; not a write

        lda  drvnum      
        ora  #secsek    ; replace w/ sector seek...
        sta  lstjob,x   ; ... during recovery
rec0             
        bit  revcnt      
        bvs  rec5       ; no track offset

        lda  #0          
        sta  eptr       ; clr offset table ptr
        sta  toff       ; clr total offset
rec1             
        ldy  eptr        
        lda  toff        
        sec      
        sbc  offset,y    
        sta  toff       ; keep track of all offsets

        lda  offset,y    

        jsr  ptch12	; set micro-stepping flag *rom-05ds 01/21/85*
;       jsr  hedoff      

        inc  eptr       ; bump table ptr
        jsr  dorec      ; do the recovery
        cmp  #2         ; error code < 2?
        bcc  rec3       ; job worked

        ldy  eptr        
        lda  offset,y    
        bne  rec1       ; null indicates end
rec3             
        lda  toff        

        jsr  ptch13	; clr micro-stepping flag *rom-05ds 01/21/85*
;       jsr  hedoff      

        lda  jobs,x      
        cmp  #2          
        bcc  rec9       ; no error
rec5             
        bit  revcnt     ; check bump-on flag
        bpl  rec7       ; no bump

quit             
        pla      
        cmp  #write     ; check original job
        bne  quit2       

        ora  drvnum      
        sta  lstjob,x   ; must restore original
quit2            
        lda  jobs,x     ; .a= error #
        jsr  error       
rec7             
        pla      
        bit  jobrtn      
        bmi  rec95      ; return job error
        pha      

;do the bump

        lda  #bump       
        ora  drvnum      
        sta  jobs,x      
rec8             
;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  stbctl	; strobe the controller 
			; ***rom ds 01/22/85***
	nop
;       lda  jobs,x      
;       bmi  rec8       ; wait

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        jsr  dorec      ; try one last set
        cmp  #2          
        bcs  quit       ; it clearly ain't gonna work
rec9             
        pla     	; check original job for write
        cmp  #write      
        bne  rec95      ; original job worked

        ora  drvnum      
        sta  lstjob,x   ; set write job back
        jsr  dorec      ; try last set of writes
        cmp  #2         ; check error code
        bcs  quit2      ; error
rec95            
        pla      
        sta  drvnum     ; restore drive #
        pla      
        tay     	; restore .y
        lda  jobs,x      
        clc     	; ok!
        rts      

hedoff          	; .a=offset
        cmp  #0          
        beq  hof3       ; no offset
        bmi  hof2       ; steps are inward
hof1             
        ldy  #1         ; step out 1 track
        jsr  movhed      
        sec      
        sbc  #1          
        bne  hof1       ; not finished
        beq  hof3        
hof2             
        ldy  #$ff       ; step in 1 track
        jsr  movhed      
        clc      
        adc  #1          
        bne  hof2       ;  not finished
hof3             
        rts      

movhed           
        pha     	; save .a
        tya     	; put phase in .a
        ldy  drvnum      
        sta  phase,y     
mh10             
        cmp  phase,y     
        beq  mh10       ; wait for controller to change it

        lda  #0          
        sta  phase,y    ; clear it out
        pla     	; restore
        rts      


dorec           	; do last job recovery
        lda  revcnt     ; re-try job revcnt...
        and  #$3f       ; ...# of times
        tay      
dorec1           
        lda  erled       
        eor  ledprt      
        sta  ledprt      
        lda  lstjob,x   ; set last job
        sta  jobs,x      
dorec2           
	jsr  stbctl	; strobe the controller *rom-05ds 01/22/85*
	nop
;       lda  jobs,x     ; wait
;       bmi  dorec2      

        cmp  #2          
        bcc  dorec3     ; it worked

        dey      
        bne  dorec1     ; keep trying
dorec3           
        pha      
        lda  erled      ; leave drive led on
        ora  ledprt      
        sta  ledprt      
        pla      
        rts     	; finished

; set header of active buffer of the 
; current lindx to track,sector,id

sethdr  jsr  getact      
seth             
        asl  a   
        tay      
        lda  track       
        sta  hdrs,y     ; set track
        lda  sector      
        sta  hdrs+1,y   ; set sector
        lda  drvnum     ; get proper id(drvnum)
        asl  a   
        tax      
        rts      
	.page  
	.subttl 'lccbingcr.src'
;
;   fast binary to gcr
;
;
put4bg  lda  #0         ;  clear table
        sta  gtab+1      
        sta  gtab+4      
;
        ldy  gcrpnt      
;
        lda  btab        
        and  #$f0        
        lsr  a   
        lsr  a   
        lsr  a   
        lsr  a   
        tax      
        lda  bgtab,x     
;
        asl  a   
        asl  a   
        asl  a   
        sta  gtab        
;
        lda  btab        
        and  #$0f        
        tax      
        lda  bgtab,x     
;
        ror  a   
        ror  gtab+1      
        ror  a   
        ror  gtab+1      
;
        and  #$07        
        ora  gtab        
        sta  (bufpnt),y          
;
        iny      
;
        lda  btab+1      
        and  #$f0        
        lsr  a   
        lsr  a   
        lsr  a   
        lsr  a   
        tax      
        lda  bgtab,x     
;
        asl  a   
        ora  gtab+1      
        sta  gtab+1      
;
;
        lda  btab+1      
        and  #$0f        
        tax      
        lda  bgtab,x     
;
        rol  a   
        rol  a   
        rol  a   
        rol  a   
        sta  gtab+2      
;
        rol  a   
        and  #1          
        ora  gtab+1      
        sta  (bufpnt),y          
;
        iny      
;
        lda  btab+2      
        and  #$f0        
        lsr  a   
        lsr  a   
        lsr  a   
        lsr  a   
        tax      
        lda  bgtab,x     
;
        clc      
        ror  a   
        ora  gtab+2      
        sta  (bufpnt),y          
        iny      
;
        ror  a   
        and  #$80        
        sta  gtab+3      
;
        lda  btab+2      
        and  #$0f        
        tax      
        lda  bgtab,x     
        asl  a   
        asl  a   
        and  #$7c        
        ora  gtab+3      
        sta  gtab+3      
;
        lda  btab+3      
        and  #$f0        
        lsr  a   
        lsr  a   
        lsr  a   
        lsr  a   
        tax      
        lda  bgtab,x     
;
        ror  a   
        ror  gtab+4      
        ror  a   
        ror  gtab+4      
        ror  a   
        ror  gtab+4      
;
        and  #$03        
        ora  gtab+3      
        sta  (bufpnt),y          
        iny      
        bne  bing35      
;
        lda  savpnt+1    
        sta  bufpnt+1    
;
;
bing35  lda  btab+3      
        and  #$0f        
        tax      
        lda  bgtab,x     
        ora  gtab+4      
        sta  (bufpnt),y          
        iny      
        sty  gcrpnt      
        rts      
;
;
;
bgtab   .byte    $0a      
	.byte    $0b     
	.byte    $12     
	.byte    $13     
	.byte    $0e     
	.byte    $0f     
	.byte    $16     
	.byte    $17     
	.byte    $09     
	.byte    $19     
	.byte    $1a     
	.byte    $1b     
	.byte    $0d     
	.byte    $1d     
	.byte    $1e     
	.byte    $15     
;
;
;
;******************************
;*
;*
;*       binary to gcr conversion
;*
;*
;*   does inplace conversion of
;*   buffer to gcr using overflow
;*   block
;*
;*
;*   creates write image
;*
;*     1 block id char
;*   256 data bytes
;*     1 check sum
;*     2 off bytes
;*   ---
;*   260 binary bytes
;*
;*  260 binary bytes >> 325 gcr
;*
;*  325 = 256 + 69 overflow
;*
;*
;********************************
;*
bingcr  lda  #0         ;  init pointers
        sta  bufpnt      
        sta  savpnt      
        sta  bytcnt      
;
        lda  #256-topwrt         
        sta  gcrpnt     ;  start saving gcr here
;
        sta  gcrflg     ;  buffer converted flag
;
        lda  bufpnt+1   ;  save buffer pointer
        sta  savpnt+1    
;
        lda  #>ovrbuf   ;  point at overflow
        sta  bufpnt+1    
;
        lda  dbid       ;  store data block id
        sta  btab       ;  and next 3 data bytes
;
        ldy  bytcnt      
;
        lda  (savpnt),y          
        sta  btab+1      
        iny      
;
        lda  (savpnt),y          
        sta  btab+2      
        iny      
;
        lda  (savpnt),y          
        sta  btab+3      
        iny      
;
bing07  sty  bytcnt     ;  next byte to get
;
        jsr  put4bg     ;  convert and store
;
        ldy  bytcnt      
;
        lda  (savpnt),y          
        sta  btab        
        iny      
        beq  bing20      
;
        lda  (savpnt),y          
        sta  btab+1      
        iny      
;
        lda  (savpnt),y          
        sta  btab+2      
        iny      
;
        lda  (savpnt),y          
        sta  btab+3      
        iny      
;
        bne  bing07     ;  jmp
;
;
bing20  lda  chksum     ;  store chksum
        sta  btab+1      
;
        lda  #0         ;  store 0 off byte
        sta  btab+2      
        sta  btab+3      
;
        jmp  put4bg     ;  convert and store and return
;
;
;
;.end
	.page   
	.subttl  'lcccntrl.src'     
;
;
;
;    *contrl
;
;    main controller loop
;
;    scans job que for jobs
;
;   finds job on current track
;   if it exists
;
lcc              
;
        tsx     	;  save current stack pointer
        stx  savsp       
;
        lda  t1lc2      ; reset irq flag
;
        lda  pcr2       ;  enable s.o. to 6502
        ora  #$0e       ;  hi output
        sta  pcr2        
;
;
;
top     ldy  #numjob-1  ;  pointer into job que
;
cont10           
        lda  jobs,y     ;  find a job (msb set)
        bpl  cont20     ;  not one here
;
        cmp  #jumpc     ;  test if its a jump command
        bne  cont30      
;
        tya     	;  put job num in .a
        jmp  ex2         
;
;
cont30           
        and  #1         ;  get drive #
        beq  cont35      
;
        sty  jobn        
        lda  #$0f       ; bad drive # error
        jmp  errr        
;
cont35  tax      
        sta  drive       
;
        cmp  cdrive     ;  test if current drive
        beq  cont40      
;
        jsr  turnon     ;  turn on drive
        lda  drive       
        sta  cdrive      
        jmp  end        ;  go clean up
;
;
cont40  lda  drvst      ;  test if motor up to speed
        bmi  cont50      
;
        asl  a          ;  test if stepping
        bpl  que        ;  not stepping
;
cont50  jmp  end         
;
cont20  dey      
        bpl  cont10      
;
        jmp  end         
;
;
que     lda  #$20       ;  status=running
        sta  drvst       
;
        ldy  #numjob-1   
        sty  jobn        
;
que10   jsr  setjb       
        bmi  que20       
;
que05   dec  jobn        
        bpl  que10       
;
;
        ldy  nxtjob      
        jsr  setjb1      
;
        lda  nxtrk       
        sta  steps       
        asl  steps      ;  steps*2
;
        lda  #$60       ;  set status=stepping
        sta  drvst       
;
;
        lda  (hdrpnt),y         ;  get dest track #
        sta  drvtrk      
fin     jmp  end         
;
;
que20   and  #1         ;  test if same drive
        cmp  drive       
        bne  que05       
;
        lda  drvtrk      
        beq  gotu       ;  uninit. track #
;
        sec     	;  calc distance to track
        sbc  (hdrpnt),y          
        beq  gotu       ;  on track
;
        eor  #$ff       ;  2's comp
        sta  nxtrk       
        inc  nxtrk       
;
        lda  jobn       ;  save job# and dist to track
        sta  nxtjob      
;
        jmp  que05       
;
;
;
;
gotu    ldx  #4         ;  set track and sectr
        lda  (hdrpnt),y          
        sta  tracc       
;
;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

gotu10  cmp  trackn-1,x ; *** rom ds 11/7/86 ***, set density for tracks > 35

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>
        dex      
        bcs  gotu10      
;
        lda  numsec,x    
        sta  sectr       
;
        txa     	;  set density
        asl  a   
        asl  a   
        asl  a   
        asl  a   
        asl  a   
        sta  work        
;
        lda  dskcnt      
        and  #$9f       ;  clear density bits
        ora  work        
        sta  dskcnt      
;
        ldx  drive      ;  drive num in .x
;
        lda  job        ;  yes, go do the job
        cmp  #bumpc     ;  test for bump
        beq  bmp         
;
;
exe     cmp  #execd     ;  test if execute
        beq  ex          
;
        jmp  seak       ;  do a sector seek
;
ex      lda  jobn       ;  jump to buffer
ex2     clc      
        adc  #>bufs      
        sta  bufpnt+1    
        lda  #0          
        sta  bufpnt      
ex3     jmp  (bufpnt)    
;
;
bmp              
        lda  #$60       ;  set status=stepping
        sta  drvst       
;
        lda  dskcnt      
        and  #$ff-$03   ;  set phase a
        sta  dskcnt      
;
;
        lda  #256-92    ;  step back 45 traks
        sta  steps       
;
        lda  #1         ;  drvtrk now 1
        sta  drvtrk      
;
        jmp  errr       ;  job done return 1
;
;
setjb   ldy  jobn        
setjb1  lda  jobs,y      
        pha      
        bpl  setj10     ;  no job here
;
        and  #$78        
        sta  job         
        tya      
        asl  a   
        adc  #<hdrs      
        sta  hdrpnt      
        tya     	;  point at buffer
        clc      
        adc  #>bufs      
        sta  bufpnt+1    
;
;
setj10  ldy  #0          
        sty  bufpnt      
;
        pla      
        rts      
;
;
;
;.end
	.page 
	.subttl 'lccconhdr.src'    
;
;
;
;    *conhdr
;
;    convert header
;    into gcr search image
;    and place in stab
;
;   image contains :
;
;   00 id id tr sc cs hbid
;
;
conhdr  lda  bufpnt+1   ; save buffer pointer
        sta  savpnt+1    
;
        lda  #>stab      
        sta  bufpnt+1    
;
        lda  #<stab      
        sta  gcrpnt      
;
        lda  hbid        
        sta  btab        
;
        lda  header+4    
        sta  btab+1      
;
        lda  header+3    
        sta  btab+2      
;
        lda  header+2    
        sta  btab+3      
;
        jsr  put4bg      
;
        lda  header+1    
        sta  btab        
;
        lda  header      
        sta  btab+1      
;
        lda  #0          
        sta  btab+2      
        sta  btab+3      
;
        jsr  put4bg      
;
        lda  savpnt+1   ; restore buffer pointer
        sta  bufpnt+1    
;
        rts      
;
;
;.end
	.page  
	.subttl 'lccend.src'
;
;
;
;   motor and stepper control
;
;
;   irq into controller every  8.224 ms
end              
        lda  t1hl2      ; set irq timer
        sta  t1hc2       
;
        lda  dskcnt      
        and  #$10       ;  test write proctect
        cmp  lwpt        
        sta  lwpt       ;  change ?

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch21	;  do auto on code *** rom ds 01/22/85 ***
	nop		;  fill
	nop		;  fill
	nop		;  fill
	
;       beq  end002     ;  no

;       lda  #1         ;  yes, set flag
;       sta  wpsw        
;

rtch21			; return

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

end002  lda  phase      ;  test for phase offset
        beq  end40       
;
        cmp  #2          
        bne  end003      
;
        lda  #0         ; phase <-- 0
        sta  phase       
        beq  end40       
;
end003  sta  steps       
        lda  #2         ; phase <-- 2
        sta  phase       
        jmp  dostep      
;
end40   ldx  cdrive     ;  work on active drive only
        bmi  end33x     ;  no active drive
;
        lda  drvst      ;  test if motor on
        tay      
        cmp  #$20       ;  test if anything to do
        bne  end10      ;  something here
;
end33x  jmp  end33      ;  motor just running
;
end10   dec  acltim     ;  dec timer
        bne  end30       
;
        tya     	;  test if acel
        bpl  end20       
;
;
        and  #$7f       ;  over, clear acel bit
        sta  drvst       
;
end20   and  #$10       ;  test if time out state
        beq  end30       

	dec  acltim2	;  timer2 *rom-05ds 01/22/85*
	bne  end30
	nop		;  fill
	jsr  motoff	;  off the motor, ok...
;
;       lda  dskcnt     ;        
;	and  #$ff-$04   ;  turnoff motor
;       sta  dskcnt      
;
;
        lda  #$ff       ;  no active drive now
        sta  cdrive      
;
        lda  #0         ;  drive inactive
        sta  drvst      ;  clear on bit and timout
        beq  end33x      
;
end30   tya     	;  test if step needed
        and  #$40        
        bne  end30x     ;  stepping
;
        jmp  end33       
;
;
end30x  jmp  (nxtst)    ; goto proper stepper state
;
inact   lda  steps      ;  get abs(steps)
        bpl  inac10      
;
        eor  #$ff        
        clc      
        adc  #1          
;
inac10  cmp  minstp     ;  test if we can accel
        bcs  inac20     ;  too small
;
        lda  #<short    ; short step mode
        sta  nxtst       
        lda  #>short     
        sta  nxtst+1     
        bne  dostep      
;
inac20          	;  calc the # of run steps
        sbc  as          
        sbc  as          
        sta  rsteps      
;
        lda  as          
        sta  aclstp     ;  set  # of accel steps
        lda  #<ssacl     
        sta  nxtst       
        lda  #>ssacl     
        sta  nxtst+1     
;
dostep  lda  steps       
        bpl  stpin       
;
;---------rom05-bc 09/12/84--------
;stpout inc steps
;	ldx dskcnt
;	dex
;	jmp stp

stpout  jmp  patch9     ; check track 0 (photo sensor)
        nop     	; 
        nop     	; 
        nop     	; 
pppppp  jmp  stp        ; goto step
;----------------------------------
;
short   lda  steps      ; step end ?
        bne  dostep     ; no
;
        lda  #<setle    ; settle
        sta  nxtst       
        lda  #>setle     
        sta  nxtst+1     
        lda  #5         ;  settle time (5*8=40ms)
        sta  aclstp      
        jmp  end33       
;
setle   dec  aclstp     ;  settle end ?
        bne  end33      ;  no
;
        lda  drvst       
        and  #$ff-$40    
        sta  drvst       
;
        lda  #<inact     
        sta  nxtst       
        lda  #>inact     
        sta  nxtst+1     
        jmp  end33       
;
stpin   dec  steps       
        ldx  dskcnt      
        inx      
;
stp     txa      
        and  #3          
        sta  tmp         
        lda  dskcnt      
        and  #$ff-$03   ;  mask out old
        ora  tmp         
        sta  dskcnt      
        jmp  end33       
;
ssacl           	;  sub acel factor
        sec      
        lda  t1hl2       
        sbc  af          
        sta  t1hc2       
;
        dec  aclstp      
        bne  ssa10       
;
        lda  as          
        sta  aclstp      
;
        lda  #<ssrun     
        sta  nxtst       
        lda  #>ssrun     
        sta  nxtst+1     
;
ssa10   jmp  dostep      
;
ssrun   dec  rsteps      
        bne  ssa10       
;
        lda  #<ssdec     
        sta  nxtst       
        lda  #>ssdec     
        sta  nxtst+1     
        bne  ssa10       
;
ssdec           	;  decel
        lda  t1hl2       
        clc      
        adc  af          
        sta  t1hc2       
;
        dec  aclstp      
        bne  ssa10       
;
        lda  #<setle    ;  goto settle mode
        sta  nxtst       
        lda  #>setle     
        sta  nxtst+1     
;
        lda  #5          
        sta  aclstp     ;  settle timer
;
;
end33   lda  pcr2       ;  disable s.o. to 6502
        and  #$ff-$02    
        sta  pcr2        
;
        rts      
;.end
	.page
	.subttl 'lccfmt1.src'      
;
fmtvar   =  $620        ;  put format vars in jump buffer
cnt      =  fmtvar       
num      =  fmtvar+1     
trys     =  fmtvar+3     
tral     =  fmtvar+4     
dtrck    =  fmtvar+6        
remdr    =  fmtvar+7        
sect     =  fmtvar+8     
;
;
;
;*  format routine for lcc
;*
;*
;*
;
;
;
code             
formt   lda  ftnum      ;  test if formatting begun
        bpl  l213       ;  yes
;
        ldx  drive      ;  no,start up by bumping
        lda  #$60       ;  status=stepping
        sta  drvst,x     
;
        lda  #1         ;  drive track =1
        sta  drvtrk,x    
        sta  ftnum      ;  start on track 1
;
        lda  #256-92    ;  bump back 45 steps
        sta  steps       
;
        lda  dskcnt     ; set phase a
        and  #$ff-$03    
        sta  dskcnt      
;
;
        lda  #10        ;  10 errors allowed
        sta  cnt         
;
        lda  #<4000     ;  first guess at track size
        sta  num         
        lda  #>4000      
        sta  num+1       
;
        jmp  end        ;  back to controller
;
;
;
l213    ldy  #0         ;  test if on right track number
        cmp  (hdrpnt),y          
        beq  l214        
;
        sta  (hdrpnt),y ;  goto right track
        jmp  end         
;
;
l214    lda  dskcnt     ;  test for write protect
        and  #$10        
        bne  topp       ;  its ok
;
        lda  #8         ;  write protect error
        jmp  fmterr      
;
topp    jsr  synclr     ;  erase track with sync
;
        jsr  wrtnum     ;  write out num syncs
;
        lda  #$55       ;  write out num non sync
        sta  data2       
;
        jsr  wrtnum      
;
        jsr  kill       ;  kill write
;
        jsr  sync       ;  find sync
;
        lda  #$40       ;  set timer mode
        ora  acr1        
        sta  acr1        
;
        lda  #100-2     ;  set up 100us timer
        sta  t1ll1      ;  cont mode timer
        lda  #0          
        sta  t1hl1      ;  hi latch
        sta  t1hc1      ;  get attention of '22
;
;
;
        ldy  #0         ;  time the sync and nonsync segments
        ldx  #0          
;
fwait   bit  dskcnt     ;  wait for sync
        bmi  fwait       
;
fwait2  bit  dskcnt     ;  wait for no sync
        bpl  fwait2      
;
f000    lda  t1lc1      ;  reset ifr
;
f001    bit  dskcnt     ;  time nonsync area
        bpl  f005       ;  time until sync found
;
        lda  ifr1       ;  test for time out
        asl  a   
        bpl  f001       ;  not yet
;
        inx     	;  .x is lsb
        bne  f000        
        iny     	;  .y is msb
        bne  f000        
;
        lda  #tolong    ;  cant find sync
        jmp  fmterr      
;
f005    stx  t2         ;  save time
        sty  t2+1        
;
        ldx  #0         ;  time sync area
        ldy  #0          
;
f006    lda  t1lc1      ;  reset ifr
;
f007    bit  dskcnt     ;  test for  no sync
        bmi  f009        
;
        lda  ifr1       ;  test for time out
        asl  a   
        bpl  f007        
;
        inx     	;  count up another 100us
        bne  f006        
        iny     	;  msb
        bne  f006        
;
        lda  #tolong    ;  cant be this long
        jmp  fmterr      
;
;
;* now calc the difference between
;* sync and nonsync and adjust
;* num accordingly
;
f009    sec     	;  t1-t2
        txa      
        sbc  t2          
        tax      
        sta  t1          
;
        tya      
        sbc  t2+1        
        tay      
        sta  t1+1        
;
        bpl  f013       ;  get abs(t1-t2)
;
        eor  #$ff       ;  make +
        tay      
        txa      
        eor  #$ff        
        tax      
        inx      
        bne  f013        
        iny      
;
f013    tya     	;  test if abs(t1-t2)<4, that is close enough
        bne  f014       ;  msb must be 0
;
        cpx  #4         ;  test lsb < 4
        bcc  count      ;  its there
;
f014    asl  t1         ;  num=num+(diff/2)
        rol  t1+1        
;
        clc      
        lda  t1          
        adc  num         
        sta  num         
;
        lda  t1+1        
        adc  num+1       
        sta  num+1       
;
        jmp  topp       ;  try again sam
;
;
count   ldx  #0         ;  now count #bytes in data segment
        ldy  #0          
        clv      
;
cnt10   lda  dskcnt     ;  test for sync
        bpl  cnt20      ;  found sync
        bvc  cnt10      ;  test if byte time
;
        clv     	;  yes, count it
        inx      
        bne  cnt10      ;  keep counting
        iny      
        bne  cnt10      ;  to many ?
;
        lda  #tomany    ;  tomany counts
        jmp  fmterr      
;
cnt20   txa     	;  #bytes=count*2
        asl  a   
        sta  tral+1      
;
        tya      
        rol  a   
        sta  tral        
;
        lda  #$ff-$40   ;  clear cont mode
        and  acr1        
        sta  acr1        
;
;
;.end
        .page  
	.subttl 'lccfmt2a.src'

gaptbl  .byte $21,$22,$23,$24,$25,$26,$27,$28,$29
gp2cmd=*-gaptbl
gp2tbl  .byte 2,2,4,6,8,8,11,19,22

; clear at least 6522 bytes written (trk1*1.08%)

spdchk  ldy  #0         ; 5100/6153  write 82% sync
	ldx  #28         
        jsr  jclear     ; clear whole disk & more
	jsr  fil_syn	; fill with sync
        jsr  kill       ; go read mode

	ldy  #mscnt      
2$	ldx  #mscnt      
3$	bit  dskcnt	; try to find sync
	bpl  5$		; got sync

        dex      
        bne  3$
        dey      
        bne  2$

4$      lda  #2          
        jmp  jfmte10    ; error

5$	ldy  #0        
        ldx  #0         
6$	bit  dskcnt     ; wait for sync to go away
	bpl  6$		; got sync

7$	lda  dskcnt     ; count time till next sync
        iny     	; lsb not used
        bne  8$
        inx     	; msb
8$	and  #$80
        bne  7$		; br, still no sync

        lda  #0          
        sta  tsttrk      
	txa		; msb

;*********************************************************
;*           ---  lookup gap2 in table ---               *
;*       x :  21 / 22 / 23 / 24 / 25 / 26 / 27 / 28 / 29 *
;*  gap2   :  02 / 02 / 04 / 06 / 08 / 08 / 0b / 13 / 16 *
;*  speed  : fast -------------------------------- slow  *
;*********************************************************
        ldx  #gp2cmd-1   
9$	cmp  gaptbl,x   ; lookup timing gap
        beq  10$
        dex      
        bpl  9$
        bmi  4$		; none

10$	lda  gp2tbl,x   ; look it up
        sta  dtrck       
        rts      

jformt  lda  ftnum      ; test if formating done
        bpl  1$		; yes

        lda  #$60       ; status = stepping
        sta  drvst       

	lda  fmtsid
	bne  3$

        lda  #01         
	.byte  skip2
3$      lda  #36
        sta  drvtrk     ; track 1/36
        sta  ftnum      ; track count =1/36
	cmp  #36	; set/clr carry flag
	jsr  set_side

        lda  #256-92    ; step back 45 tracks
        sta  steps       

        lda  dskcnt	; phase a
        and  #$ff-03     
        sta  dskcnt

        lda  #10         
        sta  cnt        ; init error count
        jmp  jend       ; go back to controler

1$	ldy  #00         
        lda  (hdrpnt),y          
        cmp  ftnum       
        beq  2$

        lda  ftnum       
        sta  (hdrpnt),y          
        jmp  jend         

2$	lda  dskcnt	; check wps
	and  #$10
        bne  toptst	; ok

        lda  #8          
        jmp  jfmterr    ; wp error

fil_syn ldx  #20        ; 20*256 bytes of sync
        lda  #$ff       ; write syncs
1$	bit  pota1
	bmi  1$
	
        sta  data2       
	bit  byt_clr

        dey      
        bne  1$
        dex      
        bne  1$
	rts

toptst  lda  tsttrk      
        bpl  1$

        jsr  spdchk      

1$	lda  dtrck       
			;  create header images
        clc      
        lda  #>bufs      
        sta  hdrpnt+1    
        lda  #00         
        sta  hdrpnt     ; point hdr point to buf0
        sta  sect        

        ldy  #0          
2$      lda  hbid       ; hbid cs s t id id 0f 0f
        sta  (hdrpnt),y          
        iny      
        lda  #00        ; check sum is zero for now
        sta  (hdrpnt),y          
        iny      

        lda  sect       ; store sector #
        sta  (hdrpnt),y          
        iny      

        lda  ftnum      ; store track #
        sta  (hdrpnt),y          
        iny      

        lda  dskid+1    ; store id low
        sta  (hdrpnt),y          
        iny      

        lda  dskid      ; store id hi
        sta  (hdrpnt),y          
        iny      

        lda  #$0f       ; store gap1 bytes
        sta  (hdrpnt),y          
        iny      
        sta  (hdrpnt),y          
        iny      

        tya     	; save this point
        pha      
        ldx  #07         
        lda  #00         
        sta  chksum     ; zero checksum
3$	dey      
        lda  (hdrpnt),y          
        eor  chksum      
        sta  chksum      
        dex      
        bne  3$	

        sta  (hdrpnt),y ; store checksum
        pla      
        tay     	; restore pointer

        inc  sect       ; goto next sector

        lda  sect       ; test if done yet
        cmp  sectr       
        bcc  2$		; more to do

	lda  #3
	sta  bufpnt+1	; $03XX

        jsr  fbtog      ; convert to gcr with no bid char
			; move buffer up 79 bytes

        ldy  #$ff-69    ; for i=n-1 to 0:mem+i+69|:=mem+i|:next
4$	lda  (hdrpnt),y ; move buf0 up 69 bytes
        ldx  #69         
        stx  hdrpnt      
        sta  (hdrpnt),y          
        ldx  #00         
        stx  hdrpnt      
        dey      
        cpy  #$ff        
        bne  4$

        ldy  #68        ; #bytes to move
5$	lda  ovrbuf+$bb,y    
        sta  (hdrpnt),y          
        dey      
        bpl  5$

;   create data block of zero

        clc      
        lda  #>bufs      
        adc  #02         
        sta  bufpnt+1   ; point to buf2
        lda  #00         
        tay     	; init offset
6$	sta  (bufpnt),y          
        iny      
        bne  6$

			
;   convert data block to gcr
;   write image
;   leave it in ovrbuf and buffer
        
        jsr  chkblk     ; get block checksum
	sta  chksum
        jsr  bingcr      

;   start the format now
;   write out sync header gap1
;   data block

        lda  #0         ; init counter
        sta  fmhdpt      

        ldx  #6         ; clear 8% of disk
        jsr  jclear     ; clear disk

7$	ldy  #numsyn    ; write 4 gcr bytes

8$	bit  pota1
	bmi  8$

	lda  #$ff       ; write sync
	sta  data2
	bit  byt_clr	; clear pa latch

        dey      
        bne  8$

        ldx  #10        ; write out header
        ldy  fmhdpt      

9$      bit  pota1
	bmi  9$

        lda  (hdrpnt),y ; get header data
        sta  data2       
	bit  byt_clr
	iny      

        dex
        bne  9$


; * write out gap1

        ldy  #gap1-2    ; write  gcr bytes

10$	bit  pota1
	bmi  10$	

        lda  #$55        
        sta  data2       
	bit  byt_clr
        
	dey      
        bne  10$

; * write out data block

        lda  #$ff       ; write data block sync
        ldy  #numsyn     

11$	bit  pota1
	bmi  11$

        sta  data2       
	bit  byt_clr

	dey      
        bne  11$

        ldy  #$bb       ; write out ovrbuf
12$	bit  pota1
	bmi  12$	

        lda  ovrbuf,y
        sta  data2       
	bit  byt_clr

        iny      
        bne  12$

13$	bit  pota1
	bmi  13$

        lda  (bufpnt),y          
        sta  data2       
	bit  byt_clr

        iny      
        bne  13$

        lda  #$55       ; write gap2(dtrck)
        ldy  dtrck       

14$	bit  pota1
	bmi  14$
	
        sta  data2       
	bit  byt_clr

        dey      
        bne  14$


        lda  fmhdpt     ; advance header pointer
        clc      
        adc  #10         
        sta  fmhdpt      

;  done writing sector

        dec  sect       ; go to next on
        beq  15$	; br, no more to do

        jmp  7$

15$	bit  pota1	; wait for last one to write
	bmi  15$

	bit  byt_clr

16$	bit  pota1	;wait for last one to write
	bmi  16$

	bit  byt_clr

        jsr  kill       ; goto read mode
	.page
	.subttl 'lccfmt2.src'
;
;
;
ds08    lda  #$66       ;  min block size 282*5/4 -256=85
;
        sta  dtrck       
;
        ldx  sectr      ;  total bytes= min*#secors
        ldy  #0          
        tya      
;
ds10    clc     	;  min*#sectors	
        adc  dtrck       
        bcc  ds14        
;
        iny    
;
ds14    iny      
        dex      
        bne  ds10        
;
        eor  #$ff       ;  get 2s comp
        sec      
        adc  #0          
;
        clc      
        adc  tral+1      
        bcs  ds15        
;
        dec  tral        
;
ds15    tax      
        tya      
        eor  #$ff        
        sec      
        adc  #0          
        clc      
        adc  tral        
;
        bpl  ds17        
;
        lda  #tobig     ;  not enough space
        jmp  fmterr      
;
ds17    tay      
        txa      
        ldx  #0          
;
ds20    sec      
        sbc  sectr       
        bcs  ds22        
;
        dey      
        bmi  ds30        
;
ds22    inx      
        bne  ds20        
;
ds30    stx  dtrck       

;-----rom05-bc---09/12/84------
        cpx  #gap2      ;  test for min size
;------------------------------

        bcs  ds32        
;
        lda  #tosmal    ;  gap2 to small
        jmp  fmterr      
;
ds32    clc      
        adc  sectr       
        sta  remdr      ;  get remaider size
;
;
;
;
;
;  create header images
;
;
        lda  #0          
        sta  sect        
;
        ldy  #0          
        ldx  drive       
;
mak10   lda  hbid       ;  hbid cs s t id id 0f 0f
        sta  buff0,y     
        iny      
;
        iny     	;  skip checksum
;
        lda  sect       ;  store sector #
        sta  buff0,y     
        iny      
;
        lda  ftnum      ;  store track #
        sta  buff0,y     
        iny      
;
        lda  dskid+1,x  ;  store id low
        sta  buff0,y     
        iny      
;
        lda  dskid,x    ;  store id hi
        sta  buff0,y     
        iny      
;
        lda  #$0f       ;  store gap1 bytes
        sta  buff0,y     
        iny      
        sta  buff0,y     
        iny      
;
        lda  #0         ; create checksum
        eor  buff0-6,y   
        eor  buff0-5,y   
        eor  buff0-4,y   
        eor  buff0-3,y   
;
        sta  buff0-7,y  ;  store checksum
;
;
        inc  sect       ;  goto next sector
;
        lda  sect       ;  test if done yet
        cmp  sectr       
        bcc  mak10      ;  more to do
;
        tya     	;  save block size
        pha      
;
;
;
;
;   create data block of zero
;
        inx     	;  .x=0
        txa      
;
crtdat  sta  buff2,x     
        inx      
        bne  crtdat      
;
;
;  convert header block to gcr
;
        lda  #>buff0     
        sta  bufpnt+1   ;  point at buffer
;
        jsr  fbtog      ;  convert to gcr with no bid char
;
        pla     	;   restore block size
        tay     	;  move buffer up 79 bytes
        dey     	;  for i=n-1 to 0:mem+i+69|:=mem+i|:next
        jsr  movup      ;  move buf0 up 69 bytes
;
        jsr  movovr     ;  move ovrbuf up to buffer
;
;
;
;   convert data block to gcr
;   write image
;
;   leave it in ovrbuf and buffer
;
;
        lda  #>buff2    ;  point at buffer
        sta  bufpnt+1    
;
;
        jsr  chkblk     ;  get block checksum
        sta  chksum      
        jsr  bingcr      
;
;
;
;   start the format now
;
;   write out sync header gap1
;   data block
;
;
;
        lda  #0         ;  init counter
        sta  hdrpnt      
;
        jsr  clear      ;  clear disk
;
wrtsyn  lda  #$ff       ;  write sync
        sta  data2       
;
        ldx  #numsyn    ;  write 4  sync
;
wrts10  bvc  *   
        clv      
;
        dex      
        bne  wrts10      
;
        ldx  #10        ;  write out header
        ldy  hdrpnt      
;
wrts20  bvc  *   
        clv      
;
        lda  buff0,y    ;  get header data
        sta  data2       
;
        iny      
        dex      
        bne  wrts20      
;
;
; * write out gap1
;
        ldx  #gap1-2    ;  write  gcr bytes
;
wrts30  bvc  *   
        clv      
;
        lda  #$55        
        sta  data2       
;
        dex      
        bne  wrts30      
;
;
;
; * write out data block
;
        lda  #$ff       ;  write data block sync
;
        ldx  #numsyn     
;
dbsync  bvc  *   
        clv      
;
        sta  data2       
;
        dex      
        bne  dbsync      
;
        ldx  #256-topwrt ;  write out ovrbuf
;
wrts40  bvc  *   
        clv      
;
        lda  ovrbuf,x    
        sta  data2       
;
        inx      
        bne  wrts40      
;
;
        ldy  #0          
;
wrts50  bvc  *   
        clv      
;
        lda  (bufpnt),y          
        sta  data2       
;
        iny      
        bne  wrts50      
;
        lda  #$55       ;  write gap2(dtrck)
        ldx  dtrck       
;
wgp2    bvc  *   
        clv      
;
        sta  data2       
        dex      
        bne  wgp2        
;
; 	ldx #20 	; write erase trail gap
;wgp3   bvc *
;   	clv
; 	dex
; 	bne wgp3
;
        lda  hdrpnt     ;  advance header pointer
        clc      
        adc  #10         
        sta  hdrpnt      
;
;
;
;  done writing sector
;
        dec  sect       ;  go to next on
        bne  wrtsyn     ;  more to do
;
        bvc  *          ;  wait for last one to write
        clv      
;
        bvc  *   
        clv      
;
        jsr  kill       ;  goto read mode
;
;
;
;.end
         .page  
         .subttl 'lccfmt3a.src'

;*   format done, now verify it

vfmt	lda  #200       ;  look at 200 syncs
        sta  trys        

1$	lda  #00         
        sta  fmhdpt     ; start with first sector again
        lda  sectr      ;  sector counter
        sta  sect        
2$	jsr  jsync      ;  find sync
	ldx  #10         
        ldy  fmhdpt     ; current header pointer

3$	lda  (hdrpnt),y          
4$	bit  pota1	;  get header byte
	bmi  4$
	
        cmp  data2       
        bne  5$		; error

        iny      
        dex
        bne  3$		;  test all bytes

        clc     	; update headr pointer
        lda  fmhdpt      
        adc  #10         
        sta  fmhdpt      
	
        jmp  6$		;  now test data

5$	dec  trys       ;  test if too many errors
        bne  1$

        lda  #notfnd    ;  too many error
        jmp  jfmterr      

6$	jsr  jsync      ;  find data sync

        ldy  #256-topwrt	
7$	lda  ovrbuf,y    ; ovr buff offset
8$	bit  pota1
	bmi  8$

        cmp  data2      ;  compare gcr
        bne  5$		; error

        iny      
        bne  7$		;  do all ovrbuf

9$	lda  (bufpnt),y          
10$	bit  pota1
	bmi  10$

        cmp  data2       
        bne  5$

        iny      
        bne  9$

        dec  sect       ; more sectors to test?
        bne  2$		; yes

;  all sectors done ok

        inc  ftnum      ;  goto next track
        lda  ftnum       
	bit  side 	;  what side are we on ?
	bmi  13$

        cmp  #36        ;  #tracks max
	.byte skip2
13$	cmp  #71
        bcs  12$

        jmp  jend       ;  more to do


12$	lda  #$ff       ;  clear ftnum
        sta  ftnum       

        lda  #$0        ;  clear gcr buffer flag
        sta  gcrflg      

        lda  #1         ;  return ok code
        jmp  jerrr        
	.page
	.subttl 'lccfmt3.src'      
;
;
;
;*   format done, now verify it
;
;
;
        lda  #200       ;  look at 200 syncs
        sta  trys        
;
comp    lda  #0         ;  pointer into headers
        sta  bufpnt      
;
        lda  #>buff0     
        sta  bufpnt+1    
;
        lda  sectr      ;  sector counter
        sta  sect        
;
cmpr10  jsr  sync       ;  find sync
;
        ldx  #10         
        ldy  #0          
;
cmpr15  bvc  *          ;  get header byte
        clv      
;
        lda  data2       
        cmp  (bufpnt),y ;  compare gcr
;
        bne  cmpr20     ; error
;
        iny      
        dex      
        bne  cmpr15     ;  test all bytes
;
        clc     	;  update headr pointer
        lda  bufpnt      
        adc  #10         
        sta  bufpnt      
;
        jmp  tstdat     ;  now test data
;
cmpr20  dec  trys       ;  test if too many errors
        bne  comp        
;
        lda  #notfnd    ;  too many error
        jmp  fmterr      
;
tstdat  jsr  sync       ;  find data sync
;
        ldy  #256-topwrt         
;
tst05   bvc  *   
        clv      
;
        lda  data2      ;  compare gcr
        cmp  ovrbuf,y    
;
        bne  cmpr20     ;  error
;
        iny      
        bne  tst05      ;  do all ovrbuf
;
        ldx  #255-3     ;  now do buffer, dont test off bytes
;
tst10   bvc  *   
        clv      
;
        lda  data2       
        cmp  buff2,y     
        bne  cmpr20      
;
        iny      
        dex      
        bne  tst10       
;
;
        dec  sect       ;  more sectors to test?
        bne  cmpr10     ;  yes
;
;
;  all sectors done ok
;
        inc  ftnum      ;  goto next track
        lda  ftnum       
        cmp  #36        ;  #tracks max
        bcs  fmtend      
;
        jmp  end        ;  more to do
;
;
fmtend  lda  #$ff       ;  clear ftnum
        sta  ftnum       
;
        lda  #$0        ;  clear gcr buffer flag
        sta  gcrflg      
;
        lda  #1         ;  return ok code
        jmp  errr        
;
;
;
;.end
        .page  
	.subttl 'lccfmt4a.src'

jfmterr dec  cnt        ;  test for retry
        beq  jfmte10      

        jmp  jend         

jfmte10 ldy  #$ff        
        sty  ftnum      ;  clear format

        iny      
        sty  gcrflg      

        jmp  jerrr        

; this subroutine will be called with x*255 for amount of chars written

jclear  lda  pcr2	;  enable write
	and  #$ff-$e0   ;  wr mode=0
	ora  #$c0
	sta  pcr2

        lda  #$ff       ;  make port an output
        sta  ddra2      ;  clear pending
        
        lda  #$55       ;  write a 1f pattern
        ldy  #00         
1$	bit  pota1
	bmi  1$

	bit  byt_clr
        sta  data2       

        dey      
        bne  1$

        dex     	;  dex amount * 255
        bne  1$

        rts      
	.page   
	.subttl 'lccfmt4.src'
;
;
;
synclr  lda  pcr2       ;  write entire track with sync
        and  #$ff-$e0    
        ora  #$c0        
        sta  pcr2        
;
        lda  #$ff       ;  output mode ddr
        sta  ddra2       
;
        sta  data2      ;  sync char
;
        ldx  #$28       ;  $28*256 bytes
        ldy  #0          
;
syc10   bvc  *   
        clv      
;
        dey      
        bne  syc10       
;
        dex      
        bne  syc10       
;
        rts     	;  leave write on
;
;
;
wrtnum  ldx  num        ;  write out num bytes
        ldy  num+1       
;
wrtn10  bvc  *   
        clv      
;
        dex      
        bne  wrtn10      
;
        dey      
        bpl  wrtn10      
;
        rts      
;
;
;
fmterr  dec  cnt        ;  test for retry
        beq  fmte10      
;
        jmp  end         
;
fmte10           
;
        ldy  #$ff        
        sty  ftnum      ;  clear format
;
        iny      
        sty  gcrflg      
;
        jmp  errr        
;
;
;
movup   lda  buff0,y    ;   move up 69 bytes
        sta  buff0+69,y ;  move from top down
        dey      
        bne  movup       
;
        lda  buff0      ;  do last byte
        sta  buff0+69    
        rts      
;
;
movovr  ldy  #68        ;  move ovrbuf into (buffer)
;
movo10  lda  ovrbuf+256-topwrt,y         
        sta  (bufpnt),y          
;
        dey      
        bpl  movo10      
;
        rts      
;
;
;
kill    lda  pcr2       ;  disable write
        ora  #$e0        
        sta  pcr2        
        lda  #$00       ;  make port input now
        sta  ddra2       
;
        rts      
;
;
;
clear   lda  pcr2       ;  enable write
        and  #$ff-$e0    
        ora  #$c0        
        sta  pcr2        
;
        lda  #$ff       ;  make port an output
        sta  ddra2       
;
        lda  #$55       ;  write a 1f pattern
        sta  data2       
;
        ldx  #$28       ;  $28*256 chars
        ldy  #00         
cler10  bvc  *   
        clv      
        dey      
        bne  cler10      
;
        dex      
        bne  cler10      
;
        rts      
;
;*****************************
;*
;*
;*     fbtog
;*     format binary to gcr conversion
;*
;*     converts buffer to gcr with out hbid
;*
;***************************
;
fbtog   lda  #0         ;  point at buffer
        sta  bufpnt      
        sta  savpnt      
        sta  bytcnt      
;
        lda  #256-topwrt ;  put gcr in ovrflow buffer
        sta  gcrpnt      
;
        lda  bufpnt+1   ;  save buffer pointer
        sta  savpnt+1    
;
        lda  #>ovrbuf    
        sta  bufpnt+1   ;  store in overbuf
;
fbg10   ldy  bytcnt     ;  get pointer
;
        lda  (savpnt),y          
        sta  btab        
        iny      
;
        lda  (savpnt),y          
        sta  btab+1      
        iny      
;
        lda  (savpnt),y          
        sta  btab+2      
        iny      
;
        lda  (savpnt),y          
        sta  btab+3      
        iny      
        beq  fbg15      ;  test if done
;
        sty  bytcnt     ;  save pointer
;
        jsr  put4bg     ;  convert and store
;
        jmp  fbg10       
;
fbg15   jmp  put4bg     ;  done, return
;
;.end
	.page   
	.subttl 'lccgcrbin.src'

mask1=$f8        
mask2=$07        
mask2x=$c0       
mask3=$3e        
mask4=$01        
mask4x=$f0       
mask5=$0f        
mask5x=$80       
mask6=$7c        
mask7=$03        
mask7x=$e0       
mask8=$1f        
;
;
;
;
; fast gcr to binary conversion
;
get4gb  ldy  gcrpnt      
;
        lda  (bufpnt),y          
        and  #mask1      
        lsr  a   
        lsr  a   
        lsr  a   
        sta  gtab       ;  hi nibble

        lda  (bufpnt),y          
        and  #mask2      
        asl  a   
        asl  a   
        sta  gtab+1      

        iny       	;  next byte
        bne  xx05       ;  test for next buffer
        lda  nxtbf       
        sta  bufpnt+1    
        ldy  nxtpnt      

xx05    lda  (bufpnt),y          
        and  #mask2x     
        rol  a   
        rol  a   
        rol  a   
        ora  gtab+1      
        sta  gtab+1      

        lda  (bufpnt),y          
        and  #mask3      
        lsr  a   
        sta  gtab+2      

        lda  (bufpnt),y          
        and  #mask4      
        asl  a   
        asl  a   
        asl  a   
        asl  a   
        sta  gtab+3      

        iny     		;  next

        lda  (bufpnt),y          
        and  #mask4x     
        lsr  a   
        lsr  a   
        lsr  a   
        lsr  a   
        ora  gtab+3      
        sta  gtab+3      

        lda  (bufpnt),y          
        and  #mask5      
        asl  a   
        sta  gtab+4      

        iny     		;  next byte

        lda  (bufpnt),y          
        and  #mask5x     
        clc      
        rol  a   
        rol  a   
        and  #1          
        ora  gtab+4      
        sta  gtab+4      

        lda  (bufpnt),y          
        and  #mask6      
        lsr  a   
        lsr  a   
        sta  gtab+5      

        lda  (bufpnt),y          
        and  #mask7      
        asl  a   
        asl  a   
        asl  a   
        sta  gtab+6      

        iny      
; test for overflow during write to binary conversion
        bne  xx06        
        lda  nxtbf       
        sta  bufpnt+1    
        ldy  nxtpnt      

xx06    lda  (bufpnt),y          
        and  #mask7x     
        rol  a   
        rol  a   
        rol  a   
        rol  a   
        ora  gtab+6      
        sta  gtab+6      

        lda  (bufpnt),y          
        and  #mask8      
        sta  gtab+7      
        iny      

        sty  gcrpnt      


        ldx  gtab        
        lda  gcrhi,x     
        ldx  gtab+1      
        ora  gcrlo,x     
        sta  btab        

        ldx  gtab+2      
        lda  gcrhi,x     
        ldx  gtab+3      
        ora  gcrlo,x     
        sta  btab+1      

        ldx  gtab+4      
        lda  gcrhi,x     
        ldx  gtab+5      
        ora  gcrlo,x     
        sta  btab+2      

        ldx  gtab+6      
        lda  gcrhi,x     
        ldx  gtab+7      
        ora  gcrlo,x     
        sta  btab+3      

        rts      

gcrhi   .byte  	$ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $80     
	.byte   $00     
	.byte   $10     
	.byte   $ff    ; error
	.byte   $c0     
	.byte   $40     
	.byte   $50     
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $20     
	.byte   $30     
	.byte   $ff    ; error
	.byte   $f0     
	.byte   $60     
	.byte   $70     
	.byte   $ff    ; error
	.byte   $90     
	.byte   $a0     
	.byte   $b0     
	.byte   $ff    ; error
	.byte   $d0     
	.byte   $e0     
	.byte   $ff    ; error
;
;
;
gcrlo   .byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   8       
	.byte   $00     
	.byte   1       
	.byte   $ff    ; error
	.byte   $c      
	.byte   4       
	.byte   5       
	.byte   $ff    ; error
	.byte   $ff    ; error
	.byte   2       
	.byte   3       
	.byte   $ff    ; error
	.byte   $f      
	.byte   6       
	.byte   7       
	.byte   $ff    ; error
	.byte   9       
	.byte   $a      
	.byte   $b      
	.byte   $ff    ; error
	.byte   $d      
	.byte   $e      
	.byte   $ff    ; error
;
;
gcrbin  lda  #0         ;  setup pointers
        sta  gcrpnt      
        sta  savpnt      
        sta  bytcnt      
;
        lda  #>ovrbuf    
        sta  nxtbf       
;
        lda  #255-toprd          
        sta  nxtpnt      
;
        lda  bufpnt+1    
        sta  savpnt+1    
;
        jsr  get4gb      
;
        lda  btab        
        sta  bid        ;  get header id
;
        ldy  bytcnt      
        lda  btab+1      
        sta  (savpnt),y          
        iny      
;
        lda  btab+2      
        sta  (savpnt),y          
        iny      
;
        lda  btab+3      
        sta  (savpnt),y          
        iny      
;
gcrb10  sty  bytcnt      
;
        jsr  get4gb      
;
        ldy  bytcnt      
;
        lda  btab        
        sta  (savpnt),y          
        iny      
        beq  gcrb20     ;  test if done yet
;
        lda  btab+1      
        sta  (savpnt),y          
        iny      
;
        lda  btab+2      
        sta  (savpnt),y          
        iny      
;
        lda  btab+3      
        sta  (savpnt),y          
        iny      
;
        bne  gcrb10     ;  jmp
;
gcrb20           
        lda  btab+1      
        sta  chksum      
        lda  savpnt+1   ; restore buffer pointer
        sta  bufpnt+1    
;
        rts      
;.end
        .page  
	.subttl 'lccgcrbn1.src'

; even faster gcr to binary conversion

jget4gb ldy  gcrpnt      

        lda  (bufpnt),y          
        sta  gtab       	;  A

        and  #mask2      
        sta  gtab+1      

        iny             	;  next byte
        bne  1$			;  test for next buffer
        lda  nxtbf       
        sta  bufpnt+1    
        ldy  nxtpnt      

1$	lda  (bufpnt),y          
	sta  gtab+2		;  C

        and  #mask2x     
        ora  gtab+1      
        sta  gtab+1     	;  B

	lda  gtab+2
        and  #mask4      
        sta  gtab+3    

        iny     		;  next

        lda  (bufpnt),y          
        tax      
        and  #mask4x     
        ora  gtab+3      
        sta  gtab+3     	;  D

        txa      
        and  #mask5      
        sta  gtab+4     

        iny     		;  next byte

        lda  (bufpnt),y          
	sta  gtab+5		;  F

        and  #mask5x     
        ora  gtab+4      
        sta  gtab+4     	;  E
	
	lda  gtab+5
        and  #mask7      
        sta  gtab+6     

        iny      

; test for overflow during write to binary conversion

        bne  2$

        lda  nxtbf       
        sta  bufpnt+1    
        ldy  nxtpnt      
        sty  bufpnt      

2$	lda  (bufpnt),y          
	sta  gtab+7    		;  H

        and  #mask7x     
        ora  gtab+6      
        sta  gtab+6    		;  G

        iny      

        sty  gcrpnt      

        ldx  gtab        
        lda  gcrtb1,x  		;  a
        ldx  gtab+1      
        ora  gcrtba,x  		;  b
        sta  btab        

        ldx  gtab+2      
        lda  gcrtb2,x    	;  c
        ldx  gtab+3      
        ora  gcrtbd,x    	;  d
        sta  btab+1      

        ldx  gtab+4      
        lda  gcrtbe,x    	;  e
        ldx  gtab+5      
	ora  gcrtb3,x    	;  f
        sta  btab+2      

        ldx  gtab+6      
        lda  gcrtbg,x    	;  g
        ldx  gtab+7      
        ora  gcrtb4,x    	;  h
        sta  btab+3      

        rts      


jgcrbin lda  #0         ;  setup pointers
        sta  gcrpnt      
        sta  savpnt      
        sta  bytcnt      

        lda  #>ovrbuf	; point to overflow first
        sta  nxtbf       

        lda  #255-toprd	; overflow offset
        sta  nxtpnt      

        lda  bufpnt+1    
        sta  savpnt+1    

        jsr  jget4gb      

        lda  btab        
        sta  bid        ;  get header id

        ldy  bytcnt      
        lda  btab+1      
        sta  (savpnt),y          
        iny      

        lda  btab+2      
        sta  (savpnt),y          
        iny      

        lda  btab+3      
        sta  (savpnt),y          
        iny      

1$	sty  bytcnt      

        jsr  jget4gb      

        ldy  bytcnt      

        lda  btab        
        sta  (savpnt),y          
        iny      
        beq  2$			; test if done yet

        lda  btab+1      
        sta  (savpnt),y          
        iny      

        lda  btab+2      
        sta  (savpnt),y          
        iny      

        lda  btab+3      
        sta  (savpnt),y          
        iny      

        bne  1$		;  jmp

2$      lda  savpnt+1   ; restore buffer pointer
        sta  bufpnt+1    
        rts      
	.subttl 'lccinit.src'      
	.page   

;     initialization of controller

cntint  lda  #%01101111         ;  data direction
        sta  ddrb2       
        and  #$ff-$08-$04-$03   ;  turn motor off,set phase a, led off
        sta  dskcnt      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch48	; *** rom ds 04/10/85 ***
;       lda  pcr2       ;  set edge and latch mode

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

rtch48  and  #$ff-$01   ;  neg edge please

;  ca2: soe output hi disable s.o. into 6502

        ora  #$0e       ; cb1 input only
        ora  #$e0       ; cb2 mode control r/w
        sta  pcr2        

        lda  #$41       ;  cont irq, latch mode
        sta  acr2        

;--------9/25 rom05-bc-------------
        lda  #0          
        sta  t1ll2       
        lda  #tim       ; / 8 ms /irq
        sta  t1hl2       
        sta  t1hc2      ;  get 6522's attention
;----------------------------------

        lda  #$7f       ;  clear all irq sources
        sta  ier2        

        lda  #$80+$40    
        sta  ifr2       ;  clear bit
        sta  ier2       ;  enable irq

        lda  #$ff       ;  no current drive
        sta  cdrive      
        sta  ftnum      ;  init format flag

        lda  #$08       ;  header block id
        sta  hbid        

        lda  #$07       ;  data block id
        sta  dbid        

        lda  #<inact     
        sta  nxtst       
        lda  #>inact     
        sta  nxtst+1     

        lda  #200        
        sta  minstp      

        lda  #4          
        sta  as          

        lda  #$4         
        sta  af          
	.page   'lcc.i/o'       
;
cntst=  *        
;
;
;
;     defs for low cost controller
;
;
;     written by glenn stark
;     4/1/80
;
;
;   (c) commodore business machines
;
timer1   =$1805         ;  timer 1 counter
;
;
;
;   mos 6522
;   address $1c00
;
        *  =$1c00        
;
dskcnt   *=*+1          ;  port b
; disk i/o control lines
; bit 0: step in
; bit 1: step out
; bit 2: -motor on
; bit 3: act led
; bit 4: write protect sense
; bit 5: density select 0
; bit 6: density select 1
; bit 7: sync detect
;
;
data2    *=*+1          ;  port a
; gcr data input and output port
;
ddrb2    *=*+1          ;  data direction control
ddra2    *=*+1          ;  data direction control
;
t1lc2    *=*+1          ;  timer 1 low counter
t1hc2    *=*+1          ;  timer 1 hi countr
;
t1ll2    *=*+1          ;  timer 1 low latch
t1hl2    *=*+1          ;  timer 1 hi latch
;
t2ll2    *=*+1          ;  timer two low latch
t2lh2    *=*+1          ;  timer two hi latch
;
sr2      *=*+1          ;  shift register
;
acr2     *=*+1   
;
pcr2     *=*+1   
;
ifr2     *=*+1   
;
ier2     *=*+1   
;
;
        *  =cntst        
;
;
;
;.end
        .page  
	.subttl 'lccread1.src'

;=============================================================
;=     8    =     8     =     8     =     8     =     8      =
;=============================================================
;   (5) (3  + 2)  (5) (1+  4)   (4  +1) (5)  (2 +  3)   (5)
;    a      b      c    d           e    f      g        h
;=============================================================

;    read in track,sector specified
;    in header

	*=*+256-<*	;  even page

jdstrt  jsr  jsrch      ;  find header
        jmp  jsync      ;  and then data block sync

jread   cmp  #0         ;  test if read job
        beq  jread01    ;  go test if write
        jmp  jwright      

jread01 jsr  jdstrt     ;  find header and start reading data
			; sync routine sets y to 0

1$	bit  pota1	;  4
	bmi  1$		;  3 + 2

        lda  data2      ;  4
        tax     	;  2   reg x = xxxxx000
        lda  gcrtb1,x   ;  4   nibble a
        sta  btab	;  3
        txa		;  2
        and  #%00000111 ;  2
        sta  btab+1	;  3   extract 3 bits nibble b

2$	bit  pota1	;  4
	bmi  2$		;  3 + 2

        lda  data2      ;  4
        sta  btab+2	;  3
        and  #%11000000 ;  2   extract 2 bits nibble b
        ora  btab+1	;  3
        tax     	;  2   reg x = xx000xxx
        lda  gcrtba,x   ;  4   nibble b
        ora  btab	;  3
        pha             ;  3
        jmp  5$		;  3   

;********************************************************************

3$	bit  pota1	;  4
	bmi  3$		;  3 + 2

        lda  data2      ;  4
        tax     	;  2   reg x = xxxxx000
        lda  gcrtb1,x   ;  4   nibble a
        sta  btab	;  3
	txa		;  2
        and  #%00000111 ;  2
        sta  btab+1	;  3   extract 3 bits nibble b

4$	bit  pota1	;  4
	bmi  4$		;  3 + 2

	lda  data2      ;  4
        sta  btab+2	;  3
        and  #%11000000 ;  2
        ora  btab+1	;  3
        tax             ;  2   reg x = xx000xxx
        lda  gcrtba,x   ;  4   nibble b
        ora  btab	;  3
        sta  (bufpnt),y ;  6
        iny     	;  2
        beq  6$		;  2

5$	lda  btab+2	;  3
        tax     	;  2   reg x = 00xxxxx0
        lda  gcrtb2,x   ;  4   nibble c
        sta  btab	;  3
	txa		;  2
        and  #%00000001 ;  2
        sta  btab+2	;  3   extract 1 bits nibble d

7$	bit  pota1	;  4
	bmi  7$		;  3 + 2

        lda  data2      ;  4
        sta  btab+3	;  3
        and  #%11110000 ;  2
        ora  btab+2	;  3
        tax     	;  2   reg x = xxxx000x
        lda  gcrtbd,x   ;  4   nibble d
        ora  btab	;  3
        sta  (bufpnt),y ;  6
        iny     	;  2
        lda  btab+3	;  3
        and  #%00001111 ;  2
        sta  btab+3	;  3   extract 4 bits nibble e

8$	bit  pota1	;  4
	bmi  8$		;  3 + 2

        lda  data2      ;  4
        sta  chksum	;  3
        and  #%10000000 ;  2
        ora  btab+3	;  3
        tax     	;  2   reg x = x000xxxx
        lda  gcrtbe,x   ;  4   nibble e
        sta  btab	;  3
        lda  chksum	;  3
        tax     	;  2   reg x = 0xxxxx00
        lda  gcrtb3,x   ;  4   nibble f
        ora  btab	;  3
        sta  (bufpnt),y ;  6
        iny     	;  2
	txa		;  2
        and  #%00000011 ;  2
        sta  chksum	;  3   extract 2 bits nibble g

9$	bit  pota1	;  4
	bmi  9$		;  3 + 2

        lda  data2      ;  4
        sta  btab+1	;  3
        and  #%11100000 ;  2
        ora  chksum	;  3
        tax     	;  2   reg x = xxx000xx
        lda  gcrtbg,x   ;  4   nibble g
        sta  btab	;  3
        lda  btab+1	;  3
        tax     	;  2   reg x = 000xxxxx
        lda  gcrtb4,x   ;  4   nibble h
        ora  btab	;  3
        sta  (bufpnt),y ;  6
        iny     	;  2
        jmp  3$		;  4

;*******************************************************************

6$	lda  btab+2	;  3
        tax     	;  2   reg x = 00xxxxx0
        lda  gcrtb2,x   ;  4   nibble c
        sta  btab	;  3
	txa		;  2
        and  #%00000001 ;  2
        sta  btab+2	;  3

10$	bit  pota1	;  4
	bmi  10$	;  3 + 2

        lda  data2      ;  4
        and  #%11110000 ;  2
        ora  btab+2	;  3
        tax     	;  2   reg x = xxxx000x
        lda  gcrtbd,x   ;  4   nibble d
        ora  btab	;  3
        sta  btab+1	;  3   store off cs byte

        pla     	; retrieve first byte off of disk
        cmp  dbid       ; see if it is a 7
        bne  12$	; br, nope


	jsr  chkblk     ; calc checksum
        cmp  btab+1
        beq  11$

        lda  #5         ; data block checksum error
        .byte    skip2   
12$	lda  #4          
        .byte    skip2   

11$	lda  #1         ; read data block ok
        jmp  jerrr        


jsrch   lda  dskid      ; get master id for the drive
        sta  header      
        lda  dskid+1     
        sta  header+1    
	
        ldy  #0         ; get track,sectr
        lda  (hdrpnt),y          
        sta  header+2    
        iny      
        lda  (hdrpnt),y          
        sta  header+3    

        lda  #0          
        eor  header      
        eor  header+1    
        eor  header+2    
        eor  header+3    
        sta  header+4   ; store the checksum
        jsr  conhdr     ; convert header to gcr

        lda  #90        ; search 90 sync chars
        sta  tmp         
1$	jsr  jsync      ; find sync

2$	lda  stab,y     ; what it should be
3$	bit  pota1
	bmi  3$

        cmp  data2      ; is it the same .cmp absolute
        bne  4$		; nope

        iny      
        cpy  #8          
        bne  2$

        rts      

4$	dec  tmp        ; try again
        bne  1$

        lda  #2         ; cant find this header
	jmp  jerrr        


jsync   ldx  #15
	ldy  #0		; s/w timers ok
1$	bit  dskcnt	; sync a synch ?
	bpl   2$

	dey
	bne  1$
	
	dex
	bne  1$

	lda  #3
	jmp  jerrr	; sync error
	
2$	lda  data2	; clear pa latch
	ldy  #0         ; clear pointer
        rts      
	.page  
	.subttl  'lcc.read'      
;
;
;
;   *read
;
;    read in track,sector specified
;    in header
;
;
reed    cmp  #0         ;  test if read job
        beq  read01     ;  go test if write
        jmp  wright      
;
read01  jsr  dstrt      ;  find header and start reading data
;
read11  bvc  *          ;  wait for byte
        clv      
;
        lda  data2      ;  store away data
        sta  (bufpnt),y ;  in data buffer
        iny      
        bne  read11      
;
        ldy  #255-toprd ;  store rest in overflow buffer
;
read20  bvc  *   
        clv      
;
        lda  data2       
        sta  ovrbuf,y    
        iny      
        bne  read20      
;
        jsr  gcrbin     ;  convert buffer to binary
;
        lda  bid        ;  test if its a data block
        cmp  dbid        
        beq  read28      
;
        lda  #4         ;  not a data block
        jmp  errr        
;
read28  jsr  chkblk     ;  calc checksum
;
        cmp  chksum      
        beq  read40      
;
        lda  #5         ;  data block checksum error
	.byte  skip2   
;
read40  lda  #1         ;  read data block ok
        jmp  errr        
;
;
;
dstrt   jsr  srch       ;  find header
        jmp  sync       ;  and then data block sync
;
;
srch    lda  drive      ;  create header image
        asl  a   
        tax      
;
        lda  dskid,x    ;  get master id for the drive
        sta  header      
        lda  dskid+1,x   
        sta  header+1    
;
        ldy  #0         ;  get track,sector
        lda  (hdrpnt),y          
        sta  header+2    
        iny      
        lda  (hdrpnt),y          
        sta  header+3    
;
        lda  #0          

;create header checksum

        eor  header      
        eor  header+1    
        eor  header+2    
        eor  header+3    
;
        sta  header+4   ;  store the checksum
;
        jsr  conhdr     ;  convert header to gcr
;
        ldx  #90        ;  search 90 sync chars
;
srch20  jsr  sync       ;  find sync
;
        ldy  #0         ;  test 8 gcr bytes
;
srch25  bvc  *   
        clv     	;  wait for byte
;
        lda  data2       
        cmp  stab,y     ;  test if the same
        bne  srch30     ;  nope
;
        iny      
        cpy  #8          
        bne  srch25      
;
        rts      
;
;
srch30  dex     	; try again
        bne  srch20      
;
        lda  #2         ;  cant find this header
err     jmp  errr        
;
;
sync             
;
        lda  #$80+80    ;  wait 20 ms for sync max
        sta  timer1      
;
        lda  #3         ;  error code for no sync
;
sync10  bit  timer1     ;  test for time out
        bpl  err         
        bit  dskcnt     ;  test for sync
        bmi  sync10      
;
;
        lda  data2      ;  reset pa latch
        clv      
        ldy  #0         ;  clear pointer
        rts      
;
;
;
;.end
        .page  
	.subttl 'lccseek1.src'

jseak   lda  #90        ; search 90 headers
        sta  tmp         

1$	jsr  jsync      ; find sync char

2$	bit  pota1	; wait for block id
	bmi  2$

        lda  data2      ; clear pa1 in the gate array
        cmp  #$52       ; test if header block
        bne  3$		; not header

        sta  stab,y   	; store 1st byte
        iny      

4$	bit  pota1
	bmi  4$

        lda  data2       
        sta  stab,y     ; store gcr header off

        iny      
        cpy  #8         ; 8 gcr bytes in header
        bne  4$

        jsr  jcnvbin    ; convert header in stabof to binary in header

        ldy  #4         ; compute checksum
        lda  #0          

5$	eor  header,y    
        dey      
        bpl  5$

        cmp  #0         ; test if ok
        bne  9$		; nope, checksum error in header

        lda  header+2    
        sta  drvtrk      

        lda  job        ; test if a seek job
        cmp  #$30        
        beq  6$

        lda  dskid       
        cmp  header      
        bne  8$

        lda  dskid+1     
        cmp  header+1    
        bne  8$

	jmp  7$		; find best sector to service

3$	dec  tmp        ; search more?
        bne  1$		; yes

        lda  #2         ; cant find a sector
        jsr  jerrr        

6$	lda  header     ; sta disk id's
        sta  dskid      ; *
        lda  header+1   
        sta  dskid+1   

	lda  #1         ; return ok code
        .byte    skip2   

8$	lda  #11        ; disk id mismatch
        .byte    skip2   

9$	lda  #9         ; checksum error in header
        jmp  jerrr        

7$	lda  #$7f       ; find best job
        sta  csect       

        lda  header+3   ; get upcoming sector #
        clc      
        adc  #2          
        cmp  sectr       
        bcc  10$

        sbc  sectr      ; wrap around

10$	sta  nexts      ; next sector

        ldx  #numjob-1   
        stx  jobn        

        ldx  #$ff        

12$	jsr  jsetjb       
        bpl  11$

        and  #drvmsk     
        cmp  cdrive     ; test if same drive
        bne  11$	; nope

        ldy  #0         ; test if same track
        lda  (hdrpnt),y          
        cmp  tracc       
        bne  11$

	lda  job
	cmp  #execd
	beq  13$

        ldy  #1          
        sec      
        lda  (hdrpnt),y          
        sbc  nexts       
        bpl  13$

        clc      
        adc  sectr       

13$	cmp  csect       
        bcs  11$

        pha     	; save it
        lda  job         
        beq  16$	; must be a read

        pla      
        cmp  #wrtmin    ; +if(csect<4)return;
        bcc  11$	; +if(csect>8)return;

        cmp  #wrtmax     
        bcs  11$

15$	sta  csect      ; its better
        lda  jobn        
        tax      
        clc      
        adc  #>bufs      
        sta  bufpnt+1    

        bne  11$

16$	pla      
        cmp  #rdmax     ; if(csect>6)return;
        bcc  15$

11$	dec  jobn        
        bpl  12$

        txa     	; test if a job to do
        bpl  14$

        jmp  jend       ; no job found

14$	stx  jobn        
        jsr  jsetjb       
        lda  job         
        jmp  jread        

jcnvbin lda  bufpnt      
        pha      
        lda  bufpnt+1    
        pha     	; save buffer pntr

        lda  #<stab	; stab offset
        sta  bufpnt     ; point at gcr code
        lda  #>stab
        sta  bufpnt+1    

        lda  #0          
        sta  gcrpnt      

        jsr  jget4gb    ; convert 4 bytes

        lda  btab+3      
        sta  header+2    

        lda  btab+2      
        sta  header+3    

        lda  btab+1      
        sta  header+4    


        jsr  jget4gb    ; get 2 more

        lda  btab       ; get id
        sta  header+1    
        lda  btab+1      
        sta  header      

        pla      
        sta  bufpnt+1   ; restore pointer
        pla      
        sta  bufpnt      
        rts      
	.page  
	.subttl 'lccseek.src'
;
;
;
seak    ldx  #90        ;  search 90 headers
        stx  tmp         
;
        ldx  #0         ; read in 8 gcr bytes
;
        lda  #$52       ;  header block id
        sta  stab        
;
seek10  jsr  sync       ;  find sync char
;
        bvc  *          ;  wait for block id
        clv      
;
        lda  data2       
        cmp  stab       ;  test if header block
        bne  seek20     ;  not header
;
seek15  bvc  *   
        clv     	;  read in gcr header
;
        lda  data2       
        sta  stab+1,x    
;
        inx      
        cpx  #7          
        bne  seek15      
;
        jsr  cnvbin     ;  convert header in stab to binary in header
;
        ldy  #4         ;  compute checksum
        lda  #0          
;
seek30  eor  header,y    
        dey      
        bpl  seek30      
;
        cmp  #0         ;  test if ok
        bne  cserr      ;  nope, checksum error in header
;
        ldx  cdrive     ;  update drive track#
        lda  header+2    
        sta  drvtrk,x    
;
        lda  job        ;  test if a seek job
        cmp  #$30        
        beq  eseek       
;
        lda  cdrive      
        asl  a          ;  test if correct id
        tay      
        lda  dskid,y     
        cmp  header      
        bne  badid       
        lda  dskid+1,y   
        cmp  header+1    
        bne  badid       
;
        jmp  wsect      ;  find best sector to service
;
;
seek20  dec  tmp        ;  search more?
        bne  seek10     ; yes
;
        lda  #2         ;  cant find a sector
        jsr  errr        
;
;
eseek   lda  header     ; harris fix....
        sta  dskid      ; ....
        lda  header+1   ; ....
        sta  dskid+1    ; ....
;
done    lda  #1         ;  return ok code
	.byte    skip2   
;
badid   lda  #11        ;  disk id mismatch
	.byte    skip2   
;
cserr   lda  #9         ;  checksum error in header
        jmp  errr        
;
;
;
wsect   lda  #$7f       ;  find best job
        sta  csect       
;
        lda  header+3   ;  get upcoming sector #
        clc      
        adc  #2          
        cmp  sectr       
        bcc  l460        
;
        sbc  sectr      ; wrap around
;
l460    sta  nexts      ;  next sector
;
        ldx  #numjob-1   
        stx  jobn        
;
        ldx  #$ff        
;
l480    jsr  setjb       
        bpl  l470        
;
        sta  work        
        and  #1          
        cmp  cdrive     ;  test if same drive
        bne  l470       ;  nope
;
        ldy  #0         ;  test if same track
        lda  (hdrpnt),y          
        cmp  tracc       
        bne  l470        
;
        lda  job        ;  test if execute job
        cmp  #execd      
        beq  l465        
;
        ldy  #1          
        sec      
        lda  (hdrpnt),y          
        sbc  nexts       
        bpl  l465        
;
        clc      
        adc  sectr       
;
l465    cmp  csect       
        bcs  l470        
;
        pha     	;  save it
        lda  job         
        beq  tstrdj     ;  must be a read
;
        pla      
        cmp  #wrtmin    ;  +if(csect<4)return;
        bcc  l470       ;  +if(csect>8)return;
;
        cmp  #wrtmax     
        bcs  l470        
;
doitt   sta  csect      ; its better
        lda  jobn        
        tax      
        adc  #>bufs      
        sta  bufpnt+1    
;
        bne  l470        
;
tstrdj  pla      
        cmp  #rdmax     ;  if(csect>6)return;
        bcc  doitt       
;
;
l470    dec  jobn        
        bpl  l480        
;
        txa     	;  test if a job to do
        bpl  l490        
;
        jmp  end        ;  no job found
;
l490    stx  jobn        
        jsr  setjb       
        lda  job         
        jmp  reed        
;
;
;
;
cnvbin  lda  bufpnt      
        pha      
        lda  bufpnt+1    
        pha     	;  save buffer pntr
;
        lda  #<stab      
        sta  bufpnt     ;  point at gcr code
        lda  #>stab      
        sta  bufpnt+1    
;
        lda  #0          
        sta  gcrpnt      
;
        jsr  get4gb     ;  convert 4 bytes
;
        lda  btab+3      
        sta  header+2    
;
        lda  btab+2      
        sta  header+3    
;
        lda  btab+1      
        sta  header+4    
;
;
        jsr  get4gb     ;  get 2 more
;
        lda  btab       ;  get id
        sta  header+1    
        lda  btab+1      
        sta  header      
;
        pla      
        sta  bufpnt+1   ; restore pointer
        pla      
        sta  bufpnt      
;
        rts      
;
;
;
;
;
;
;
;.end
        .page  
        .subttl 'lccutil1.src'

;  * utility routines

jerrr   ldy  jobn       ; return  job code
        sta  jobs,y      

        lda  gcrflg     ; test if buffer left gcr
        beq  1$		; no

        jsr  jwtobin    ; convert back to binary

1$      jsr  trnoff     ; start timeout on drive

        ldx  savsp       
        txs     	; reset stack pointer

        jmp  jtop       ; back to the top

;   motor and stepper control
;   irq into controller every 8ms

jend              
        lda  t1hl2	; set irq timer
	sta  t1hc2

	lda  dskcnt
        and  #$10	; wpsw 
        cmp  lwpt       ; same as last
        sta  lwpt       ; update 
        bne  1$

        lda  mtrcnt     ; anything to do?
        bne  7$		; dec & finish up

        beq  2$		; nothing to do

1$	lda  #$ff        
        sta  mtrcnt     ; 255*8ms motor on time
        jsr  moton       

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  ptch72	; *** rom ds 05/20/86 ***
	nop
;       lda  #1          
;       sta  wpsw        

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        bne  2$		; bra

7$      dec  mtrcnt     ; dec & return
        bne  2$
        lda  drvst       
        cmp  #$00       ; motor off & no active drive ?
        bne  2$		; br, do not turn it off something is going on

        jsr  motoff      

2$      lda  phase	; test for phase offset
        beq  5$

        cmp  #2          
        bne  3$

        lda  #0          
        sta  phase
        beq  5$		; bra

3$	sta  steps       
        lda  #2          
        sta  phase
        jmp  6$

5$	ldx  cdrive     ;  work on active drive only
        bmi  8$		;  no active drive

        lda  drvst      ;  test if motor on
        tay      
        cmp  #$20       ;  test if anything to do
        bne  9$		;  something here

8$	jmp  10$	;  motor just running

9$	dec  acltim     ;  dec timer
        bne  11$
	
        tya     	;  test if acel
        bpl  12$


        and  #$7f       ;  over, clear acel bit
        sta  drvst       

12$	and  #$10       ;  test if time out state
        beq  11$

	dec  acltim2	;  decrement second timer
	bne  11$	

	jsr  motoff      

        lda  #$ff       ;  no active drive now
        sta  cdrive      

        lda  #0         ;  drive inactive
        sta  drvst      ;  clear on bit and timout
        beq  8$	

11$	tya     	;  test if step needed
        and  #$40        
        bne  13$	;  stepping

        jmp  10$

13$	lda  nxtst      ; step or settle
        bne  18$	; go set

	lda  steps       
        beq  17$

6$      lda  steps       
        bpl  14$

	tya
	pha		; save regs .y
	ldy  #99	; wait for trk_00
15$	lda  pota1	; check for trk_00
	ror  a		; rotate into carry
	php		; save it
	lda  pota1	; debounce it
	ror  a		; => carry
	ror  a		; => bit 7
	plp		; restore carry
	and  #$80	; set/clear sign bit
	bcc  21$	

	bpl  16$	; carry set(off) & sign clear(on) exit

	bmi  20$	; bra

21$	bmi  16$	; carry clear(on) & sign set(off) exit
 
20$	dey 
	bne  15$	; wait a while 

	bcs  16$	; br, not track 00 ?

	lda  adrsed	; enable/disable track 00 sense
	bne  16$	; br, nope...

	lda  dskcnt	; phase 0
	and  #3
	bne  16$

	pla
	tay		; restore .y

	lda  #0
	sta  steps	; nomore steps
	jmp  10$

16$	pla
	tay		; restore .y

	inc  steps      ; keep stepping
        lda  dskcnt
        sec      
        sbc  #1        	; -1 to step out
        jmp  19$

17$	lda  #2         ;  settle time
        sta  acltim      
        sta  nxtst      ; show set status
        jmp  10$

18$	dec  acltim      
        bne  10$

        lda  drvst       
        and  #$ff-$40    
        sta  drvst       

        lda  #00         
        sta  nxtst       
        jmp  10$

14$	dec  steps       
        lda  dskcnt
        clc      
        adc  #01         

19$	and  #3          
        sta  tmp         
        lda  dskcnt
;<><><><><><><><><><><><><><><><><><><><><><><><><><><>
;       and  #$ff-$03   ; mask out old
;       ora  tmp         
;	sta  dskcnt
	jmp  ptch0a	; *** rom ds 11/7/86 ***, finish up
	nop		; fill

10$	jmp  ptch0b	; *** rom ds 11/7/86 ***, disable SO

;<><><><><><><><><><><><><><><><><><><><><><><><><><><>

	rts      
	.page 
	.subttl 'lccutil.src'         
;
;
;  * utility routines
;
;
errr    ldy  jobn       ;  return  job code
        sta  jobs,y      
;
        lda  gcrflg     ;  test if buffer left gcr
        beq  errr10     ;  no
;
        jsr  wtobin     ;  convert back to binary
;
errr10           
        jsr  trnoff     ;  start timeout on drive
;
        ldx  savsp       
        txs     	;  reset stack pointer
;
        jmp  top        ;  back to the top
;
;
;
turnon  lda  #$a0       ;  turn on drive
; drvst=acel and on
        sta  drvst       
;
;
        lda  dskcnt     ;  turn motor on and select drive
        ora  #$04       ;  turn motor on
        sta  dskcnt      
;
        lda  #50	;  delay  .4 sec *** rom ds 04/17/85 ***
        sta  acltim      
;
        rts      
;
;
;
trnoff  ldx  cdrive     ;  start time out of current drive
        lda  drvst      ; status=timeout
        ora  #$10        
        sta  drvst       
;
	jmp  ptch20	; setup timers for timeout *rom-05ds 01/22/85*
	nop		; fill
	nop		; fill

;       lda  #255       ;  255*.025s time out
;       sta  acltim      
;
;       rts      
;
;
;
;.end
        .page  
        .subttl 'lccwrt1.src'
 
;    write out data buffer

jwright cmp  #$10       ;  test if write
        beq  1$

        jmp  jvrfy        

1$	jsr  chkblk     ;  get block checksum
	sta  chksum

        lda  dskcnt	;  test for write protect
        and  #$10
        bne  2$		;  not  protected

        lda  #8         ;  write protect error
        jmp  jerrr        

2$	jsr  bingcr     ;  convert buffer to write image

        jsr  jsrch      ;  find header

        ldy  #gap1-2    ;  wait out header gap

3$	bit  pota1
	bmi  3$

	bit  byt_clr

        dey     	;  test if done yet
        bne  3$

        lda  #$ff       ;  make output $ff
        sta  ddra2       

        lda  pcr2	;  set write mode
        and  #$ff-$e0   ;  0=wr
	ora  #$c0
        sta  pcr2

        lda  #$ff       ;  write 4 gcr sync
        ldy  #numsyn    
        sta  data2       

4$	bit  pota1
	bmi  4$

	bit  byt_clr

        dey      
        bne  4$

; write out overflow buffer

        ldy  #256-topwrt  

5$	lda  ovrbuf,y   ; get a char
6$	bit  pota1
	bmi  6$

        sta  data2      ;  stuff it

        iny      
        bne  5$		;  do next char

			;  write rest of buffer

7$	lda  (bufpnt),y ;  now do buffer
8$	bit  pota1	;  wait until ready
	bmi  8$

        sta  data2      ;  stuff it again

        iny      	;  test if done
        bne  7$		;  do the whole thing

9$	bit  pota1	;  wait for last char to write out
	bmi  9$

        lda  pcr2	;  goto read mode
	ora  #$e0
        sta  pcr2

        lda  #0         ;  make data2 input $00
        sta  ddra2       

        jsr  jwtobin    ;  convert write image to binary

        ldy  jobn       ;  make job a verify

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch62	; *** rom ds 01/21/86 ***, chk for verify
;       lda  jobs,y      
        eor  #$30        
        sta  jobs,y      

        jmp  jseak      ;  scan job que

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>


;    * jwtobin
;    convert write image back to
;    binary data


jwtobin lda  #0
	sta  savpnt     
	sta  bufpnt     ;  lsb for overflow area
	sta  nxtpnt  

        lda  bufpnt+1   
        sta  nxtbf      ;  save for next buffer.

        lda  #>ovrbuf	;  overflow first
        sta  bufpnt+1   ;  msb for overflow area
        sta  savpnt+1

        lda  #256-topwrt 
        sta  gcrpnt     ;  offset
        sta  bytcnt     ;  ditto

        jsr  jget4gb    ;  get first four- id and 3 data

        lda  btab       ;  save bid
        sta  bid         

        ldy  bytcnt      

        lda  btab+1      
        sta  (savpnt),y
        iny      

        lda  btab+2      
        sta  (savpnt),y
        iny      

        lda  btab+3      
        sta  (savpnt),y
        iny      

        sty  bytcnt      

; do overflow first and store back into overflow buffer

1$	jsr  jget4gb    ; do rest of overflow buffer

        ldy  bytcnt      

        lda  btab        
        sta  (savpnt),y
        iny      

        lda  btab+1      
        sta  (savpnt),y
        iny      
        beq  2$

        lda  btab+2      
        sta  (savpnt),y
        iny      

        lda  btab+3      
        sta  (savpnt),y
        iny      

        sty  bytcnt      
        bne  1$		;  jmp till end of overflow buffer

2$      lda  btab+2      
        sta  (bufpnt),y          
        iny      

        lda  btab+3      
        sta  (bufpnt),y          
        iny      

        sty  bytcnt      

3$	jsr  jget4gb      

        ldy  bytcnt      

        lda  btab        
        sta  (bufpnt),y          
        iny      

        lda  btab+1      
        sta  (bufpnt),y          
        iny      

        lda  btab+2      
        sta  (bufpnt),y          
        iny      

        lda  btab+3      
        sta  (bufpnt),y          
        iny      

        sty  bytcnt      
        cpy  #187
        bcc  3$

	lda  #69		;  move buffer up
        sta  savpnt      

        lda  bufpnt+1    
        sta  savpnt+1    

        ldy  #256-topwrt-1

4$	lda  (bufpnt),y          
        sta  (savpnt),y          

        dey      
        bne  4$

        lda  (bufpnt),y          
        sta  (savpnt),y          

; load in overflow

        ldx  #256-topwrt

5$	lda  ovrbuf,x    
        sta  (bufpnt),y          

        iny      
        inx      
        bne  5$

        stx  gcrflg     ; clear buffer gcr flag
        rts      



;    * verify data block
;   convert to gcr verify image
;   test against data block
;   convert back to binary


jvrfy   cmp  #$20       ;  test if verify
        beq  1$

        bne  7$		; bra

1$      jsr  chkblk     ; get block checksum
	sta  chksum

        jsr  bingcr     ; convert to verify image

        jsr  jdstrt       

        ldy  #256-topwrt
2$	lda  ovrbuf,y   ;  get char
3$	bit  pota1
	bmi  3$

        eor  data2      ;  test if same
        bne  4$		; verify error

        iny      
        bne  2$		;  next byte


5$	lda  (bufpnt),y ;  now do buffer

6$	bit  pota1
	bmi  6$

        eor  data2      ;  test if same
        bne  4$		;  error

        iny      
        cpy  #$fd       ;  dont test off bytes
        bne  5$
	beq  8$		;  bra

7$	jsr  jsrch      ;  sector seek
8$	lda  #1
	.byte  skip2
4$	lda  #7         ;  verify error
        jmp  jerrr        
	.page 
	.subttl 'lccwrt.src'       
;
;
;
;    * write job
;
;    write out data buffer
;
;
wright  cmp  #$10       ;  test if write
        beq  wrt05       
;
        jmp  vrfy        
;
wrt05   jsr  chkblk     ;  get block checksum
        sta  chksum      
;
        lda  dskcnt     ;  test for write protect
        and  #$10        
        bne  wrt10      ;  not  protected
;
        lda  #8         ;  write protect error
        jmp  errr        
;
wrt10   jsr  bingcr     ;  convert buffer to write image
;
        jsr  srch       ;  find header
;
        ldx  #gap1-2    ;  wait out header gap
;
wrt20   bvc  *   
        clv      
;
        dex     	;  test if done yet
        bne  wrt20       
;
        lda  #$ff       ;  make output $ff
        sta  ddra2       
;
        lda  pcr2       ;  set write mode
        and  #$ff-$e0    
        ora  #$c0        
        sta  pcr2        
;
        lda  #$ff       ;  write 5 gcr sync
        ldx  #numsyn    ; 
        sta  data2       
        clv      
;
wrtsnc  bvc  *   
;
        clv      
        dex      
        bne  wrtsnc      
;
;  write out overflow buffer
;
        ldy  #256-topwrt        
;
wrt30   lda  ovrbuf,y   ;  get a char
        bvc  *          ;  wait until ready
        clv      
;
        sta  data2      ;  stuff it
        iny      
        bne  wrt30      ;  do next char
;
; write rest of buffer
;
wrt40   lda  (bufpnt),y ;  now do buffer
        bvc  *          ;  wait until ready
        clv      
;
        sta  data2      ;  stuff it again
        iny      
;
; test if done
;
        bne  wrt40      ;  do the whole thing
;
        bvc  *          ;  wait for last char to write out
;
;
        lda  pcr2       ;  goto read mode
        ora  #$e0        
        sta  pcr2        
;
        lda  #0         ;  make data2 input $00
        sta  ddra2       
;
        jsr  wtobin     ;  convert write image to binary
;
        ldy  jobn       ;  make job a verify
        lda  jobs,y      
        eor  #$30        
        sta  jobs,y      
;
        jmp  seak       ;  scan job que
;
;
chkblk  lda  #0         ;  get eor checksum
        tay      
;
chkb10  eor  (bufpnt),y          
        iny      
        bne  chkb10      
;
        rts     	;  return checksum in .a
;
;
;
;
;    * wtobin
;
;    convert write image back to
;    binary data
;
;
wtobin  lda  #0         ;  init pointer
        sta  savpnt      
        sta  bufpnt     ;  lsb
        sta  nxtpnt      
;
        lda  bufpnt+1   ; goto buffer next
        sta  nxtbf       
;
        lda  #>ovrbuf   ;  use overflow first
        sta  bufpnt+1    
        sta  savpnt+1    
;
        lda  #256-topwrt         
        sta  gcrpnt     ;  get here first
        sta  bytcnt     ;  store here
;
        jsr  get4gb     ;  get first four- id and 3 data
;
        lda  btab       ;  save bid
        sta  bid         
;
        ldy  bytcnt      
;
        lda  btab+1      
        sta  (savpnt),y          
        iny      
;
        lda  btab+2      
        sta  (savpnt),y          
        iny      
;
        lda  btab+3      
        sta  (savpnt),y          
        iny      
;
        sty  bytcnt      
;
wtob14  jsr  get4gb     ; do rest of overflow buffer
;
        ldy  bytcnt      
;
        lda  btab        
        sta  (savpnt),y          
        iny      
;
        lda  btab+1      
        sta  (savpnt),y          
        iny      
        beq  wtob50      
;
        lda  btab+2      
        sta  (savpnt),y          
        iny      
;
        lda  btab+3      
        sta  (savpnt),y          
        iny      
;
        sty  bytcnt      
        bne  wtob14     ;  jmp
;
wtob50           
;
        lda  btab+2      
        sta  (bufpnt),y          
        iny      
;
        lda  btab+3      
        sta  (bufpnt),y          
        iny      
;
        sty  bytcnt      
;
wtob53  jsr  get4gb      
;
        ldy  bytcnt      
;
        lda  btab        
        sta  (bufpnt),y          
        iny      
;
        lda  btab+1      
        sta  (bufpnt),y          
        iny      
;
        lda  btab+2      
        sta  (bufpnt),y          
        iny      
;
        lda  btab+3      
        sta  (bufpnt),y          
        iny      
;
        sty  bytcnt      
        cpy  #187        
        bcc  wtob53      
;
wtob52  lda  #69        ;  move buffer up
        sta  savpnt      
;
        lda  bufpnt+1    
        sta  savpnt+1    
;
        ldy  #256-topwrt-1       
;
wtob55  lda  (bufpnt),y          
        sta  (savpnt),y          
;
        dey      
        bne  wtob55      
;
        lda  (bufpnt),y          
        sta  (savpnt),y          
;
;  move overflow over to buffer
;
        ldx  #256-topwrt        
;
wtob57  lda  ovrbuf,x    
        sta  (bufpnt),y          
;
        iny      
        inx      
        bne  wtob57      
;
        stx  gcrflg     ;  clear buffer gcr flag
;
;
        rts      
;
;
;
;
;
;    * verify data block
;
;   convert to gcr verify image
;
;   test against data block
;
;   convert back to binary
;
;
vrfy    cmp  #$20       ;  test if verify
        beq  vrf10       
;
        jmp  sectsk      
;
vrf10            
;
        jsr  chkblk     ;  get block checksum
        sta  chksum      
;
        jsr  bingcr     ;  convert to verify image
;
        jsr  dstrt       
;
        ldy  #256-topwrt         
vrf15   lda  ovrbuf,y   ;  get char
        bvc  *   
        clv      
;
        eor  data2      ;  test if same
        bne  vrf20      ;  verify error
;
        iny      
        bne  vrf15      ;  next byte
;
;
vrf30   lda  (bufpnt),y ;  now do buffer
;
        bvc  *   
        clv     ;  wait for char
;
        eor  data2      ;  test if same
        bne  vrf20      ;  error
;
        iny      
        cpy  #$fd       ;  dont test off bytes
        bne  vrf30       
;
;
        jmp  done       ;  verify ok
;
vrf20   lda  #7         ;  verify error
        jmp  errr        
;
;
sectsk  jsr  srch       ;  sector seek
        jmp  done        
;
;
;.end
	.page   
	.subttl 'leds.src'

;turn on activity led specified
; by drvnum

setlds  sei      
        lda  #$ff-led1-led0      
        and  ledprt      
        pha      

        lda  drvnum      
        beq  leds0       
        pla      
        ora  #led1       
        bne  leds1       
leds0            
        pla      
        ora  #led0       
leds1            
        sta  ledprt      
        cli      
        rts      

ledson  sei      
        lda  #led1+led0          
        ora  ledprt      
        sta  ledprt      
        cli      
        rts      

erroff           
        lda  #0          
        sta  erword      
        sta  erled       
        rts      

erron   sei      
        txa      
        pha     	; save .x
        lda  #80         
        sta  erword      
        ldx  #0          
        lda  ledmsk,x    
        sta  erled       
        ora  ledprt     ; set led on
        sta  ledprt      
        pla      
        tax     	; restore .x
        cli      
        rts      
	.page 
	.subttl 'lookup.src'
;  optsch  optimal search for lookup
;  and fndfil
	
optsch  lda  #0         ; determine optimal search
        sta  temp       ; init drive mask
        sta  drvflg      
        pha      
        ldx  f2cnt       
os10    pla      
        ora  temp        
        pha      
        lda  #1          
        sta  temp        
        dex      
        bmi  os30        
        lda  fildrv,x    
        bpl  os15        
        asl  temp        
        asl  temp        

os15    lsr  a   
        bcc  os10        
        asl  temp        
        bne  os10       ; (branch)

os30    pla      
        tax      
        lda  schtbl-1,x          
        pha      
        and  #3          
        sta  drvcnt      
        pla      
        asl  a   
        bpl  os40        
        lda  fildrv      
os35    and  #1          
        sta  drvnum      

        lda  drvcnt      
        beq  os60       ; only one drive addressed

        jsr  autoi      ; check drive for autoinit
        beq  os50       ; drive is active

        jsr  togdrv      
        lda  #0         ; set 1 drive addressed
        sta  drvcnt      
        jsr  autoi      ; check drive for autoinit
        beq  os70       ; drive is active
os45             
        lda  #nodriv     
        jsr  cmderr      
os50             
        jsr  togdrv      
        jsr  autoi      ; check drive for autoinit
        php      
        jsr  togdrv      
        plp      
        beq  os70       ; drive is active

        lda  #0         ; set 1 drive addressed
        sta  drvcnt      
        beq  os70       ; bra
os60             
        jsr  autoi      ; check drive for autoinit
        bne  os45       ; drive is not active
os70             
        jmp  setlds      

os40    rol  a   
        jmp  os35        

schtbl  .byte    0,$80,$41        
	.byte    1,1,1,1         
	.byte    $81,$81,$81,$81         
	.byte    $42,$42,$42,$42         
        .page  
;   look up all files in stream
;   and fill tables w/ info

lookup  jsr  optsch      
lk05    lda  #0          
        sta  delind      
        jsr  srchst     ; start search
        bne  lk25        
lk10    dec  drvcnt      
        bpl  lk15        
        rts     	; no more drive searches

lk15    lda  #1         ; toggle drive #
        sta  drvflg      
        jsr  togdrv      
        jsr  setlds     ;  turn on led
        jmp  lk05        
lk20    jsr  search     ; find valid fn
        beq  lk30       ; end of search
lk25    jsr  compar     ; compare dir w/ table
        lda  found      ; found flag
        beq  lk26       ; all fn's not found, yet
        rts      
	
lk26    lda  entfnd      
        bmi  lk20        
        bpl  lk25        

lk30    lda  found       
        beq  lk10        
        rts      

;  find next file name matching
;  any file in stream & return
;  with entry found stuffed into
;  tables

ffre    jsr  srre       ; find file re-entry
        beq  ff10        
        bne  ff25        

ff15    lda  #1          
        sta  drvflg      
        jsr  togdrv      
        jsr  setlds      

ffst    lda  #0         ; find file start entry
        sta  delind      
        jsr  srchst      
        bne  ff25        
        sta  found       
ff10    lda  found       
        bne  ff40        
        dec  drvcnt      
        bpl  ff15        
        rts      

fndfil  jsr  search     ; find file continuous...
        beq  ff10       ; ... re-entry, no channel activity
ff25    jsr  compar     ; compare file names
        ldx  entfnd      
        bpl  ff30        
        lda  found       
        beq  fndfil      
        bne  ff40        
	
ff30    lda  typflg      
        beq  ff40       ; no type restriction
        lda  pattyp,x    
        and  #typmsk     
        cmp  typflg      
        bne  fndfil      
ff40    rts      
        .page  

;  compare all filenames in stream table
;  with each valid entry in the 
;  directory.  matches are tabulated

compar  ldx  #$ff        
        stx  entfnd      
        inx      
        stx  patflg      
        jsr  cmpchk      
        beq  cp10        
cp02    rts     	; all are found

cp05    jsr  cc10        
        bne  cp02        
cp10    lda  drvnum      
        eor  fildrv,x    
        lsr  a   
        bcc  cp20       ; right drive
        and  #$40        
        beq  cp05       ; no default
        lda  #2          
        cmp  drvcnt      
        beq  cp05       ; don't use default

cp20    lda  filtbl,x   ; good drive match
        tax      
        jsr  fndlmt      
        ldy  #3          
        jmp  cp33        
cp30             
        lda  cmdbuf,x    
        cmp  (dirbuf),y          
        beq  cp32       ; chars are =

        cmp  #'?         
        bne  cp05       ; no single pattern
        lda  (dirbuf),y          
        cmp  #$a0        
        beq  cp05       ; end of filename
cp32             
        inx      
        iny      
cp33             
        cpx  limit       
        bcs  cp34       ; end of pattern

        lda  cmdbuf,x    
        cmp  #'*         
        beq  cp40       ; star matches all
        bne  cp30       ; keep checking
cp34             
        cpy  #19         
        bcs  cp40       ; end of filename

        lda  (dirbuf),y          
        cmp  #$a0        
        bne  cp05        

cp40    ldx  f2ptr      ; filenames match
        stx  entfnd      
        lda  pattyp,x   ; store info in tables
        and  #$80        
        sta  patflg      
        lda  index       
        sta  entind,x    
        lda  sector      
        sta  entsec,x    
        ldy  #0          
        lda  (dirbuf),y          
        iny      
        pha      
        and  #$40        
        sta  temp        
        pla      
        and  #$ff-$20    
        bmi  cp42        

        ora  #$20        
cp42             
        and  #$27        
        ora  temp        
        sta  temp        
        lda  #$80        
        and  pattyp,x    
        ora  temp        
        sta  pattyp,x    
        lda  fildrv,x    
        and  #$80        
        ora  drvnum      
        sta  fildrv,x    

        lda  (dirbuf),y          
        sta  filtrk,x    
        iny      
        lda  (dirbuf),y          
        sta  filsec,x    
        lda  rec         
        bne  cp50        
        ldy  #21         
        lda  (dirbuf),y          
        sta  rec         

;check table for unfound files

cp50             
cmpchk  lda  #$ff        
        sta  found       
        lda  f2cnt       
        sta  f2ptr       

cc10    dec  f2ptr       
        bpl  cc15        
        rts     	; table exhausted

cc15    ldx  f2ptr       
        lda  pattyp,x    
        bmi  cc20        
        lda  filtrk,x    
        bne  cc10        
cc20    lda  #0          
        sta  found       
        rts      
        .page  

;  search directory 
;  returns with valid entry w/ delind=0
;  or returns w/ 1st deleted entry
;  w/ delind=1 
;
;  srchst will initiate a search
;  search will continue a search

srchst           
        ldy  #0         ; init deleted sector
        sty  delsec      
        dey      
        sty  entfnd      

        lda  dirtrk     ; start search at beginning
        sta  track       
        lda  #1          
        sta  sector      
        sta  lstbuf      
        jsr  opnird     ; open internal read chnl
	
sr10    lda  lstbuf     ; last buffer if 0
        bne  sr15        
        rts     	; (z=1)

sr15    lda  #7          
        sta  filcnt      
        lda  #0         ; read track #
        jsr  drdbyt      
        sta  lstbuf     ; update end flag

sr20    jsr  getpnt      
        dec  filcnt      
        ldy  #0          
        lda  (dirbuf),y ; read file type
        bne  sr30        

        lda  delsec     ; deleted entry found
        bne  search     ; deleted entry already found
        jsr  curblk     ; get current sector
        lda  sector      
        sta  delsec      

        lda  dirbuf     ; get current index
        ldx  delind     ; bit1: want deleted entry
        sta  delind      
        beq  search     ; need valid entry
        rts     	; (z=0)

sr30    ldx  #1          
        cpx  delind     ; ?looking for deleted?
        bne  sr50       ;  no!
        beq  search      

srre    lda  dirtrk      
        sta  track       
        lda  dirsec      
        sta  sector      
        jsr  opnird      
        lda  index       
        jsr  setpnt      

search  lda  #$ff        
        sta  entfnd      
        lda  filcnt     ; adjust file count
        bmi  sr40        
        lda  #32        ; incr by 32
        jsr  incptr      
        jmp  sr20        

sr40    jsr  nxtbuf     ; new buffer
        jmp  sr10       ; (branch)

sr50    lda  dirbuf     ; found valid entry
        sta  index      ; save index
        jsr  curblk     ; get sector
        lda  sector      
        sta  dirsec      

        rts     	; (z=0)
autoi            
;  check drive for active diskette
;  init if needed
;  return nodrv status

        lda  autofg      
        bne  auto2      ; auto-init is disabled

        ldx  drvnum      
        lsr  wpsw,x     ; test & clear wpsw
        bcc  auto2      ; no change in diskette

        lda  #$ff        
        sta  jobrtn     ; set error return code
        jsr  itrial     ; init-seek test
        ldy  #$ff       ;  .y= true
        cmp  #2          
        beq  auto1      ; no sync= no diskette

        cmp  #3          
        beq  auto1      ; no header= no directory

        cmp  #$f         
        beq  auto1      ; no drive!!!!

        ldy  #0         ; set .y false
auto1            
        ldx  drvnum      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch44	; *** rom ds 05/01/85 ***
;       tya      
;       sta  nodrv,x    ; set condn of no-drive
rtch44			; ret address

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        bne  auto2      ; no need to init crud!

        jsr  initdr     ; init that drive
auto2            
        ldx  drvnum      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch50	; *** rom ds 05/01/85 ***
;       lda  nodrv,x    ; return no-drive condn
;       rts      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>
        .page  
        .subttl 'lstdir.src'

; start the directory loading function
; get the buffer and get it started

stdir   lda  #0          
        sta  sa          
        lda  #1         ; allocate chanl and 1 bufefer
        jsr  getrch      

        lda  #0          
        jsr  setpnt      

        ldx  lindx       
        lda  #0          
        sta  lstchr,x    
        jsr  getact      
        tax      
        lda  drvnum      
        sta  lstjob,x    
        lda  #1         ;  put sal in buffer
        jsr  putbyt      
        lda  #4         ; put sah in buffer
        jsr  putbyt      
        lda  #1         ; insert fhoney links (0101)
        jsr  putbyt      
        jsr  putbyt      
        lda  nbtemp      
        jsr  putbyt     ; put in drvnum
        lda  #0          
        jsr  putbyt      
        jsr  movbuf     ; get disk name
        jsr  getact      
        asl  a   
        tax      
        dec  buftab,x    
        dec  buftab,x    
        lda  #0         ; end of this line
        jsr  putbyt      
dir1    lda  #1         ; insert fhoney links ($0101)
        jsr  putbyt      
        jsr  putbyt      
        jsr  getnam     ; get #bufrs and file name
        bcc  dir3       ; test if last entry
        lda  nbtemp      
        jsr  putbyt      
        lda  nbtemp+1    
        jsr  putbyt      
        jsr  movbuf      
        lda  #0         ; end of entry
        jsr  putbyt      
        bne  dir1        
dir10   jsr  getact      
        asl  a   
        tax      
        lda  #0          
        sta  buftab,x    
        lda  #rdytlk     
        ldy  lindx       
        sta  dirlst      
        sta  chnrdy,y   ;  directory list buffer full
        lda  data        
        rts      

dir3    lda  nbtemp     ;  this is end of load
        jsr  putbyt      
        lda  nbtemp+1    
        jsr  putbyt      
        jsr  movbuf      
        jsr  getact      
        asl  a   
        tax      
        dec  buftab,x    
        dec  buftab,x    
        lda  #0         ;  end of listing (000)
        jsr  putbyt      
        jsr  putbyt      
        jsr  putbyt      
        jsr  getact      
        asl  a   
        tay      
        lda  buftab,y    
        ldx  lindx       
        sta  lstchr,x    
        dec  lstchr,x    
        jmp  dir10       

; transfer file name to listing buffer

movbuf  ldy  #0          
movb1   lda  nambuf,y    
        jsr  putbyt      
        iny      
        cpy  #27         
        bne  movb1       
        rts      

; get char for directory loading

getdir  jsr  getbyt      
        beq  getd3       
        rts      
getd3   sta  data        
        ldy  lindx       
        lda  lstchr,y    
        beq  gd1         
        lda  #eoiout     
        sta  chnrdy,y    
        lda  data        
        rts      
gd1              
        pha      
        jsr  dir1        
        pla      
        rts      
        .page  
        .subttl 'map.src'
; build a new map on diskette

newmap           
newmpv           
        jsr  clnbam      
        ldy  #0          
        lda  #18        ; set link to 18.1
        sta  (bmpnt),y   
        iny      
        tya      
        sta  (bmpnt),y   
        iny      
        iny      
        iny     	; .y=4
nm10             
        lda  #0         ; clear track map
        sta  t0          
        sta  t1          
        sta  t2          

        tya      	; 4=>1
        lsr  a   
        lsr  a          ; .a=track #
        jsr  maxsec     ; store blks free byte away
        sta  (bmpnt),y   
        iny      
        tax      
nm20             
        sec     	; set map bits
        rol  t0         ;      t0          t1          t2
        rol  t1         ;   76543210  111111         xxx21111
        rol  t2         ;             54321098          09876
        dex      	;   11111111  11111111          11111
        bne  nm20        
nm30            	; .x=0
        lda  t0,x        
        sta  (bmpnt),y  ; write out bit map
        iny      
        inx      
        cpx  #3          
        bcc  nm30        
        cpy  #$90       ; end of bam, 4-143
        bcc  nm10        

        jmp  nfcalc     ; calc # free sectors

; write out the bit map to
; the drive in lstjob(active)

mapout  jsr  getact      
        tax      
        lda  lstjob,x    
mo10    and  #1          
        sta  drvnum     ; check bam before writing

; write bam according to drvnum

scrbam           
        ldy  drvnum      
        lda  mdirty,y    
        bne  sb10        
        rts     	; not dirty
sb10             
        lda  #0         ; set to clean bam
        sta  mdirty,y    
        jsr  setbpt     ; set bit map ptr
        lda  drvnum      
        asl  a   
        pha      
;put memory images to bam
        jsr  putbam      
        pla      
        clc      
        adc  #1          
        jsr  putbam      
; verify the bam block count
; matches the bits

        lda  track       
        pha     	; save track var
        lda  #1          
        sta  track       
sb20             
        asl  a   
        asl  a   
        sta  bmpnt       

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  ptch39	; *** rom ds 02/25/85 ***
;       jsr  avck       ; check available blocks

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        inc  track       
        lda  track       
        cmp  maxtrk      
        bcc  sb20        
        pla     	; restore track var
        sta  track       

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  wrt_bam
;       jmp  dowrit     ; write it out

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><>
	.page

; set bit map ptr, read in bam if nec.

setbpt           
        jsr  bam2a       
        tax      
        jsr  redbam     ; read bam if not in
        ldx  jobnum      
        lda  bufind,x   ; set the ptr
        sta  bmpnt+1     
        lda  #0          
        sta  bmpnt       
        rts      

; calc the number of free blocks on drvnum

numfre           
        ldx  drvnum      
        lda  ndbl,x      
        sta  nbtemp      
        lda  ndbh,x      
        sta  nbtemp+1    
        rts      
 .lib dos
 .lib equate
 .lib i/odef
 .lib ramvar
 .lib romtbl
 .lib dskint
 .lib idle
 .lib ieee
 .lib tstfnd
 .lib erproc
 .lib frets
 .lib lstdir
 .lib parsex
 .lib setdrv
 .lib autoit
 .lib lookup
 .lib trnsfr
 .lib new
 .lib scrtch
 .lib duplct
 .lib copysetup
 .lib copyall
 .lib rename
 .lib verdir
 .lib echksm
 .lib memrw
 .lib block
 .lib fndrel
 .lib tst2
 .lib tst3
 .lib tst4
 .lib jobs
 .lib addfil
 .lib open
 .lib close
 .lib opchnl
 .lib tstflg
 .lib tsutil
 .lib ssutil
 .lib sstest
 .lib getact
 .lib rel1
 .lib rel2
 .lib rel3
 .lib rel4
 .lib ssend
 .lib record
 .lib nulbuf
 .lib addrel
 .lib newss
 .lib vector
 .lib chksum
 .page ''
 .end
        .page 
        .subttl 'memrw.src'

;  memory access commands
;  "-" must be 2nd char
mem     lda  cmdbuf+1    
        cmp  #'-         
        bne  memerr      

        lda  cmdbuf+3   ; set address in temp
        sta  temp        
        lda  cmdbuf+4    
        sta  temp+1      

        ldy  #0          
        lda  cmdbuf+2    
        cmp  #'R
        beq  memrd      ; read
        jsr  killp      ; kill protect
        cmp  #'W
        beq  memwrt     ; write
        cmp  #'E
        bne  memerr     ; error

; execute

memex   jmp  (temp)      

memrd            
        lda  (temp),y    
        sta  data        
        lda  cmdsiz      
        cmp  #6          
        bcc  m30         

        ldx  cmdbuf+5    
        dex      
        beq  m30         
        txa      
        clc      
        adc  temp        
        inc  temp        
        sta  lstchr+errchn       
        lda  temp        
        sta  cb+2        
        lda  temp+1      
        sta  cb+3        
        jmp  ge20        
m30              
        jsr  fndrch      
        jmp  ge15        

memerr  lda  #badcmd    ; bad command
        jmp  cmderr      

memwrt          	; write
m10     lda  cmdbuf+6,y          
        sta  (temp),y   ; transfer from cmdbuf
        iny      
        cpy  cmdbuf+5   ; # of bytes to write
        bcc  m10         
        rts      
        .page  
	.subttl 'mfmcntrl.src'

;bit   7   -           test track
;bit   6   -           test sector
;bit   5   -           test write protect switch
;bit   4   -           motor on
;bit   3   -           seek to track
;bit   2   -           wait for motor up to speed
;bit   1   -           logical seek
;bit   0   -           error return

cmdinf   .byte  $00,$15,$00,$00,$00,$15,$00,$bc          
         .byte  $34,$de,$fe,$dc,$15,$15,$00     

cmdjmp   .word    cmdzer         ; reset
         .word    cmdone         ; bump
         .word    cmdtwo         ; wp status
         .word    cmdthr         ; setup for step
         .word    cmdfor         ; reserved
         .word    cmdfve         ; read address
         .word    seke		 ; seek physical
         .word    cmdsev         ; format track
         .word    cmdeig         ; format disk
         .word    cmdnin         ; read sector
         .word    cmdten         ; write sector
         .word    cmdele         ; verify
         .word    cmdtwv         ; verify format
         .word    cmdthi         ; create sector table
         .word    diskin	 ; is the disk home ?

cmdis    =  *-cmdjmp     
        .page    
prcmd   sei      
	pha		; save cmd
        tax      
        lda  cmdinf,x   ; get information
        sta  info        
	lda  dkmode
	ora  #$80	; mfm mode
	sta  dkmode

        asl  info       ; check track
        bcc  prc00       

        lda  cmdbuf+3
        sta  cmd_trk	; save track
prc00   asl  info       ; check sector
        bcc  prc01       

        lda  cmdbuf+4
        sta  sectr      ; save sector
prc01   asl  info       ; check wp for write jobs
        bcc  prc02       

        lda  dskcnt
        and  #$10
        bne  prc02       

	lda  switch	; set kill job switch
	ora  #%00001000
	sta  switch	; wp error
        ldx  #8          
	stx  ctl_dat

prc02   asl  info       ; motor on ?
        bcc  prc03       

        jsr  stmtr      ; start motor
prc03   asl  info       ; seek ?
        bcc  prc04      ; seek physical while motor is spinning

        jsr  seke       ; seek job
prc04   asl  info       ; wait for motor up to speed ?
        bcc  prc05       

        jsr  wtmtr      ; go and wait

; disk up to speed

prc05   jsr  sel_sid	; select 
	asl  info	; logical seek required ?
	bcc  prc06

	jsr  s_log	; seek by reading

; disk up to speed and on track

prc06   lda  #0          
        pla		; get jobs
        asl  a   
        tax      
        lda  cmdjmp,x   ; do indirect
        sta  temp        
        lda  cmdjmp+1,x          
        sta  temp+1      

	jsr  do_the_command

	jsr  trnoff	; motor off...

	ldx  mfmcmd
	cpx  #2
	php

	asl  info
	bcs  prc07
 
  	plp
	bcc  prc08

	jmp  exbad

prc07	plp		; restore status

prc08	rts

do_the_command	jmp  (temp)

	.page


;******************************************************

moton   php		; no irq's
	sei
	lda  dskcnt
	ora  #4
	sta  dskcnt
	plp		; retrieve status
	rts

;******************************************************

motoff  php		; no irq's
	sei
	lda  dskcnt
	and  #$ff-$04
	sta  dskcnt
	plp		; retrieve status
	rts

;******************************************************

led_on	php		; no irq's
	sei		
	lda  dskcnt	; turn on led
	ora  #8
	sta  dskcnt
	plp		; retrieve status
	rts

;******************************************************

led_off	php		; no irq's
	sei
	lda  dskcnt	; turn led off
	and  #$ff-$08
	sta  dskcnt
	plp		; retrieve status
	rts

;******************************************************

stmtr   php      
        sei      
        lda  drvst       
	bmi  2$		; accelerating ?

	and  #bit5+bit4
	beq  1$

        lda  #$20       ; disk up to speed
        sta  drvst
2$	plp
	rts

1$	plp
	sta  cdrive	; set drive
        jmp  turnon	; turn on motor
	nop
	nop
	nop
	nop
	nop
	nop

;******************************************************

wtmtr   php             ; wait for motor up to speed
        cli      
wtmtr1  lda  drvst       
        cmp  #$20       ; drv ready ?
        bne  wtmtr1      

        plp      
        rts      

;******************************************************

seke    php  		; wait for not busy
        cli             ; enable irqs for motor on timing
        lda  cmd_trk
        asl  a          ; half steps
        cmp  cur_trk	; correct track ?
        beq  sekrtn     ; br, yep

seklop  lda  cmd_trk	; where
        asl  a   
        cmp  cur_trk
        beq  sekdun      

        bcs  sekup      ; hi or lo ?

sekdn   jsr  stout      ; step out
        jmp  seklop     ; next

sekup   jsr  stin       ; step in
        jmp  seklop     ; next also

sekdun  ldy  #$10	; settle
        jsr  xms        ; delay

sekrtn  plp      
        rts      

;******************************************************

stin    lda  cur_trk	; get current valuestep
	clc
	adc  #1
        jmp  stpfin     ; half step out

stout   ldy  #99	; wait for trk_00
1$	lda  pota1	; check for trk_00
	ror  a		; rotate into carry
	php		; save carry
	lda  pota1	; debounce it
	ror  a		; carry <=
	ror  a		; bit 7 <=
	plp		; retrieve carry
	and  #$80	; set/clear sign bit
	bcc  3$		; on track zero

	bpl  2$		; carry set(off) & sign(on) exit

	bmi  4$		; bra

3$	bmi  2$		; carry clear(on) & sign(off) exit

4$	dey	
	bne  1$		; continue

	bcs  2$		; br, not track 00 ?

	lda  dskcnt	; phase 0
	and  #3
	bne  2$

	lda  #0		; trk zero
	sta  cur_trk	; current trk = 40*2/0
	rts

2$	lda  cur_trk	; get current half step
        sec
	sbc #1

stpfin  sta  cur_trk	; save
        and  #3         ; strip unused
        sta  temp        
	php		; no irq's
	sei
        lda  dskcnt
        and  #$fc       ; strip step bits
        ora  temp        
        sta  dskcnt
	plp		; retrieve status
        ldy  #$06	; 6 ms step time, 16 ms settle
        
xms     jsr  onems      ; delay 1 ms
        dey      
        bne  xms         

        rts      


;******************************************************

onems   ldx  #2
        lda  #$6f	; *** rom ds 12/27/85 ***, beta9 1 mS.
onems1  adc  #1          
        bne  onems1      

        dex      
        bne  onems1      

        rts      


;******************************************************


cvstat  WDTEST		; chk address
	lda  wdstat     ; retrieve status
        lsr  a   
        lsr  a   
        lsr  a   
        and  #3         ; check status
        tax      
        lda  mfmer,x     
	sta  mfmcmd
	tax
        rts      

;******************************************************

strtwd  pha		; save accum
	jsr  led_on	; act led on
	pla
	WDTEST		; chk address
	sta  wdstat	; send cmd to wd 1770
	lda  #1		; wait for busy
	WDTEST		; chk address
1$	bit  wdstat
	beq  1$

	jmp  jslower	; WD1770 bug ????


;******************************************************

waitdn  jsr  led_off	; act led off
	lda  #1		; wait for unbusy
	WDTEST		; chk address
1$	bit  wdstat
	bne  1$

	rts

	.page	

;******************************************************

sectnx  lda  minsek	
	sec
	sbc  #1
	sta  ctl_dat	; save min sector - 1
	lda  cmdbuf+4	; get original sector
	clc
	adc  cpmit	; next sector
	cmp  maxsek
	beq  2$		; equal to or

	bcc  2$		;          less than

	sbc  maxsek	; rap around
	clc	
	adc  ctl_dat	; add min now
2$	sta  cmdbuf+4	; next sector for controller
	rts

	.page

;******************************************************

sectcv	ldy  #0
	ldx  #0
	lda  cmdbuf+3   ; start at sector x
	and  #$3f	; delete mode bit & table bit
	sta  cmdbuf+3
	sta  minsek	; new min
	pha		; save ss
	lda  cmdbuf+7	; save ns
	pha
	inc  cmdbuf+4	; interleave + 1

sectop	lda  cmdbuf+3	; ss
	sta  cmdbuf+11,y ; table goes to cmdbuf+42
	inc  cmdbuf+3	; increment ss
	inx
	tya		; do addition
	clc
	adc  cmdbuf+4	; add interleave
	tay
	cpy  #32	; no more room
	bcs  bd_int

	cpy  cmdbuf+7	; ns
	bcc  secles	; < than

	bne  seclmo	; can't be equal until the end

	cpx  cmdbuf+7	; ns end ?
	beq  seclmo

bd_int  dec  cmdbuf+4	; restore
	pla
	sta  cmdbuf+7	; restore ns
	pla
	sta  cmdbuf+3	; restore ss

	sec
	rts		; bad exit

seclmo  tya
	sec
	sbc  cmdbuf+7	; ns
	tay

secles	cpx  cmdbuf+7	; ns
	bne  sectop

	stx  cpmsek	; ns
	dex
	txa
	clc
	adc  minsek	; find max
	sta  maxsek	; new max
	cmp  minsek	; better be less than max
	bcc  bd_int	; bad starting sector

	pla
	sta  cmdbuf+7	; restore ns
	pla
	sta  cmdbuf+3	; restore ss
	dec  cmdbuf+4	; restore

	clc		; ok exit
	rts        


;******************************************************


verfmt	lda  mfmcmd
	pha		; save current logical track

	ldy  #0
	sty  mfmhdr	; init index into sector table
ver_fm	ldy  mfmhdr	; get current index
	lda  cmdbuf+11,y
	sta  wdsec	; setup sector for wd 1770
	jsr  cmdtwv	; verify
	ldx  mfmcmd	; get error
	cpx  #2
	bcs  ver_00

	inc  mfmhdr	; next entry in sector table
	ldy  mfmhdr
	cpy  cmdbuf+7	; check with ns
	bne  ver_fm	; done ?

	clc
	.byte  skip1	
ver_00	sec
	pla
	sta  mfmcmd	; restore track address
no_trk	rts


;******************************************************

chcsee	lda  cmdsiz
	cmp  #7		; next track parameter given ?
	bcc  no_trk

	lda  cmdbuf+6	; get nt
	sta  cmd_trk
	jmp  seke


;******************************************************


s_log	lda  mfmcmd	; save command
	pha
	jsr  cmdfiv	; where are we?
	ldx  mfmcmd	; status ok?
	cpx  #2
	bcc  1$		; see if we are there...

	jsr  cmdone	; restore to trk_00
	jsr  cmdfiv	; where are we.. better be track 00
	ldx  mfmcmd	; status ok?
	cpx  #2
	bcs  2$		; on error leave on track 00

1$	lda  cmd_trk	; we there yet ?
	asl  a
	cmp  cur_trk	; are we on ?
	beq  2$

	jsr  seke	; find it...should be there now...hope?

2$	pla
	sta  mfmcmd	; restore command	
	rts


;******************************************************


sel_sid php		; no irq's
	sei
	lda  switch
	and  #%00010000 ; check side
	cmp  #%00010000	; set/clr carry
	jsr  set_side	; set h/w
	plp		; retrieve status
	rts

;******************************************************

maxmin	ldy  cpmsek	; get ns
	dey		; one less
	lda  #255	; as small as min can get
minim	cmp  cmdbuf+11,y
	bcc  no_min	; br, no change

	lda  cmdbuf+11,y
no_min	dey
	bpl  minim

	sta  minsek	; save min

	ldy  cpmsek	
	dey		; one less
	lda  #0		; as small as max can get
maxim	cmp  cmdbuf+11,y
	bcs  no_max	; br, no change

	lda  cmdbuf+11,y
no_max	dey
	bpl  maxim

	sta  maxsek	; save max
	rts

;******************************************************

fn_int	ldx  cpmsek	; location of min
	ldy  #0		; start in beginning
fn_000	lda  cmdbuf+11,y	
	cmp  minsek
	beq  fn_001	; min ?

	iny
	cpy  cpmsek
	bne  fn_000	

fn_001  sty  ctl_cmd	; save index
	lda  minsek	; get min
	clc	
	adc  #1		; add one to it
	sta  ctl_dat	; save min + 1

	ldx  #255	; interleave at zero
fnlook	lda  cmdbuf+11,y
	cmp  ctl_dat	; find min + 1 ?
	beq  fn_002

	inx		; cpmit = cpmit + 1
	iny	
	cpy  cpmsek	; index here ?
	bne  fnlook

	ldy  #0		; rap around
	beq  fnlook 	; bra

fn_002	rts		; return with hard interleave in .x


;******************************************************


diskin  lda  temp
	pha		; save temp
	php		; save status
	sei		; no irq's
	lda  wdtrk	; where are we ?
	sta  wddat	; here we are..
	lda  #$18
	jsr  strtwd	; seek cmd
	jsr  waitdn	; fin ?
	ldx  #0
	ldy  #128	; wait a little more than a revolution
	WDTEST		; chk address
	lda  wdstat	; get start status
	and  #2
	sta  temp	; save current status
	WDTEST		; chk address
1$	lda  wdstat	; get current status
	and  #2
	cmp  temp	; same go on
	beq  2$

	plp		; retrieve status	
	jmp  4$		; finish up ok disk inserted

2$	dex	
	bne  1$

	dey
	bne  1$		; timout ?

	plp		; retrieve status
	sec		; exit no index sensor toggling
	.byte skip1
4$	clc		; ok
	pla
	sta  temp	; restore temp
	rts
        .page  
	.subttl 'mfmsubr1.src'

; format track

cmdsev  lda  #$f8	; write track
	jsr  strtwd	; send command

	bit  switch	; system 34 / iso standard switch
	bvc  no_ind	; write index ?

	ldx  #80
	WDTEST		; chk address
2$      lda  wdstat
	and  #3
	lsr  a
	bcc  v6
	beq  2$

	lda  #$4e
	sta  wddat	; give him the data
	dex
	bne  2$

	ldx  #12
	WDTEST		; chk address
3$	lda  wdstat
	and  #3
	lsr  a
	bcc  v6
	beq  3$

	lda  #0
	sta  wddat
	dex
	bne  3$

	ldx  #3
	WDTEST		; chk address
4$	lda  wdstat
	and  #3
	lsr  a
	bcc  v6
	beq  4$

	lda  #$f6
	sta  wddat
	dex
	bne  4$

	WDTEST		; chk address
5$	lda  wdstat
	and  #3
	lsr  a
	bcc  v6
	beq  5$

	lda  #$fc
	sta  wddat


	ldx  #50
	WDTEST		; chk address
6$      lda  wdstat
	and  #3
	lsr  a
	bcc  v6
	beq  6$

	lda  #$4e
	sta  wddat	; give him the data
	dex
	bne  6$

	beq  inner	; bra... done...


no_ind	ldx  #60
	WDTEST		; chk address
cmd7    lda  wdstat
	and  #3
	lsr  a
v6	bcc  v1	
	beq  cmd7

	lda  #$4e
	sta  wddat	; give him the data
	dex
	bne  cmd7


inner	ldy  #1  	; ss

main7	ldx  #12
	WDTEST		; chk address
cmd70   lda  wdstat
	and  #3
	lsr  a
	bcc  v1	
	beq  cmd70

	lda  #0
	sta  wddat
	dex
	bne  cmd70

	ldx  #3
	WDTEST		; chk address
cmd71   lda  wdstat
	and  #3
	lsr  a
v1	bcc  v2	
	beq  cmd71

	lda  #$f5
	sta  wddat
	dex
	bne  cmd71


	WDTEST		; chk address
cmd7n   lda  wdstat
	and  #3
	lsr  a
	bcc  v2	
	beq  cmd7n

	lda  #$fe   	; id address mark
	sta  wddat


	WDTEST		; chk address
cmd7f   lda  wdstat
	and  #3
	lsr  a
	bcc  v2	
	beq  cmd7f

	lda  mfmcmd	; give him the track
	sta  wddat

	WDTEST		; chk address
cmd7e   lda  wdstat
	and  #3
	lsr  a
	bcc  v2	
	beq  cmd7e
	
	lda  switch
	and  #%00010000 ; what side are we on ?
	bne  1$

	lda  #0
	.byte skip2
1$	lda  #1
	sta  wddat	; side number is ...
	
	WDTEST		; chk address
cmd7d   lda  wdstat
	and  #3
	lsr  a
	bcc  v2	
	beq  cmd7d
	
	lda  cmdbuf+10,y ; sector number actually cmdbuf+11
	sta  wddat

	WDTEST		; chk address
cmd7c   lda  wdstat
	and  #3
	lsr  a
v2	bcc  v3
	beq  cmd7c

	lda  cmdbuf+5	; sz
	sta  wddat
	
	WDTEST		; chk address
cmd7b   lda  wdstat
	and  #3
	lsr  a
	bcc  v3
	beq  cmd7b

	lda  #$f7	; crc 2 bytes written
	sta  wddat

	ldx  #22

	WDTEST		; chk address
cmd72   lda  wdstat
	and  #3
	lsr  a
	bcc  v3
	beq  cmd72


	lda  #$4e
	sta  wddat
	dex
	bne  cmd72

	ldx  #12

	WDTEST		; chk address
cmd73   lda  wdstat
	and  #3
	lsr  a
v3	bcc  v4
	beq  cmd73



	lda  #0
	sta  wddat
	dex
	bne  cmd73

	ldx  #3
	WDTEST		; chk address
cmd74   lda  wdstat
	and  #3
	lsr  a
	bcc  v4
	beq  cmd74


	lda  #$f5	; a1
	sta  wddat
	dex
	bne  cmd74

	WDTEST		; chk address
cmd7a   lda  wdstat
	and  #3
	lsr  a
	bcc  v4
	beq  cmd7a

	lda  #$fb	; dam
	sta  wddat
	
	sty  temp	; save current sector


	ldy  mfmsiz_hi	; high
	WDTEST		; chk address
cmd750  lda  wdstat
	and  #3
	lsr  a
v4	bcc  v5
	beq  cmd750


	lda  cmdbuf+10  ; fl
	sta  wddat
	
	cpx  mfmsiz_lo
	beq  cmd75x

	inx		; increment
	jmp  cmd750
	
cmd75x	inx
	dey
	bne  cmd750


	WDTEST		; chk address
cmd7ff  lda  wdstat
	and  #3
	lsr  a
	bcc  v5
	beq  cmd7ff

	lda  #$f7	; crc
	sta  wddat
	
	ldy  cmdbuf+5	; ss
	lda  gapmfm,y
	ldy  temp	; sector restore
	tax

	WDTEST		; chk address
cmd7fe  lda  wdstat
	and  #3
	lsr  a
	bcc  v5
	beq  cmd7fe

	lda  #$4e	; gap 3
	sta  wddat
	dex
	bne  cmd7fe

	cpy  cmdbuf+7	; ns
	beq  finmfm

	iny		; inc sector
	jmp  main7


	WDTEST		; chk address
finmfm  lda  wdstat
	and  #3
	lsr  a
	bcc  vfin
	beq  finmfm

	clc
	lda  #$4e	; wait for wd to time out
	sta  wddat
	jmp  finmfm

vfin    jsr  waitdn	; wait for sleepy time
	clc		; good carry
        .byte skip1
v5     	sec
	rts

	; min gap size @2% fast
gapmfm 	.byte 7, 12, 23, 44 
	
numsek  .byte 26, 16, 9, 5

	.page
; format disk

;  0     1    2    3      4    5    6    7    8   9   10  + cmdbuf
;  ^     ^    ^    ^      ^    ^    ^    ^    ^   ^    ^
; "U" + $00 + N +  MD  + CP + SZ + LT + NS + ST + S + FL 

cmdeig  lda  switch	; check abort command switch
	and  #%00001000
	beq  1$

	ldx  ctl_dat	; get error
	stx  mfmcmd	; save for prcmd
	sec		; wp error usually
	rts

1$	jsr  clrchn	; close all channels
	lda  cmdsiz	; setup default parms
	sec
	sbc  #4		; less mandatory + 1
	tay
	beq  cp00	; mode only, gave cp
	
	dey
	beq  sz00	; md, cp only, gave sz

	lda  #0
	sta  mfmcmd	; clear status

	lda  cmdbuf+5
	jsr  consek	; setup sector size
	
	dey
	beq  lt00	; md, cp, sz only, gave lt

	dey
	beq  ns00	; md, cp, sz, lt only, gave ns
	
	dey
 	beq  st00       ; md, cp, sz, lt, ns only, gave st

	dey
	beq  s00        ; md, cp, sz, lt, ns, st only, gave s

	dey
	beq  fl00	; md, cp, sz, lt, ns, st, ss only, gave fl

	jmp  start8

	.page
cp00	lda  #0		; default interleave
	sta  cmdbuf+4	

sz00	lda  #0
	sta  mfmcmd	; clear status

	lda  #1		; 256 byte sectors
	sta  cmdbuf+5
	jsr  consek	; setup block size

lt00	lda  #39	; last track is #39, 40 tracks total
	sta  cmdbuf+6

ns00	lda  numsek,x	; x=sector size index for # of sectors per track
	sta  cmdbuf+7

st00	lda  #0		; default track #0 start
	sta  cmdbuf+8
	sta  wdtrk

s00	lda  #0		; default steps from track 00
	sta  cmdbuf+9

fl00	lda  #$e5	; default block fill
	sta  cmdbuf+10

	.page
start8  jsr  go_fmt	; format side zero
	lda  mfmcmd	; error ?
	cpx  #2
	bcs  1$

	lda  switch	; check for double sided opt
	and  #%00100000
	beq  1$

	lda  switch	; set single side
	ora  #%00010000
	sta  switch
	jsr  sel_sid	; select h/w
	jsr  go_fmt	; format side one
1$	jmp  cmdone	; restore

	.page
go_fmt	jsr  diskin	; is there a diskette in the unit
	bcs  c_801	; br, nope...

	lda  #1	
	sta  ifr1	; clear irq from write protect
	jsr  cmdone     ; restore to track one/zero ?
	lda  cmdbuf+8   ; store logical
	sta  mfmcmd	; in mfmcmd
	sta  wdtrk

	bit  cmdbuf+3	; check table bit
	bvs  1$		; br, table has been given to us

	jsr  sectcv	; generate sector table
	bcs  c_801

1$	lda  cmdbuf+9	; offset track 00 by s steps
	and  #$7f	; clear mode bit
	beq  c_800

	clc
	adc  cmd_trk	; add to it	
	sta  cmd_trk	; physical
	jsr  seke	; & seek to it

c_800	sei
	lda  ifr1	; check for disk change
	lsr  a
	bcs  c_801

	jsr  cmdsev     ; format track
        bcs  c_801      

	lda  ifr1	; check for disk change
	lsr  a
	bcs  c_801

	jsr  verfmt	; verify format 
        bcs  c_801      

	lda  ifr1	; check for disk change
	lsr  a
	bcs  c_801

        lda  mfmcmd	; last track ?
        cmp  cmdbuf+6   ; lt
        beq  c_802      

        inc  cmd_trk
	inc  wdtrk
	inc  mfmcmd	; track to write

        jsr  seke       ; goto next track
        jmp  c_800

c_802   bit  switch
	bpl c_803	; kill next

	sec
	lda  cmdbuf+6	; lt
	sbc  cmdbuf+8	; st
	cmp  #39	; no more than 40 tracks
	bcs  c_803
	
	inc  cmd_trk	; clear next track
	jsr  seke
	ldx  #28

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  ptch0d	; *** rom ds 01-13-87 ***,no SO
	nop
	nop
	nop		; fill
;	jsr  jclear	; with gcr 0
;	jsr  kill	; read mode again

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

c_803	ldx  #0
	.byte  skip2
c_801   ldx  #6
	stx  mfmcmd	; ok exit
	jmp  upinst
        .page  
	.subttl 'mfmsubr2.src'

;  mfm read

cmdnin  lda  switch
	and  #%00100000
	bne  c_904

	lda  #3
	sta  bufpnt+1	; buffer #1
	ldy  #0         ; even page
	sty  bufpnt
	ldx  mfmsiz_hi	; get sector size
        lda  cmdbuf+3   ; get track address
        sta  wdtrk      ; give track to wd
        lda  cmdbuf+4   ; get sector address
        sta  wdsec      ; give sector to wd
	lda  #$88       ; read sector command
	jsr  strtwd	; send cmd

	WDTEST		; chk address
c_900	lda  wdstat	; get a byte
	and  #3
	lsr  a
	bcc  c_902

	and  #1
	beq  c_900

        lda  wddat      ; get a byte from wd
        sta  (bufpnt),y ; put it in the buffer
        cpy  mfmsiz_lo	; done buffer
        beq  c_901	; next

        iny      
        bne  c_900

c_901	iny		; y=0
	dex      
        beq  c_902

        inc  bufpnt+1   ; next buffer
        jmp  c_900

c_902	jsr  waitdn	; wait for unbusy	

	jsr  cvstat	; convert
	jsr  upinst     ; update controller status

        bit  switch
	bvs  c_905	; ignore error ?

	cpx  #2          
	bcc  c_905

	jmp  finbad	; send it 

c_905	jsr  hskrd	; send status

	lda  switch
	bmi  c_906	; buffer transfer ?

c_904	lda  #3
	sta  bufpnt+1	; buffer #1
	ldy  #0         ; even page
	sty  bufpnt
	ldx  mfmsiz_hi	; get sector size
c_907	lda  (bufpnt),y ; get data from buffer
	sta  ctl_dat	
	jsr  hskrd	; handshake data to host

        cpy  mfmsiz_lo	; done buffer ?
        beq  c_908	; next

        iny      
        bne  c_907

c_908	iny		; y=0
	dex      
        beq  c_906

        inc  bufpnt+1   ; next buffer
        jmp  c_907

c_906	dec  cmdbuf+5	; any more sectors ?
	beq  cmd9ex

	jsr  sectnx     ; next sector rap ?
	jmp  cmdnin	; continue...

cmd9ex  jmp  chcsee     ; next track

	.page
; mfm write

cmdten  lda  #3
	sta  bufpnt+1	; buffer #1
	ldy  #0
	sty  bufpnt
	ldx  mfmsiz_hi	; get high byte 
	lda  switch
	bmi  c_100	; buffer transfer ?

c_101	lda  pb		; debounce
	eor  #clkout	; toggle clock
	bit  icr	; clear pending
	sta  pb		; doit

c_103	lda  pb	
	bpl  c_102	; atn ?

	jsr  tstatn	; service if appropiate

c_102	lda  icr
	and  #8
	beq  c_103	; wait for byte ready

	lda  sdr	; get data
	sta  (bufpnt),y
	cpy  mfmsiz_lo
	beq  c_104

	iny		
	bne  c_101

c_104	iny		; y=0
	dex
	beq  c_100
	
	inc  bufpnt+1   ; next buffer
	jmp  c_101

c_100	lda  switch
	and  #%00100000
	bne  c_105	; buffer op

	lda  switch
	and  #%00001000 ; check internal switch
	beq  c_112	

	ldx  ctl_dat
	jmp  fail

c_112	lda  #3         ; write now
	sta  bufpnt+1	; buffer #1
	ldy  #0
	sty  bufpnt
	ldx  mfmsiz_hi
	lda  cmdbuf+3	; get track
	sta  wdtrk
	lda  cmdbuf+4
	sta  wdsec
	lda  ifr1	; check for diskette change
	lsr  a
	bcs  c_110

	lda  #$a8	; normal dam
	jsr  strtwd

	WDTEST		; chk address
cmd10	lda  wdstat
	and  #3
	lsr  a
	bcc  c_110

	and  #1
	beq  cmd10

	lda  (bufpnt),y
	sta  wddat	; send data to wd1770
	cpy  mfmsiz_lo
	beq  cmd100

	iny
	bne  cmd10

cmd100	iny		; y=0
	dex
	beq  c_108
	
	inc  bufpnt+1	; next page...
	jmp  cmd10

c_108   lda  ifr1	; check for diskette change
	lsr  a
	bcs  c_110

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  ptch60	; *** rom ds 01/21/86 ***, chk for verify
;	jsr  cmdele	; verify

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	bcc  c_109

c_110	jsr  spout	; go output
	ldx  #7
	bne  c_111	; bra

c_109	jsr  spout
	jsr  cvstat
c_111	stx  mfmcmd	; save status
	jsr  upinst
	jsr  hskrd	; send it
	jsr  burst	; wait for clk high
	jsr  spinp	; input

	bit  switch	; abort on error ?
	bvs  c_105

	cpx  #2		; error ?
	bcs  c_107

c_105	dec  cmdbuf+5	; more sectors
	beq  c_106

	jsr  sectnx	; next sector
	jmp  cmdten

c_106	jmp  chcsee

c_107	rts
        .page  
	.subttl 'mfmsubr3.src'

; mfm verify for write operation

cmdele  lda  #3
	sta  bufpnt+1	; buffer #0
	ldy  #0         ; even page
	sty  bufpnt
        ldx  mfmsiz_hi	; get sector high
        lda  cmdbuf+3   ; get track address
        sta  wdtrk      ; give track to wd
        lda  cmdbuf+4   ; get sector address
        sta  wdsec      ; give sector to wd
cmd_11	lda  #$88       ; read sector command
	jsr  strtwd	; send cmd

	WDTEST		; chk address
c_1100	lda  wdstat     ; get a byte
	and  #3
	lsr  a
	bcc  c_1101

	and  #1
	beq  c_1100	; byte ready

        lda  wddat      ; get a byte from wd
        cmp  (bufpnt),y ; put it in the buffer
        bne  c_1101	; flag error

	cpy  mfmsiz_lo	; done buffer
        beq  c_1102	; next

        iny      
        bne  c_1100

c_1102  iny		; y=0
	dex      
c_1103	beq  c_1104

        inc  bufpnt+1   ; next buffer
        jmp  c_1100

c_1101	lda  #$d0	; abort wd1770
	WDTEST		; chk address
	sta  wdstat	; send cmd
	
	jsr  jslowd	; wait 40 uS.

	ldx  #7
	.byte skip2
c_1104  ldx  #0
	stx  mfmcmd
	jmp  waitdn	; wait for the wd1770 to sleep
	
	.page
; verify sector to fill byte
; track and sector must be setup previous

cmdtwv  lda  #3
	sta  bufpnt+1	; buffer #1
	ldy  #0         ; even page
	sty  bufpnt
        ldx  mfmsiz_hi	; get sector high
cmd_12	lda  #$88       ; read sector command
	jsr  strtwd	; send cmd

	WDTEST		; chk address
c_1200	lda  wdstat     ; get a byte
	and  #3
	lsr  a
	bcc  c_1201

	and  #1
	beq  c_1200	; byte ready

        lda  wddat      ; get a byte from wd
	cmp  cmdbuf+10  ; same as fill byte ?
	bne  c_1201	; br, on error

	cpy  mfmsiz_lo	; get lo sector size
	beq  c_1202

	iny  
	bne  c_1200	; done ?

c_1202	iny
	dex      
c_1203	beq  c_1204

        inc  bufpnt+1   ; next buffer
        jmp  c_1200

c_1201	lda  #$d0	; abort wd1770
	WDTEST		; chk address
	sta  wdstat	; send cmd
	
	jsr  jslowd	; wait 40 uS.

	ldx  #7
	.byte skip2
c_1204  ldx  #0
	stx  mfmcmd
	jmp  waitdn	; wait for the wd1770 to sleep

	.page
cmdthi  php
	sei
	jsr  cmdone	; track zero start

	bit  switch	; seek to n-track ?
	bpl  1$

	lda  cmdbuf+3
	sta  cmd_trk	; goto this track
	jsr  seke

1$	lda  #0
	sta  cpmsek	; clear # of sectors

	jsr  cmdfiv	; read address
	ldx  mfmcmd	; check status
	cpx  #2
	bcs  2$

	lda  mfmhdr+2
	sta  cmd_sec	; this is where we stop

3$	jsr  cmdfiv	; read address
	lda  mfmhdr+2	; get next sector
	ldy  cpmsek
	sta  cmdbuf+11,y 
	inc  cpmsek	; inc sector count
	cpy  #31	; went too far ?
	bcs  2$

	cmp  cmd_sec	; done yet ?
	bne  3$		; wait for rap...

	lda  mfmhdr	; get track
	sta  cmd_trk	; save for later

	ldx #0
	.byte skip2
2$	ldx #2
	stx  mfmcmd
	plp
	rts
        .page  
	.subttl 'mfmsubr.src'

cmdzer  jmp  dskint     ; reset

	.page
cmdone  lda  #180	
	sta  cur_trk

	lda  #0		; side zero
	sta  wdtrk	; init to trk zero
	sta  cmd_trk	; find zero
        jmp  seke

	.page
cmdtwo  lda  dskcnt	; return status of wp
        and  #$10
        rts      

	.page
cmdthr  sty  cmd_trk
	stx  cur_trk
	rts             

	.page
cmdfor  rts             ; reserved

	.page
;read address routine

;  0       1      2           3         4    5 + mfmhdr
;  ^       ^      ^           ^         ^    ^
; track  side#  sector  sector_length  crc  crc

; track 	=> cur-trk
; sector size 	=> mfmsiz_hi
; status  	=> mfmsiz_lo
; sector size 	=> dkmode

cmdfve	jsr  cmdone	; restore
	jsr  diskin	; is there a disk in the drive ?
	bcs  1$

	jsr  cmdfiv	; seek
	lda  nsectk,x	; get # of sectors
	sta  cpmsek	; set def.
	sta  maxsek	; store max sector number....def, ok
	lda  #1
	sta  minsek	; let query decide otherwise....def, ok
	rts

1$	lda  #$0d	; no disk
	sta  mfmcmd	; error
	bne  abrsk	; abort

cmdfiv  lda  #0          
        sta  mfmsiz_lo  ; clear sector size
        sta  mfmsiz_hi  ; *
        lda  #$c8       ; read address
	jsr  strtwd	; start cmd

; store address away

        ldx  #0          
        ldy  #6          
	WDTEST		; chk address
mseek1  lda  wdstat
	and  #3
	lsr  a
        bcc  mrcnf      ; no address mark found
	beq  mseek1

        lda  wddat      ; get data
        sta  mfmhdr,x   ; data in .a
        inx      
        dey      
        bne  mseek1      

mrcnf	jsr  waitdn	; wait for not busy

; wd 1770 status returns

; motor wp spin/rec recnofnd crc lstdat  datreq busy
;   s7  s6    s5       s4     s3   s2      s1    s0

        jsr  cvstat     ; get status

mseek3  lda  mfmhdr     ; read track address
	asl  a		; * 2
        sta  cur_trk

        lda  mfmhdr+3   ; get sector size
consek  and  #3         ; clear remaining
        tax      
	lda  sectlo,x    
        sta  mfmsiz_lo  ; sector size low
        lda  secthi,x    
        sta  mfmsiz_hi  ; sector size high

abrsk   lda  dkmode     ; set sector size in
        and  #%10000000 ; clear all but mode bit
	ora  mfmcmd	; set status
	ora  seckzz,x	; shifted sector size
        sta  dkmode      
	rts      

; sector size low for mfm 128,256,512,1024 byte sectors
sectlo   .byte  127,255,255,255          

; sector size high for mfm 128,256,512,1024 byte sectors
secthi   .byte  1,1,2,4          

; shifted sector size for mfm 128,256,512,1024 byte sectors
seckzz   .byte  $00,$10,$20,$30

; max sector #'s for mfm 128, 256, 512, 1024 byte sectors
nsectk	 .byte  26, 16, 9, 5

; ok, checksum-error, sector-not-found, no-address-mark
mfmer    .byte  1,9,2,3 

        .page
        .subttl 'new.src'

;  new: initialize a disk, disk is 
;  soft-sectored, bit avail. map,
;  directory, & 1st block are all inited

new     jsr  onedrv      
        lda  fildrv     ; set up drive #
        bpl  n101        

        lda  #badfn     ; bad drive # given
        jmp  cmderr      

n101    and  #1          
        sta  drvnum      

;	jsr  setlds
        jsr  ptch11     ; clr nodrv ***rom ds 01/21/85***

        lda  drvnum      
        asl  a   
        tax      
        ldy  filtbl+1   ; get disk id
        cpy  cmdsiz     ; ?is this new or clear?
        beq  n108       ; end of cmd string
        lda  cmdbuf,y   ; format disk****
        sta  dskid,x    ; store in proper drive
        lda  cmdbuf+1,y ; (y=0)
        sta  dskid+1,x   

        jsr  clrchn     ; clear all channels when formatting
        lda  #1         ; ...in track, track=1
        sta  track       

;--------- patch7 for format bug 10/17/83---
;       jsr  format     ; transfer format to ram
        jsr  patch7     ; set format flag
;-------------------------------------------

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>        

	jmp  ptch27	; *** rom ds 03/15/85 ***
;	jsr  clrbam     ; zero bam

rtch27

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>        

	jmp  n110        

n108    jsr  initdr     ; clear directory only

        ldx  drvnum      
        lda  dskver,x   ; use current version #
        cmp  vernum      
        beq  n110        
        jmp  vnerr      ; wrong version #

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

n110   	jsr  ptch32	; *** rom ds 02/21/85 ***
;       jsr  newmap     ; new bam

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        lda  jobnum      
        tay      
        asl  a   
        tax      
        lda  dsknam     ; set ptr to disk name
        sta  buftab,x    
        ldx  filtbl      
        lda  #27         
        jsr  trname     ; transfer cmd buf to bam

        ldy  #$12        
        ldx  drvnum      
        lda  vernum     ; set dos's current format type
        sta  dskver,x    
        txa      
        asl  a   
        tax      
        lda  dskid,x    ; write directory's  i.d.
        sta  (dirbuf),y          
        iny      
        lda  dskid+1,x   
        sta  (dirbuf),y          

        iny      
        iny      
        lda  #dosver+$30 ; write directory dos version
        sta  (dirbuf),y          
        iny      
        lda  vernum     ; write directory format type
        sta  (dirbuf),y          

        ldy  #2          
        sta  (bmpnt),y  ; write diskette's format type
        lda  dirtrk      
        sta  track       
        jsr  usedts     ; set bam block used
        lda  #1          
        sta  sector      
        jsr  usedts     ; set 1st dir block used
        jsr  scrbam     ; scrub the bam
        jsr  clrbam     ; set to all 0's
        ldy  #1          
        lda  #$ff       ; set end link
        sta  (bmpnt),y   
        jsr  drtwrt     ; clear directory
        dec  sector      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  initdr	; *** rom ds 02/27/85 ***
;       jsr  drtrd      ; read bam back

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        jmp  endcmd      
	.page
	.subttl 'newss.src'
;*********************************
;* newss: generate new ss & fix  *
;*        old ss's to reflect it.*
;*   vars:                       *
;*   regs:                       *
;*                               *
;*********************************

newss            
        jsr  nxtts      ; get t&s based on hdr
        jsr  dblbuf     ; use inactive buffer
        jsr  scrub       
        jsr  getact      
        pha      
        jsr  clrbuf      
        ldx  lindx       
        lda  ss,x       ; set regs for transfer
        tay      
        pla      
        tax      
        lda  #ssioff    ; # of chars
        jsr  b0tob0     ; transfer at buf(0)

        lda  #0          
        jsr  ssdir       
        ldy  #2          
        lda  (dirbuf),y ; get ss #
        pha      
        lda  #0          
        jsr  setpnt      
        pla      
        clc      
        adc  #1          
        sta  (dirbuf),y ; put ss # in new ss
        asl  a   
        adc  #4          
        sta  r3         ; save position
        tay      
        sec      
        sbc  #2          
        sta  r4          
        lda  track       
        sta  r1         ; save for ss update
        sta  (dirbuf),y ; put track in ss
        iny      
        lda  sector      
        sta  r2         ; save for ss update
        sta  (dirbuf),y ; put sector in ss
        ldy  #0          
        tya      
        sta  (dirbuf),y ; null link
        iny      
        lda  #ssioff+1  ; ptr to last byte
        sta  (dirbuf),y          

        lda  #ssioff     
        jsr  setpnt      
        jsr  wrtab       
        jsr  watjob      

ns20             
        ldx  lindx       
        lda  ss,x       ; get ss buffer #
        pha      
        jsr  gaflgs      
        ldx  lindx       
        sta  ss,x       ; swap act-buf & ss
        pla      
        ldx  lbused      
        sta  buf0,x      

        lda  #0          
        jsr  setpnt     ; set link to new ss
        ldy  #0          
        lda  track       
        sta  (dirbuf),y          
        iny      
        lda  sector      
        sta  (dirbuf),y          
        jmp  ns50        
ns40             
        jsr  getact      
        ldx  lindx       
        jsr  ibrd       ; read next ss
        lda  #0          
        jsr  setpnt     ; ptr=0
ns50             
        dec  r4          
        dec  r4          
        ldy  r3         ; get new ss link ptr
        lda  r1          
        sta  (dirbuf),y ; put track in
        iny      
        lda  r2          
        sta  (dirbuf),y ; put sector in

        jsr  wrtout     ; write it back...
        jsr  watjob     ; ...& wait
        ldy  r4          
        cpy  #3          
        bcs  ns40       ; more ss to update!

        jmp  dblbuf     ; reset active buffer
        .page 
	.subttl 'newvec.src'

	*=$BF00
	; vector table

	jmp  irq
	jmp  jirq
	jmp  lcc
	jmp  jlcc
	jmp  setjb
	jmp  jsetjb
	jmp  errr
	jmp  jerrr
	jmp  kill
	jmp  conhdr
	jmp  sync
	jmp  jsync
	jmp  gcrbin
	jmp  jgcrbin
	jmp  chkblk
	jmp  get4gb
	jmp  jget4gb
	jmp  jslowd
	jmp  slowd
	jmp  jslower
	jmp  clrbam
	jmp  clnbam
	jmp  parsxq
	jmp  parse
	jmp  cmdset
	jmp  cmdrst
	jmp  prcmd
	jmp  moton
	jmp  motoff
	lda  cmdtbb
	jmp  dblbuf
	jmp  open
	jmp  close
	jmp  error
	jmp  fstload
	jmp  signature
	jmp  cntint
	jmp  end
	jmp  jend
	lda  cjumpl

	*=*+30		; more room for vector jumps
	; info tables

	.word cmdjmp

	.page
	.subttl 'notes.src'
;
;11/6/86 David G. Siracusa 
;S.O enable was not disabled when exiting the 1571 controller.
;I don't know how the drive could ever work, miracles will
;never cease.   A symptom of this bug was made apparent with relative
;files would corrupt the disk.  NOTE: This would explain any quirk, occasional
;burp, or demonic possesion you thought the drive had.
;
;11/6/86 David G. Siracusa
;BAM swap bug.  When all buffers are allocated by the application, the
;DOS frees up the BAM buffer by marking it out of memory.  When it was
;reread it would also reread BAM for side one.  If BAM for side one is
;'dirty', it would be corrupted.  This fix uses hex 1B6 for a swap flag
;this is the ultimate in kludges!!!  The BAM for side one is rebuilt on 
;the reread of BAM.
;
;11/6/86 David G. Siracusa
;Save@ is fixed.  Variable nodrv is now a 16 bit addressable var, stlbuf
;routine steals the buffer locked by drive one.
;
;11/7/86 David G. Siracusa
;The applications which addressed tracks > 35 now have the density set
;properly, worktable and trackn replace worktble and trknum respectfully.
;
;12/2/86 David G. Siracusa
;Some applications fell prey to the 1541a ROM change which changed equate
;'TIM' to 20h, it is now 3ah like -05 1541 ROM.
;
;12/08/86 David G. Siracusa
;Inquire failed because I moved the GCR density tables.  I neglected
;to set the HDR var in INQUIRE.  The byte before the table was an RTS 
;($60) which was the perfect density for an uninitialized HDR var.  I 
;set HDR to 1 so the proper density will be set.
;
;01/13/87 David G. Siracusa
;Partial MFM Format did not work because of the above SO problem, byte
;ready's were disabled to the 65c22a.
;
        .page
        .subttl 'nulbuf.src'          
;*********************************
;* nulbuf: set null records in   *
;*         act-buf for extention *
;*    vars:  nr,rs,lx,act-buf    *
;*      in: nr= last record      *
;*          position in previous *
;*          buffer.              *
;*     out: nr= last record      *
;*          position in buffer   *
;*          for next nulbuf or   *
;*          to set lstchr.       *
;*********************************

nulbuf           
        jsr  set00      ; set indirect ptr
        ldy  #2          
        lda  #0          
nb10             
        sta  (dirbuf),y ; clear buffer
        iny      
        bne  nb10        

        jsr  addnr      ; advance nr
nb20             
        sta  nr,x        
        tay      
        lda  #$ff        
        sta  (dirbuf),y ; init record w/ cr
        jsr  addnr       
        bcc  nb20       ; not done

        bne  nb30        
        lda  #0          
        sta  nr,x        
nb30             
        rts      

; add rs & nr, leave in accum
; c=1: cross buffer boundary

addnr            
        ldx  lindx       
        lda  nr,x        
        sec      
        beq  an05        

        clc      
        adc  rs,x        
        bcc  an10        
        bne  an05        
        lda  #2          
        bit  er00        
        rts      

an05             
        adc  #1         ; adjust for link
        sec      
an10             
        rts      
        .page  
        .subttl 'open.src'

;  open channel from ieee
;  parses the input string that is 
;  sent as an open data channel,
;  load, or save.  channels are allocated
;  and the directory is searched for
;  the filename contained in the string.

open             
        lda  sa          
        sta  tempsa      
        jsr  cmdset     ; initiate cmd ptrs
        stx  cmdnum      
        ldx  cmdbuf      
        lda  tempsa      
        bne  op021       
        cpx  #'*        ; load last?
        bne  op021       
        lda  prgtrk      
        beq  op0415     ; no last prog, init 0

op02            	; load last program
        sta  track       
        lda  prgdrv      
        sta  drvnum      
        sta  fildrv      
        lda  #prgtyp     
        sta  pattyp      
        lda  prgsec      
        sta  sector      
        jsr  setlds     ;  make sure led gets turned on!!
        jsr  opnrch      
        lda  #prgtyp+prgtyp      
        ora  drvnum      
endrd   ldx  lindx       
        sta  filtyp,y    
        jmp  endcmd      

op021   cpx  #'$         
        bne  op041       
        lda  tempsa     ; load directory
        bne  op04        
        jmp  loadir      

op04    jsr  simprs     ; open dir as seq file
        lda  dirtrk      
        sta  track       
        lda  #0          
        sta  sector      
        jsr  opnrch      
        lda  drvnum      
        ora  #seqtyp+seqtyp      
        jmp  endrd       

op041   cpx  #'#        ; "#" is direct access 
        bne  op042       
        jmp  opnblk      

op0415  lda  #prgtyp    ; program type
        sta  typflg      
        lda  #0          
        sta  drvnum      
        sta  lstdrv      
        jsr  initdr      

op042            
        jsr  prscln     ; look for ":"
        bne  op049       
        ldx  #0          
        beq  op20       ; bra
op049            
        txa      
        beq  op10        
op05    lda  #badsyn    ; something amiss
        jmp  cmderr      
op10    dey     	; back up to ":"
        beq  op20       ; 1st char is ":"
        dey      
op20    sty  filtbl     ; save filename ptr
        lda  #$8d       ; look for cr-shifted
        jsr  parse       

        inx      
        stx  f2cnt       
        jsr  onedrv      
        jsr  optsch      
        jsr  ffst       ; look for file entry
        ldx  #0          
        stx  rec         
        stx  mode       ; read mode
        stx  type       ; deleted
        inx      
        cpx  f1cnt       
        bcs  op40       ; no parameters

        jsr  cktm       ; check for type & mode
        inx      
        cpx  f1cnt       
        bcs  op40       ; only one parameter

        cpy  #reltyp     
        beq  op60       ; set record size

        jsr  cktm       ; set type/mode
op40             
        ldx  tempsa      
        stx  sa         ; set sa back
        cpx  #2          
        bcs  op45       ; not load or save

        stx  mode       ; mode=sa
        lda  #$40        
        sta  wbam        
        lda  type        
        bne  op50       ; type from parm

        lda  #prgtyp     
        sta  type       ; use prg
op45             
        lda  type        
        bne  op50       ; type from parm

        lda  pattyp      
        and  #typmsk     
        sta  type       ; type from file

        lda  filtrk      
        bne  op50       ; yes, it exists

        lda  #seqtyp     
        sta  type       ; default is seq
op50             
        lda  mode        
        cmp  #wtmode     
        beq  op75       ; go write

        jmp  op90        

op60             
        ldy  filtbl,x   ; get record size
        lda  cmdbuf,y    
        sta  rec         
        lda  filtrk      
        bne  op40       ; it's here, read

        lda  #wtmode    ; use write to open
        sta  mode        
        bne  op40       ; (bra)

op75             
        lda  pattyp      
        and  #$80        
        tax      
        bne  op81        
op77    lda  #$20       ; open write
        bit  pattyp      
        beq  op80        
        jsr  deldir     ; created
        jmp  opwrit      

op80    lda  filtrk      
        bne  op81        
        jmp  opwrit     ; not found, ok!
op81    lda  cmdbuf      
        cmp  #'@        ; check for replace
        beq  op82        
        txa      
        bne  op815       
        lda  #flexst     
        jmp  cmderr      
op815            
        lda  #badfn      
        jmp  cmderr      

;********* check for bug here******
op82             
        lda  pattyp     ; replace
        and  #typmsk     
        cmp  type        
        bne  op115       
        cmp  #reltyp     
        beq  op115       

        jsr  opnwch      
        lda  lindx       
        sta  wlindx      
        lda  #irsa      ; internal chan
        sta  sa          
        jsr  fndrch      
        lda  index       
        jsr  setpnt      
        ldy  #0          
        lda  (dirbuf),y          
        ora  #$20       ; set replace bit
        sta  (dirbuf),y          

        ldy  #26         
        lda  track       
        sta  (dirbuf),y          
        iny      
        lda  sector      
        sta  (dirbuf),y          

        ldx  wlindx      
        lda  entsec      
        sta  dsec,x      
        lda  entind      
        sta  dind,x      
        jsr  curblk      
        jsr  drtwrt      
        jmp  opfin       

;**********************************


op90    lda  filtrk     ; open read (& load)
        bne  op100       
op95             
        lda  #flntfd    ; track not recorded
        jmp  cmderr     ; not found
op100            
        lda  mode        
        cmp  #mdmode     
        beq  op110       
        lda  #$20        
        bit  pattyp      
        beq  op110       
        lda  #filopn     
        jmp  cmderr      
op110   lda  pattyp      
        and  #typmsk    ; type is in index table
        cmp  type        
        beq  op120       
op115   lda  #mistyp    ; type mismatch
        jmp  cmderr      
op120           	; everything is ok!
        ldy  #0          
        sty  f2ptr       
        ldx  mode        
        cpx  #apmode     
        bne  op125       
        cmp  #reltyp     
        beq  op115       

        lda  (dirbuf),y          
        and  #$4f        
        sta  (dirbuf),y          
        lda  sa          
        pha      
        lda  #irsa       
        sta  sa          
        jsr  curblk      
        jsr  drtwrt      
        pla      
        sta  sa          
op125            
        jsr  opread      
        lda  mode        
        cmp  #apmode     
        bne  opfin       

        jsr  append      
        jmp  endcmd      

;**********************
opread           
        ldy  #19         
        lda  (dirbuf),y          
        sta  trkss       
        iny      
        lda  (dirbuf),y          
        sta  secss       
        iny      
        lda  (dirbuf),y          
        ldx  rec         
        sta  rec         
        txa      
        beq  op130       
        cmp  rec         
        beq  op130       
        lda  #norec      
        jsr  cmderr      
op130            
        ldx  f2ptr       
        lda  filtrk,x    
        sta  track       
        lda  filsec,x    
        sta  sector      
        jsr  opnrch      
        ldy  lindx      ; open a read chnl
        ldx  f2ptr       
        lda  entsec,x    
        sta  dsec,y      
        lda  entind,x    
        sta  dind,y      
        rts      

opwrit           
        lda  fildrv      
        and  #1          
        sta  drvnum      
        jsr  opnwch      
        jsr  addfil     ; add to directory
opfin            
        lda  sa          
        cmp  #2          
        bcs  opf1        

        jsr  gethdr      
        lda  track       
        sta  prgtrk      

        lda  drvnum      
        sta  prgdrv      

        lda  sector      
        sta  prgsec      
opf1             
        jmp  endsav      

cktm             
        ldy  filtbl,x   ; get ptr
        lda  cmdbuf,y   ; get char
        ldy  #nmodes     
ckm1             
        dey      
        bmi  ckm2       ; no valid mode

        cmp  modlst,y    
        bne  ckm1        
        sty  mode       ; mode found
ckm2             
        ldy  #ntypes     
ckt1             
        dey      
        bmi  ckt2       ; no valid type

        cmp  tplst,y     
        bne  ckt1        
        sty  type       ; type found
ckt2             
        rts      

append           
        jsr  gcbyte      
        lda  #lrf        
        jsr  tstflg      
        beq  append      

        jsr  rdlnk       
        ldx  sector      
        inx      
        txa      
        bne  ap30        
        jsr  wrt0       ; get another block
        lda  #2          
ap30             
        jsr  setpnt      
        ldx  lindx       
        lda  #rdylst     
        sta  chnrdy,x    
        lda  #$80       ; chnl bit
        ora  lindx       
        ldx  sa          
        sta  lintab,x    
        rts      


; load directory
loadir           
        lda  #ldcmd      
        sta  cmdnum      
        lda  #0         ; load only drive zero
        ldx  cmdsiz      
        dex      
        beq  ld02        

ld01    dex     	; load by name
        bne  ld03        
        lda  cmdbuf+1    
        jsr  tst0v1      
        bmi  ld03        

ld02            	; load dir with a star
        sta  fildrv      
        inc  f1cnt       
        inc  f2cnt       
        inc  filtbl      
        lda  #$80        
        sta  pattyp      
        lda  #'*         
        sta  cmdbuf     ; cover both cases
        sta  cmdbuf+1    
        bne  ld10       ; (branch)

ld03             
        jsr  prscln      
        bne  ld05       ; found ":"
;search by name on both drives
        jsr  cmdrst      
        ldy  #3          
ld05    dey      
        dey      
        sty  filtbl      

        jsr  tc35       ; parse & set tables
        jsr  fs1set      
        jsr  alldrs      

ld10    jsr  optsch     ; new directory
        jsr  newdir      
        jsr  ffst        
ld20    jsr  stdir      ; start directory
        jsr  getbyt     ; set 1st byte
        ldx  lindx       
        sta  chndat,x    
        lda  drvnum      
        sta  lstdrv      
        ora  #4          
        sta  filtyp,x    
        lda  #0          
        sta  buftab+cbptr        
        rts      
        .page  
        .subttl 'opnchnl.src'
; opnchnl

; open a read chanl with 2 buffers
; will insert sa in lintab
; and inits all pointers.
; relative ss and ptrs are set.

opnrch  lda  #1         ; get one data buffer
        jsr  getrch      
        jsr  initp      ; clear pointers
        lda  type        
        pha      
        asl  a   
        ora  drvnum      
        sta  filtyp,x   ; set file type
        jsr  strrd      ; read 1st one or two blocks
        ldx  lindx       
        lda  track       
        bne  or10        

        lda  sector      
        sta  lstchr,x   ; set last char ptr
or10             
        pla      
        cmp  #reltyp     
        bne  or30       ; must be sequential stuff

        ldy  sa          
        lda  lintab,y   ; set channel as r/w
        ora  #$40        
        sta  lintab,y    

        lda  rec         
        sta  rs,x       ; set record size

        jsr  getbuf     ; get ss buffer
        bpl  or20        
        jmp  gberr      ; no buffer
or20             
        ldx  lindx       
        sta  ss,x        
        ldy  trkss      ; set ss track
        sty  track       
        ldy  secss      ; set ss sector
        sty  sector      
        jsr  seth       ; set ss header
        jsr  rdss       ; read it in
        jsr  watjob      
orow             
        ldx  lindx       
        lda  #2          
        sta  nr,x       ; set for nxtrec

        lda  #0          
        jsr  setpnt     ; set first data byte

        jsr  rd40       ; set up 1st record
        jmp  gethdr     ; restore t&s

or30             
        jsr  rdbyt      ; sequential set up
        ldx  lindx       
        sta  chndat,x    
        lda  #rdytlk     
        sta  chnrdy,x    
        rts      

; initialize variables for open chanl
; lstjob,sets active buffer#,lstchr,
; buffer pointers in buftab=2

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

initp   ldx  lindx       
        lda  buf0,x      
        asl  a   
	bmi  1$		; *** rom ds 02/27/85 ***

        tay      
        lda  #2          
        sta  buftab,y    

1$      lda  buf1,x      
        ora  #$80        
        sta  buf1,x      
        asl  a   
	bmi  2$		; *** rom ds 02/27/85 ***

        tay      
        lda  #2          
        sta  buftab,y    
2$      lda  #0          
        sta  nbkl,x      

	jmp  ptch41	; *** rom ds 02/27/85 ***
	nop		; fill

;       sta  nbkh,x      
;       lda  #0          
;       sta  lstchr,x    
;       rts      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>


; open a write chanl with 2 buffers

opnwch  jsr  intts      ; get first track,sector
        lda  #1          
        jsr  getwch     ; get 1 buffers for writing
        jsr  sethdr     ; set up buffer headers
        jsr  initp      ; zropnt
        ldx  lindx       
        lda  type        
        pha      
        asl  a   
        ora  drvnum      
        sta  filtyp,x   ; set filtyp=seq
        pla      
        cmp  #reltyp     
        beq  ow10        
        lda  #rdylst    ;  active listener
        sta  chnrdy,x    
        rts      

ow10             
        ldy  sa          
        lda  lintab,y    
        and  #$3f        
        ora  #$40        
        sta  lintab,y   ; set channel as r/w

        lda  rec         
        sta  rs,x       ; set record size

        jsr  getbuf     ; get ss buffer
        bpl  ow20        
        jmp  gberr      ; no buffer
ow20             
        ldx  lindx       
        sta  ss,x        
        jsr  clrbuf      

        jsr  nxtts       
        lda  track       
        sta  trkss      ; save ss t&s
        lda  sector      
        sta  secss       

        ldx  lindx       
        lda  ss,x        
        jsr  seth       ; set ss header
        lda  #0          
        jsr  setssp      
        lda  #0         ; set null link
        jsr  putss       
        lda  #ssioff+1  ; set last char
        jsr  putss       
        lda  #0         ; set this ss #
        jsr  putss       
        lda  rec        ; record size
        jsr  putss       
        lda  track       
        jsr  putss       
        lda  sector      
        jsr  putss       
        lda  #ssioff     
        jsr  setssp      
        jsr  gethdr     ; get first t&s
        lda  track       
        jsr  putss       
        lda  sector      
        jsr  putss       

        jsr  wrtss      ; write it out
        jsr  watjob      
        lda  #2          
        jsr  setpnt      

        ldx  lindx      ; set nr for null buffer
        sec      
        lda  #0          
        sbc  rs,x        
        sta  nr,x        

        jsr  nulbuf     ; null records
        jsr  nullnk      
        jsr  wrtout      
        jsr  watjob      
        jsr  mapout      
        jmp  orow        

;*
;*
;***********************
;*
;* putss
;*
;* put byte into side sector
;*
;***********************
;*
;*
putss   pha      
        ldx  lindx       
        lda  ss,x        
        jmp  putb1       
        .page  
        .subttl 'parsex.src'

;parse & execute string in cmdbuf

parsxq           
        lda  #0          
        sta  wbam        
        lda  lstdrv      
        sta  drvnum      
        jsr  okerr       
        lda  orgsa       
        bpl  ps05        
        and  #$f         
        cmp  #$f         
        beq  ps05        
        jmp  open        
ps05    jsr  cmdset     ; set variables,regs
        lda  (cb),y      
        sta  char        
        ldx  #ncmds-1   ; search cmd table
ps10    lda  cmdtbl,x    
        cmp  char        
        beq  ps20        
        dex      
        bpl  ps10        
        lda  #badcmd    ; no such cmd
        jmp  cmderr      
ps20    stx  cmdnum     ; x= cmd #

        cpx  #pcmd      ; cmds not parsed
        bcc  ps30        
        jsr  tagcmd     ; set tables, pointers &patterns
ps30    ldx  cmdnum      
        lda  cjumpl,x    
        sta  temp        
        lda  cjumph,x    
        sta  temp+1      
        jmp  (temp)     ; command table jump

; successful command termination
endcmd           
        lda  #0          
        sta  wbam        
endsav           
        lda  erword      
        bne  cmderr      
;
        ldy  #0          
        tya      
        sty  track       

scrend  sty  sector     ; scratch entry
        sty  cb          
        jsr  errmsg      
        jsr  erroff      
scren1           
        lda  drvnum      
        sta  lstdrv      
        tax      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch43	; *** rom ds 05/01/85 ***
	nop		; fill
;       lda  #0          
;       sta  nodrv,x     
rtch43

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        jsr  clrcb       
        jmp  freich     ; free internal channel
;
clrcb            
        ldy  #cmdlen-1   
        lda  #0          
clrb2            
        sta  cmdbuf,y    
        dey      
        bpl  clrb2       
        rts      
;
;
; command level error processing

cmderr  ldy  #0          
        sty  track       
        sty  sector      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch47	; *** rom ds 03/31/85 ***
;       jmp  cmder2      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

simprs  ldx  #0         ; simple parser
        stx  filtbl      
        lda  #':         
        jsr  parse       
        beq  sp10        
        dey      
        dey      
        sty  filtbl      
sp10    jmp  setany     ; set drive #
        .page  'parse-tagcmd'    
prscln           
        ldy  #0          
        ldx  #0          
        lda  #':         
        jmp  parse      ; find pos'n of ":"
;
;tag command string
;  set up cmd structure
;  image & file stream ptrs
;

tagcmd           
        jsr  prscln      
        bne  tc30        
tc25    lda  #nofile    ; none, no files
        jmp  cmderr      
tc30    dey      
        dey      
        sty  filtbl     ; ":"-1 starts fs1
        txa      
        bne  tc25       ; err: "," before ":"
tc35    lda  #'=        ; search: "="
        jsr  parse       
        txa     	; ?file count= 1-1?
        beq  tc40        
        lda  #%01000000 ; g1-bit
tc40    ora  #%00100001 ; e1,^e2-bits
        sta  image      ; fs structure
        inx      
        stx  f1cnt       
        stx  f2cnt      ; init for no fs2
        lda  patflg      
        beq  tc50        
        lda  #%10000000 ; p1-bit
        ora  image       
        sta  image       
        lda  #0          
        sta  patflg     ; clear pattern flag
tc50    tya     	; ptr to fs2
        beq  tc75       ;  fs2 not here
        sta  filtbl,x    
        lda  f1cnt      ; fs2 is here now,...
        sta  f2ptr      ; ...now set f2 ptr
        lda  #$8d       ; find cr-shifted
        jsr  parse      ; parse rest of cmd string
        inx     	; advance filtbl ptr to end
        stx  f2cnt      ; save it
        dex     	; restore for test
        lda  patflg     ; save last pattern
        beq  tc60       ; ?any patterns?
        lda  #%1000     ; yes, p2-bit
tc60    cpx  f1cnt      ; ?f2cnt=f1cnt+1?
        beq  tc70        
        ora  #%0100     ; g2-bit
tc70    ora  #%0011     ; e2-bit,^e2-bit
        eor  image      ; eor clears ^e2-bit
        sta  image       
tc75             
        lda  image       
        ldx  cmdnum      
        and  struct,x   ; match cmd template
        bne  tc80        
        rts      
tc80    sta  erword     ; **could be warning
        lda  #badsyn    ; err: bad syntax
        jmp  cmderr      

        .page
	.subttl  'parse'   

;parse string
;  looks for special chars,
;  returning when var'bl char
;  is found
;   a: var'bl char
;   x: in,out: index, filtbl+1
;   y: in: index, cmdbuf
;     out: new ptr, =0 if none
;         (z=1) if y=0

parse   sta  char       ; save var'bl char
pr10    cpy  cmdsiz     ; stay in string
        bcs  pr30        
        lda  (cb),y     ; match char
        iny      
        cmp  char        
        beq  pr35       ; found char
        cmp  #'*        ; match pattern chars
        beq  pr20        
        cmp  #'?         
        bne  pr25        
pr20    inc  patflg     ; set pattern flag
pr25    cmp  #',        ; match file separator
        bne  pr10        
        tya      
        sta  filtbl+1,x ; put ptrs in table
        lda  patflg     ; save pattern for ea file
        and  #$7f        
        beq  pr28        
        lda  #$80       ; retain pattern presence...
        sta  pattyp,x    
        sta  patflg     ; ...but clear count
pr28    inx      
        cpx  #mxfils-1   
        bcc  pr10       ; no more than mxfils
pr30    ldy  #0         ; y=0 (z=1)
pr35    lda  cmdsiz      
        sta  filtbl+1,x          
        lda  patflg      
        and  #$7f        
        beq  pr40        
        lda  #$80        
        sta  pattyp,x    
pr40    tya     	; z is set
        rts      

;initialize command tables, ptrs, etc.

cmdset  ldy  buftab+cbptr        
        beq  cs08        
        dey      
        beq  cs07        

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  ptch49	; *** rom ds 04/14/85 ***
;       lda  cmdbuf,y    

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        cmp  #cr         
        beq  cs08        
        dey      

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  ptch49	; *** rom ds 04/14/85 ***
;       lda  cmdbuf,y    

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

        cmp  #cr         
        beq  cs08        
        iny      
cs07    iny      
cs08    sty  cmdsiz     ; set cmd string size

        cpy  #cmdlen+1   
        ldy  #$ff        
        bcc  cmdrst      
        sty  cmdnum      
        lda  #longln    ; long line error
        jmp  cmderr      
;  clear variables,tables
cmdrst           
        ldy  #0          
        tya      
        sta  buftab+cbptr        
        sta  rec         
        sta  type        
        sta  typflg      
        sta  f1ptr       
        sta  f2ptr       
        sta  f1cnt       
        sta  f2cnt       
        sta  patflg      
        sta  erword      
        ldx  #mxfils     
cs10    sta  filtbl-1,x          
        sta  entsec-1,x          
        sta  entind-1,x          
        sta  fildrv-1,x          
        sta  pattyp-1,x          
        sta  filtrk-1,x          
        sta  filsec-1,x          
        dex      
        bne  cs10        
        rts      
	.page
	.subttl	'patchn.src'

;*****************************************************

jslower txa
	ldx  #5
	bne  jslowd+3	; bra

jslowd  txa
	ldx  #$0d	; insert 40 us. delay at 2 Mhz
1$	dex
	bne  1$
	tax
	rts

;*****************************************************

sav_pnt	lda  bmpnt	; save pointers
	sta  savbm
	lda  bmpnt+1	
	sta  savbm+1
	rts

;*****************************************************

res_pnt lda  savbm
	sta  bmpnt	; save pointers
	lda  savbm+1
	sta  bmpnt+1	
	rts

;*****************************************************

set_bm  ldx  drvnum

	NODRRD		; read nodrv,x absolute
;	lda  nodrv,x	; drive active

	beq  1$

	lda  #nodriv

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

;	jsr  cmder3
	jsr  cmder2	; *** rom ds 11/07/85 beta9 ***, set error

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

1$      jsr  bam2x	; get index into bufx
	jsr  redbam	; read BAM if neccessary

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  2$		; *** rom ds 09/12/85 ***, always in memory!
;	lda  wbam	; write pending ?

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	beq  3$

	ora  #$80
	sta  wbam	; set write pending flag
	bne  2$		; bra

3$	jsr  wrt_bam	; write out BAM, *** rom ds 9/12/85 ***, never get here

2$	jsr  sav_pnt	; save current BAM pointers
	jsr  where	; find BAM	
	lda  track	; get offset
	sec
	sbc  #36
	tay
	lda  (bmpnt),y	; get count
	pha
	jsr  res_pnt	; restore BAM pointers
	pla		; count in .a
	rts

   .byte $44,$41,$56,$49,$44,$20,$47,$2e,$20,$53,$49,$52,$41,$43,$55,$53,$41

bam_pt	lda  track	
	sec
	sbc  #36	; offset
	tay
	lda  sector	; sector/8
	lsr  a
	lsr  a
	lsr  a
	clc
	adc  bmindx,y	; get location
	tay	
	lda  sector
	and  #7
	tax		; which sector
	lda  ovrbuf+$46,y
	and  bmask,x
	php		; save status
	lda  ovrbuf+$46,y
	plp		; retrieve status set/clr (z)
	rts	

;*****************************************************

deall_b	jsr  sav_pnt	; save pointers
	jsr  where	; where is the BAM
	lda  track
	sec
	sbc  #36	; offset
	tay		; index into table
	clc
	lda  (bmpnt),y	; goto location and add 1
	adc  #1
	sta  (bmpnt),y
	jmp  res_pnt	; restore BAM pointers

;*****************************************************

alloc_b jsr  sav_pnt	; save pointers
	jsr  where	; find location of the BAM
	lda  track
	sec
	sbc  #36	; offset
	tay		; index into table
	sec
	lda  (bmpnt),y	; goto location and sub 1
	sbc  #1
	sta  (bmpnt),y
	jmp  res_pnt	; restore pointers

;*****************************************************

where	ldx  #blindx+7
	lda  buf0,x
	and  #$0f
	tax
	lda  bufind,x	; which buffer is the BAM in
	sta  bmpnt+1
	lda  #$dd	
	sta  bmpnt	
	rts

;*****************************************************

chk_blk lda  temp
	pha		; save temp

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  ptch71	; *** rom ds 05/19/86 ***
;	jsr  sav_pnt	

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	lda  track
	sec
	sbc  #36	; get offset
	tay
	pha		; save 
	jsr  where	; where is the BAM
	lda  (bmpnt),y
	pha	
	lda  #0
	sta  temp	; start at zero
	lda  #>bam_one
	sta  bmpnt+1	; msb
	lda  bmindx,y	; starts here
	clc
	adc  #<bam_one	; add offset 
	sta  bmpnt

	ldy  #2	
1$	ldx  #7		; verify bit map to count value
2$	lda  (bmpnt),y
	and  bmask,x
	beq  3$

	inc  temp	; on free increment
3$	dex
	bpl  2$

	dey
	bpl  1$

	pla
	cmp  temp
	beq  4$
	
	lda  #direrr
	jsr  cmder2

4$	pla
	tay		; restore track offset
	pla
	sta  temp	; restore temp
	jmp  res_pnt

;*****************************************************

wrt_bam lda  pota1	; 1/2 Mhz ?
	and  #$20
	bne  1$

2$	jmp  dowrit
	
1$	lda  maxtrk
	cmp  #37
	bcc  2$

	ldx  jobnum	
	lda  lstjob,x	; save last job
	pha
	jsr  dowrit	; write out side zero
	jsr  sav_pnt	; save bmpnt
	jsr  setbpt
	jsr  clrbam+3	; clear buffer
	lda  jobnum
	asl  a
	tax
	lda  #53
	sta  hdrs,x	; put track in job queue, same sector

	ldy  #104
3$	lda  ovrbuf+$46,y
	sta  (bmpnt),y
	dey 
	bpl  3$		; transfer to buffer

	jsr  res_pnt	; restore pointers
	jsr  dowrit	; write this track out

	lda  jobnum
	asl  a
	tax
	lda  dirtrk     ; *2
	sta  hdrs,x	; read back BAM side zero	
	jsr  doread	; done
	pla
	ldx  jobnum
	sta  lstjob,x	; restore last job
	rts
;*****************************************************

bmindx  .byte 0,3,6,9,12,15,18,21,24,27,30,33,36,39,42,45,48,51,54
	.byte 57,60,63,66,69,72,75,78,81,84,87,90,93,96,99,102

	.page
;----------------------------------------------------
; 
; PATCHES  David G Siracusa - Commodore (c) 1985
;
; Compatibility is fun...
; I know you can say fun... I know you can...
;
;----------------------------------------------------
;
;     patch 18  *rom ds 01/21/85*
;     
ptch18  lda  pota1	; 1/2 Mhz ?
	and  #$20
	beq  1$

	ldy  #0		; clr regs
	ldx  #0
	lda  #1		; place filename
	sta  filtbl
	jsr  onedrv	; setup drv
	jmp  rtch18	; ret

1$	lda  #$8d	; 1541 mode
	jsr  parse
	jmp  rtch18	; ret
;
;
;----------------------------------------------------
;
;     patch 19  *rom ds 01/21/85*
;     
ptch19	jsr  parsxq	; parse & xeq cmd
	jsr  spinp	; input
	lda  fastsr	; clr error
	and  #$7f
	sta  fastsr	
	jmp  rtch19
;
;
;----------------------------------------------------
;
;     patch 20  *rom ds 01/21/85*
;
ptch20	lda  #255
	sta  acltim
	lda  #6		; setup timer2
	sta  acltim2
	rts
;
;
;----------------------------------------------------
;
;     patch 21  *rom ds 01/21/85*
;
ptch21	bne  1$		
	
	lda  mtrcnt	; anything to do ?
	bne  2$
	beq  3$

1$	lda  #255
	sta  mtrcnt	; irq * 255
	jsr  moton	; turn on the motor

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jsr  ptch72	; *** rom ds 05/20/86 ***
	nop
;	lda  #1
;	sta  wpsw

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	bne  3$		; bra

2$	dec  mtrcnt
	bne  3$

	lda  drvst
	bne  3$

	jsr  motoff	; turn off the motor

3$	jmp  rtch21	; return
;
;
;----------------------------------------------------
;
;     patch 22 
;
ptch22  lda  #2	
	sta  pb
	lda  #$20	; *,_atn,2 mhz,*,*,side 0,sr-in,*
	sta  pa1	
	jmp  ptch22r	; ret
;
;
;----------------------------------------------------
;
;     patch 23
;
;
ptch23  lda  pota1	; 1/2 Mhz ?
	and  #$20
	bne  1$

2$	jmp  doread	; 1541 mode read BAM side 0

1$	lda  maxtrk	; seek ok, on other side ?
	cmp  #37
	bcc  2$		; seek regular method

	jsr  sav_pnt	; save pointers

	lda  #0
	sta  bmpnt	; even page
	ldx  jobnum
	lda  bufind,x	; which buffer is it in
	sta  bmpnt+1
	lda  #$ff

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch70	; *** rom ds 05/14/86 ***
;	sta  jobrtn	; error recovery on
rtch70			; ret

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	lda  jobnum
	asl  a
	tax
	lda  #53	; read BAM side one
	sta  hdrs,x	; put track in job queue, same sector
	jsr  doread	; read it
	cmp  #2		; ok ?
	ror  a		; carry set bad return
	and  #$80	; isolate sign bit
	eor  #$80
	sta  dside
	bpl  3$		; br, error

	ldy  #104
4$	lda  (bmpnt),y	; get BAM 1 and put somewhere in God's country
	sta  ovrbuf+$46,y
	dey 
	bpl  4$

3$
	lda  #$ff
	sta  jobrtn	; set error recovery on

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

rtch70a

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	lda  jobnum
	asl  a
	tax
	lda  dirtrk	; read BAM side zero
	sta  hdrs,x	; put track in job queue, same sector
	jsr  doread	; read it
	cmp  #2
	bcc  5$

	tax		; save error
	lda  #36
	sta  maxtrk	; def single sided
	jsr  res_pnt	; save BAM pointers
	txa
	jsr  error      ; let the error handler do the rest
	jmp  rec7	; consist
	
5$	ldy  #3
	lda  (bmpnt),y	; check double sided flag
	and  dside	; both must be ok....
	bmi  6$

	lda  #36
	.byte skip2
6$	lda  #71
	sta  maxtrk	; double sided

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	jmp  ptch69	; *** rom ds 05/14/86 fix swap problem ***
;	jmp  res_pnt	; restore BAM pointers

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>
;
;
;----------------------------------------------------
;
;     patch 24
;
ptch24  jsr  dojob	; seek side zero
	pha		; save status
	cmp  #2		; error ?
	bcs  1$		; br, error...
	
	lda  pota1
	and  #$20
	beq  1$		; 1/2 Mhz ?

	lda  #71
	sta  maxtrk	; let DOS think he has double sided capability

	lda  #$ff
	sta  jobrtn	; return from error
	lda  header	; get id's
	pha
	lda  header+1
	pha		; save them

	lda  jobnum
	asl  a
	tax
	lda  #53	; seek side one BAM track
	sta  hdrs,x	; put track in job queue, same sector
	lda  #seek

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

;	jsr  dojob	; try it...
	jsr  ptch64 	; make it faster...

;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>

	cmp  #2		; error ?

	pla
	tay		; header+1
	pla
	tax		; header
	bcs  2$		; error on last job ?

	cpx  header	; same id's ?
	bne  2$

      	cpy  header+1	; same id's ?
	bne  2$

	lda  #71	; probably double sided
	.byte skip2
2$	lda  #36
	sta  maxtrk

	sty  header+1	; restore original id's from side zero
	stx  header

	lda  jobnum
	asl  a
	tax
	lda  dirtrk	
	sta  hdrs,x	; put track in job queue, same sector
	
1$	pla		; get status
	rts
;
;
;----------------------------------------------------
;
;     patch 25
;
ptch25 	jsr  setbpt	; setup bit map pointer
	lda  pota1
	and  #%00100000 ; 1/2 Mhz ?
	beq  1$

	lda  #0
	ldy  #104	; clr BAM side one
2$	sta  ovrbuf+$46,y
	dey
	bpl  2$

1$	jmp  rtch25	; return
;
;
;----------------------------------------------------
;
;     patch 26
;
ptch26  pha		; save .a
	lda  pota1
	and  #%00100000 ; 1/2 Mhz ?
	beq  1$

	pla
	cmp  #36	; > trk 35 ?
	bcc  2$

	sbc  #35	
	.byte skip1
1$	pla		; restore .a
2$	ldx  nzones	; cont
	rts
;
;
;----------------------------------------------------
;
;     patch 27
;
ptch27  jsr  clrbam	; clear BAM area
	lda  pota1	
	and  #%00100000
	bne  1$		; 1/2 Mhz ?

	lda  #36
	.byte skip2
1$	lda  #71
	sta  maxtrk	; set maximum track full format
	jmp  rtch27	; return
;
;
;----------------------------------------------------
;
;     patch 28
;
ptch28  lda  pota1	
	and  #%00100000
	bne  1$		; 1/2 Mhz ?

	jmp  format	; 1541 mode

1$      jmp  jformat	; 1571 mode	
;
;
;----------------------------------------------------
;
;     patch 29
;
ptch29  lda  pa1	; change to 1 mhz
	and  #%11011111
	sta  pa1
	jsr  jslowd	; wait for ...

	lda  #$7f
	sta  icr	; clear all sources of irq's
	lda  #8
	sta  cra	; turn off timers
	sta  cra+1	; off him too...

	lda  #0		; msb nil
	sta  tima_h
	lda  #6		; msb nil
	sta  tima_l	; 6 us bit cell time
	lda  #1
	sta  cra	; start timer a
	jsr  spinp	; finish up and enable irq's from '26
	jmp  tstatn	; chk for atn & goto xidle
;
;
;----------------------------------------------------
;
;     patch 30
;
ptch30  
;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>
;	lda  pota1	
	jsr  ptch57	; *** rom ds 08/05/85 ***, serial bus fix for a
			; very old bug.
;<><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>
	and  #%00100000
	beq  1$		; 1/2 Mhz ?

	jmp  jatnsrv	; 2 Mhz
1$	jmp  atnsrv	; 1 Mhz
;
;
;----------------------------------------------------
;
;     patch 31
;
ptch31	sei
	ldx  #topwrt	; set stack pointer
	txs
	jmp  rtch31
;  
;
;----------------------------------------------------
;
;     patch 32
;
ptch32  lda  pota1	; 1/2 Mhz ?
	and  #$20
ptch32a	bne  1$

6$	ldy  #3
	lda  #0
	sta  (bmpnt),y	; set single sided bit in BAM side 0
	jmp  newmap	; finish up 1541 mode

1$	lda  maxtrk	; single/double sided?
	cmp  #37
	bcc  6$

	ldy  #1
	ldx  #0
2$	cpy  #18
	beq  4$

	txa
	pha		; save .x
	lda  #0		; start temps at zero
	sta  t0
	sta  t1
	sta  t2
	lda  num_sec-1,y
	tax		; number of sectors per track
3$	sec
	rol  t0
	rol  t1
	rol  t2
	dex
	bne  3$

	pla
	tax		; restore .x

	lda  t0		; write BAM side one
	sta  ovrbuf+$46,x
	lda  t1
	sta  ovrbuf+$46+1,x
	lda  t2
	sta  ovrbuf+$46+2,x
	inx
	inx
	inx
	cpx  #$33	; skip track 53
	bne  4$

	inx
	inx
	inx
	iny		; bypass and leave allocated
	
4$	iny
	cpy  #36
	bcc  2$
	
	jsr  newmap	; generate new BAM for side zero

	ldy  #3
	lda  #$80
	sta  (bmpnt),y	; set double sided bit in BAM side 0

	ldy  #$ff
	ldx  #34
5$	lda  num_sec,x	; get # of free sectors
	sta  (bmpnt),y	; save in top end of BAM zero
	dey		; => $dd-$ff
	dex
	bpl  5$

	ldy  #$ee	; offset for track 53	
	lda  #0
	sta  (bmpnt),y	; allocate track 53 

	jmp  nfcalc	; calculate BAM side 0/1
;
;
;----------------------------------------------------
;
;     patch 33
;
ptch33	lda  pota1	; 1/2 Mhz ?
	and  #$20
	bne  1$

2$	jsr  freuse	; track < 36 or 1541 mode
	jmp  frets2	; ret

1$	lda  track	; is track greater than 35
	cmp  #36
	bcc  2$

	jsr  set_bm	; setup & update parms
	jsr  bam_pt	; find position within table
	bne  3$

	ora  bmask,x	; set it free
	sta  ovrbuf+$46,y
	jsr  dtybam	; set dirty flag
	jsr  deall_b	; deallocate BAM
	lda  track
	cmp  #53	; skip BAM track side one
	beq  4$

	lda  drvnum	; get drv #
	asl  a
	tax		; *2
	jmp  fre20	; ret
3$	sec
4$	rts
;
;
;----------------------------------------------------
;
;     patch 34
;
ptch34	lda  pota1	; 1/2 Mhz ?
	and  #$20
	bne  1$

2$	jsr  freuse	; track < 36 or 1541 mode
	jmp  rtch34

1$	lda  track	; check track
	cmp  #36
	bcc  2$

	jsr  set_bm	; setup & update parms
	jsr  bam_pt	; find position within the BAM
	beq  3$

	eor  bmask,x	; set it used
	sta  ovrbuf+$46,y
	jsr  dtybam	; set dirty flag
	jsr  alloc_b	; allocate BAM
	lda  track
	cmp  #53	; skip BAM track side one
	beq  3$

	lda  drvnum	; get drv #
	asl  a
	tax	
	jmp  use30
3$	rts
;
;
;----------------------------------------------------
;
;     patch 35
;
ptch35  lda  pota1	; 1/2 Mhz ?
	and  #$20
	bne  1$

2$	jsr  setbam	; track < 36 or 1541 mode
	jmp  rtch35

1$	lda  track	; check track
	cmp  #36
	bcc  2$

	jsr  set_bm	; setup & update parms
	jsr  chk_blk	; chk alloc w/ bits

	lda  num_sec,y  ; get number of sectors on this track 
	sta  lstsec	; & save it
4$	lda  sector	; what sector are we looking at ?
	cmp  lstsec
	bcs  5$

	jsr  bam_pt	; check availability
	bne  6$

	inc  sector	; we will find something I hope...
	bne  4$

5$	lda  #0	
6$	rts		; (z=1) used 	
;
;
;----------------------------------------------------
;
;     patch 36
;
ptch36  lda  pota1	; 1/2 Mhz ?
	and  #$20
	bne  1$

2$	lda  temp
	pha
	jmp  rtch36

1$	lda  track	; check track
	cmp  #36
	bcc  2$

	cmp  #53
	beq  3$

	lda  temp
	pha		; save temp var
	jsr  set_bm	; setup & update parms
	tay		; save .a
	pla
	sta  temp	; restore temp	
	tya		; return allocation status in .a
	jmp  rtch36a

3$	lda  #0		; z=1
	jmp  rtch36a	; track 53 allocated
;
;
;----------------------------------------------------
;
;     patch 37
;
ptch37  lda  pota1	; 1/2 Mhz ?
	and  #$20
	bne  1$

2$	jsr  setbam	; setup the BAM pointer
	jmp  rtch37	; ret

1$	lda  track	; check track
	cmp  #36
	bcc  2$

	jsr  set_bm	; setup & update parms
	jmp  rtch37a	
;
;
;----------------------------------------------------
;
;     patch 38
;
ptch38  lda  pota1	; 1/2 Mhz ?
	and  #$20
	bne  1$

2$	jsr  setbam	; setup the BAM pointer
	jmp  rtch38	; ret

1$	lda  track	; check track
	cmp  #36
	bcc  2$

	jsr  set_bm	; setup & update parms
	jmp  rtch38a
;
;
;----------------------------------------------------
;
;     patch 39
;
ptch39	lda  pota1	; 1/2 Mhz ?
	and  #$20
	bne  1$

2$	jmp  avck	; check block availability

1$	lda  maxtrk	; check maximum track
	cmp  #37
	bcc  2$

	lda  track	; check track do regular check
	cmp  #36
	bcc  2$

	jmp  chk_blk	; finish up
;
;
;----------------------------------------------------
;
;     patch 40
;
ptch40  sta  ndbl,x	; save low order
	lda  pota1	; 1/2 Mhz ?
	and  #$20
	beq  1$

	lda  maxtrk	; check maximum track
	cmp  #37
	bcc  1$

	jsr  sav_pnt	; save pointers
	jsr  where	; locate BAM
	
	ldy  #34	; count backwards and sleep........zzzzZZZZZZZZ
	lda  ndbl
2$	clc
	adc  (bmpnt),y	; count it up
	sta  ndbl	; keep track HAHA !!!
	bcc  3$

	inc  ndbh	; increment msb
3$	dey
	bpl  2$
 
	jmp  res_pnt	; restore pointers

1$	rts		; done
;
;
;----------------------------------------------------
;
;     patch 41
;
ptch41  sta  nbkl,x
	sta  nbkh,x
	lda  #0
	sta  lstchr,x
	rts
;
;
;----------------------------------------------------
;
;     patch 42
;
ptch42  jsr  jformat	; format disk in GCR
	ldy  #0
	sty  jobrtn
	rts
;
	*=*+11		; address adjust
;
;
;----------------------------------------------------
;
;     patch 43
;
;
ptch43  lda  #0		; clr nodrv
	NODRWR		; write nodrv,x absolute
	jmp  rtch43
;
;
;----------------------------------------------------
;
;     patch 44
;
;  
ptch44  tya		; set/clr nodrv
	NODRWR		; write nodrv,x absolute
	jmp  rtch44
;
;
;----------------------------------------------------
;
;     patch 45
;
ptch45  lda  pota1	; 1/2 Mhz ?
	and  #$20
	beq  1$

	jmp  a2		; 1571 mode
1$	jmp  atns20	; 1541 mode
;
;
;----------------------------------------------------
;
;     patch 46
;
ptch46	pha		; save error
	stx  jobnum	; save job #
	lda  pota1	; 1/2 Mhz ?
	and  #$20
	beq  1$

	bit  fastsr	; error recovery on ?
	bpl  1$

	lda  fastsr
	and  #$7f	; clr error recovery
	sta  fastsr

	pla		; get error
	tax
	jmp  ctr_err	; let it error out

1$	jmp  rtch46	; return	
;
;
;----------------------------------------------------
;
;     patch 47
;
ptch47	pha		; save error
	lda  pota1	; 1/2 Mhz ?
	and  #$20
	beq  1$

	bit  fastsr	; error recovery on ?
	bpl  1$

	lda  fastsr
	and  #$7f	; clr error recovery
	sta  fastsr
	sei
	ldx  #2
	jsr  handsk	; send it
	lda  #0
	sta  sa
	jsr  close	; close channel

1$	pla		; get error #
	jmp  cmder2	
;
;
;----------------------------------------------------
;
;     patch 48
;
ptch48  lda  #0
	sta  drvst	; clr drive status
	lda  pcr2	; set edge and latch mode
	jmp  rtch48
;
;
;----------------------------------------------------
;
;     patch 49
;
ptch49  lda  cmdbuf	; is there a 'U0n' cmd waiting in the wings
	cmp  #'U
	bne  1$		; br, no 'U'

	lda  cmdbuf+1
	cmp  #'0	
	beq  2$		; br, we got it toyota

1$	lda  cmdbuf,y	; get char
	.byte skip2
2$	lda  #0		; null
	rts
;
;
;----------------------------------------------------
;
;     patch 50
;
ptch50	ldx  drvnum	; get offset
	NODRRD		; read nodrv,x absolute
	rts
;
;
;----------------------------------------------------
;
;     patch 51
;
ptch51	sta  wpsw,x	; clr wp switch
	NODRWR		; write nodrv,x absolute
	jmp  rtch51
;
;
;----------------------------------------------------
;
;     patch 52
;
ptch52	ldx  drvnum	; get offset
	NODRRD		; read nodrv,x absolute
	jmp  rtch52
;
;
;----------------------------------------------------
;
;     patch 53
;
ptch53  lda  ip
	cmp  #<sysirq	; irq ?
	bne  1$

	lda  ip+1
	cmp  #>sysirq	; irq ?
	bne  1$

	brk		; do irq and return
	nop
	rts

1$	jmp  (ip)
;
;
;----------------------------------------------------
;
;     patch 54
;
ptch54  cmp  #2		; error ?
	bcc  1$

	cmp  #15	; no drv condition ?
	beq  1$

	jmp  rtch54	; bad, try another
1$	jmp  stl50	; ok
;
;
;----------------------------------------------------
;
;     patch 55
;
ptch55  sta  ftnum	; clear flag
	jsr  led_on	; lights on
	jsr  ptch42	; format it
	pha		; save error
	jsr  led_off	; turn off the lights
	pla
	rts
;
;
;----------------------------------------------------
;
;     patch 56
;
ptch56  lda  pattyp	; check file type
	and  #typmsk	; remove other bits
	cmp  #2
	rts
;
;
;----------------------------------------------------
;
;     patch 57
;
ptch57  lda  pota1	; 1/2 Mhz ?
	bit  pa1	; clear h/w interrupt source
	rts
;  
;
;----------------------------------------------------
;
;     patch 58
;
ptch58  lda  pota1	; 1/2 Mhz ?
	and  #$20
	bne  1$

	jmp  newmap	; finish up 1541 mode

1$	jmp  ptch32a	; bra, go doit !!!
;  
;
;----------------------------------------------------
;
;     patch 59
;
ptch59  lda  #2	
	sta  t1hc1	; wait 256 uS
	rts
;  
;
;----------------------------------------------------
;
;     patch 60
;
ptch60  lda  vertog	; verify ?
	bne  1$		; br, no verify

	jmp  cmdele	; doit	
	
1$	clc		; say ok...
	rts		
;  
;
;----------------------------------------------------
;
;     patch 61
;
ptch61	tay
	cmp  #'V	; fast read ?
	bne  devs

	sei		; no irq's
	lda  pota1
	and  #$20
	bne  1$		

2$	jmp  utlbad	; 1571 only

1$	lda  cmdbuf+4	; which ?
	cmp  #'1
	beq  3$

	cmp  #'0
	bne  2$		; no switch specified

3$	and  #$ff-$30
	sta  vertog	; save flag
	cli
	rts

devs	cpy #4
	jmp  rtch61
;  
;
;----------------------------------------------------
;
;     patch 62
;
ptch62	lda  vertog	; to verify or not to verify ?
	bne  1$

	lda  jobs,y	; get job
	.byte skip2
1$	lda  #$30	; verify is off
	eor  #$30
	sta  jobs,y
	bne  2$

	jmp  jerrr	; done

2$	jmp  jseak	; return
;
;
;----------------------------------------------------
;
;     patch 63
;
ptch63	jsr  hskrd	; send interleave
	lda  #bit5
	bit  switch	; send sector table ?
	beq  1$

	ldy  #0
2$	lda  cmdbuf+11,y	
	sta  ctl_dat	; get ready to send
	jsr  hskrd	; send it
	iny
	cpy  cpmsek	; send whole thing
	bne  2$

1$	rts		; done
;
;
;----------------------------------------------------
;
;     patch 64
;
ptch64	ldx  jobnum	; get job #
	ora  #$08	; sector seek... 
	sta  jobs,x
	jmp  stbctl	; this better work!!!
;
;
;----------------------------------------------------
;
;     patch 65
;
ptch65
	jsr  burst_doit
	jmp  endcmd

Burst_doit
	jmp  (ip)
;
;
;----------------------------------------------------
;
;     patch 66
;
ptch66
	cmp  #3
	bcs  1$

	lda  #dskful	
	jsr  errmsg

1$	lda  #1
	rts
;
;
;----------------------------------------------------
;
;     patch 67
;
ptch67
	php
	sei
	lda  #0
	sed
1$	cpx  #0          
        beq  2$

        clc      
        adc  #1          
        dex      
        jmp  1$
2$	plp	
	jmp  hex5
;
;
;----------------------------------------------------
;
;     patch 68
;
ptch68
	php
	sei
	sta  sdr	; send it
        lda  fastsr      
        eor  #4         ; change state of clk
        sta  fastsr      
        lda  #8
1$	bit  icr	; wait transmission time
        beq  1$

	plp
        rts      
;
;
;----------------------------------------------------
;
;     patch 69
;
ptch69
	lda  maxtrk	; double sided?
	cmp  #37
	bcc  5$

	lda  temp
	pha		; save temp
	lda  track
	pha
	ldy  #0
	sty  track	
1$	lda  #0
	sta  temp	; start at zero
	lda  #>bam_one
	sta  bmpnt+1	; msb
	lda  bmindx,y	; starts here
	clc
	adc  #<bam_one	; add offset 
	sta  bmpnt

	ldy  #2	
2$	ldx  #7		; verify bit map to count value
3$	lda  (bmpnt),y
	and  bmask,x
	beq  4$

	inc  temp	; on free increment
4$	dex
	bpl  3$

	dey
	bpl  2$

	jsr  where	; where is the BAM
	lda  temp
	ldy  track
	sta  (bmpnt),y
	inc  track
	ldy  track
	cpy  #35
	bcc  1$		; check side one

	pla
	sta  track	; restore track,
	pla
	sta  temp	; temp,
5$	jmp  res_pnt	; and bam pointers
;
;
;----------------------------------------------------
;
;     patch 70
;
ptch70
	sta  jobrtn
	pha
	lda  swapfg	; read bam one?
	beq  1$

	lda  #0
	sta  swapfg	; clr it
	pla
	jmp  rtch70	; yes

1$	lda  #$80
	sta  dside
	pla
	jmp  rtch70a	; nope
;
;
;----------------------------------------------------
;
;     patch 71
;
ptch71
	jsr  sav_pnt	
	jsr  ptch69	; we can rebuild 'em, we have the technology!
	jmp  sav_pnt	
;
;
;----------------------------------------------------
;
;     patch 72
;
ptch72
	lda  #1
	sta  wpsw
	sta  swapfg	; mark bam one out of memory
	rts
;
;
;----------------------------------------------------
;
;     patch 73
;
ptch73
	lda  #1		; BAM is out of mem
	sta  swapfg
	jmp  ptch23	; continue on...
;
;
;----------------------------------------------------
;
;   patch 74
;
;
ptch74
	lda  #1
	sta  wpsw
	sta  swapfg
	jmp  initdr
        .page
	.subttl 'record.src'          
;*********************************
;* record: position relative     *
;*         pointers to given     *
;*         record number or to   *
;*         last record if out of *
;*         range.                *
;*********************************
record           
        jsr  cmdset     ; init tables, ptrs
        lda  cmdbuf+1    
        sta  sa          
        jsr  fndrch      
        bcc  r20        ; got channel's lindex

        lda  #nochnl    ; no valid channel
        jsr  cmderr      
r20              
        lda  #lrf+ovrflo         
        jsr  clrflg      
        jsr  typfil     ; get file type
        beq  r30        ; it is relative file

        lda  #mistyp    ; wrong type
        jsr  cmderr      
r30              
        lda  filtyp,x    
        and  #1          
        sta  drvnum      
        lda  cmdbuf+2    
        sta  recl,x     ; get record #
        lda  cmdbuf+3    
        sta  rech,x      
        ldx  lindx      ; clear chnrdy to rndrdy
        lda  #rndrdy     
        sta  chnrdy,x    

        lda  cmdbuf+4   ; get offset
        beq  r40         
        sec      
        sbc  #1          
        beq  r40         
        cmp  rs,x        
        bcc  r35         

        lda  #recovf     
        sta  erword      
        lda  #0          
r35              
r40              
        sta  recptr     ; set offset
        jsr  fndrel     ; calc ss stuff
        jsr  sspos      ; set ss ptrs
        bvc  r50         

        lda  #lrf       ; beyond the end
        jsr  setflg     ; set last rec flag
        jmp  rd05        
r50              
        jsr  positn     ; position to record
        lda  #lrf        
        jsr  tstflg      
        beq  r60         
        jmp  rd05        
r60              
        jmp  endcmd     ; that's all

;*********************************
;* positn: position relative     *
;*         data block into active*
;*         buffer & next block   *
;*         into inactive buffer. *
;*********************************

positn           
        jsr  posbuf     ; position buffers
        lda  relptr      
        jsr  setpnt     ; set ptr from fndrel

        ldx  lindx       
        lda  rs,x        
        sec     	; calc the offset
        sbc  recptr      
        bcs  p2          
        jmp  break      ; should not be needed
p2               
        clc      
        adc  relptr      
        bcc  p30         
        adc  #1          
        sec      
p30              
        jsr  nxout      ; set nr
        jmp  rd15        
        lda  #recovf     
        jsr  cmderr      
;*
;*********************************
;* posbuf: position proper data  *
;*         blocks into buffers   *
;*********************************

posbuf           
        lda  dirbuf      
        sta  r3          
        lda  dirbuf+1    
        sta  r4          
        jsr  bhere      ; is buffer in?
        bne  p10        ; yes!
        rts      
p10              
        jsr  scrub      ; clean buffer
        jsr  getlnk      
        lda  track       
        beq  p80         
        jsr  bhere2      
        bne  p75         
        jsr  dblbuf      
        jmp  freiac      
p75              
        jsr  freiac      

p80              
        ldy  #0         ; get proper block
        lda  (r3),y      
        sta  track       
        iny      
        lda  (r3),y      
        sta  sector      
        jmp  strdbl     ; get next block, too.
bhere            
        jsr  gethdr     ; get the header
bhere2           
        ldy  #0          
        lda  (r3),y      
        cmp  track       
        beq  bh10       ; test sector, too.
        rts      
bh10             
        iny      
        lda  (r3),y      
        cmp  sector     ; set .z
        rts      
	.page   
	.subttl 'rel1.src'          
;***********************************
;*
;*     routine: nxtrec
;*
;*
;*
;*
;*
;***********************************
nxtrec           
        lda  #ovrflo     
        jsr  clrflg      

        lda  #lrf        
        jsr  tstflg      
        bne  nxtr40      

        ldx  lindx       
        inc  recl,x     ;  goto next record #
        bne  nxtr15      
        inc  rech,x      

nxtr15           
        ldx  lindx       
        lda  nr,x        
        beq  nxtr45     ; there is a nr

        jsr  getpnt     ;  get pointer
        ldx  lindx      ; test if same buffer
        cmp  nr,x        
        bcc  nxtr20     ;  yes, bt<nr

        jsr  nrbuf      ;  no,next buffer

nxtr20  ldx  lindx       
        lda  nr,x        
        jsr  setpnt     ; advance to next rec
        lda  (buftab,x) ; read 1st dat byte

        sta  data       ; save for read channel
        lda  #ovrflo     
        jsr  clrflg     ;  clear
; the overflow flag
        jsr  addnr      ; advance nr
nxout            
        pha      
        bcc  nxtr30     ; no block boundary

        lda  #0          
        jsr  drdbyt     ; check track link
        bne  nxtr30     ; not last block

        pla      
        cmp  #2          
        beq  nxtr50      
nxtr45           
        lda  #lrf        
        jsr  setflg      
nxtr40           
        jsr  getpre      
        lda  buftab,x    
        sta  lstchr,y    
        lda  #cr         
        sta  data        
        rts      

nxtr50           
        jsr  nxtr35      
        ldx  lindx       
        lda  #0          
        sta  nr,x        
        rts      
nxtr30           
        pla      
nxtr35           
        ldx  lindx       
;*
        sta  nr,x        
        jmp  setlst      
;*
;*
;**********************************
;*
;*
;*  nrbuf
;*
;*
;********************************
;*
;*
nrbuf           	; read trk,sec link
        jsr  setdrn      
        jsr  rdlnk       

        jsr  gaflgs     ; test if dirty
        bvc  nrbu50     ; clean, dont write out

        jsr  wrtout     ; dirty, write out
        jsr  dblbuf     ; toggle active buffer

        lda  #2          
        jsr  setpnt      
        jsr  tstwrt     ; test if lstjob is wrt
        bne  nrbu20     ; not a write,buffer ok

        jsr  rdab       ; read in needed buffer
        jmp  watjob     ; wait around till done

nrbu50  jsr  dblbuf     ; toggle act buf
        jsr  tstwrt     ; was lstjob a wrt?
        bne  nrbu70     ; not a write

        jsr  rdab       ; read in needed buffer
        jsr  watjob     ; wait till done

nrbu70          	; read trk,sec link
        jsr  rdlnk      ; to do a read ahead

        lda  track      ; test if last buffer
        beq  nrbu20     ; yes,no dbl buff todo

        jsr  dblbuf     ; start read job on the
        jsr  rdab       ; inactive buffer
        jsr  dblbuf      

nrbu20  rts      
	.page  
	.subttl 'rel2.src'          
;**********************************
;*
;* relput
;*
;*
;**********************************
;*
;*
relput  jsr  sdirty     ; write data to buffer
        jsr  getact      
        asl  a   
        tax      
        lda  data        
        sta  (buftab,x)          

        ldy  buftab,x   ; inc the pointer
        iny      
        bne  relp05      
        ldy  lindx       
        lda  nr,y        
        beq  relp07      

relp06           
        ldy  #2          
relp05           
        tya      
        ldy  lindx       

        cmp  nr,y       ; test if nr=pointer
        bne  relp10     ; no,set new pointer

relp07  lda  #ovrflo    ; yes,set overflow
        jmp  setflg      

relp10          	; write back new pointer
        inc  buftab,x    

        bne  relp20     ; test if =0
        jsr  nrbuf      ; prepare nxt buffer

relp20  rts      
;*
;*
;*
;*********************************
;*
;*  wrtrel
;*
;*
;*********************************
;*
;*
wrtrel           
        lda  #lrf+ovrflo ;check all flags
        jsr  tstflg      
        bne  wr50       ; some flag is set
wr10             
        lda  data       ; ready to put data
        jsr  relput      
wr20             
        lda  eoiflg      
        beq  wr40       ; eoi was sent
        rts      
wr30             
        lda  #ovrflo     
        jsr  tstflg      
        beq  wr40       ; no rec overflow
        lda  #recovf     
        sta  erword     ; set error for end of print
wr40             
        jsr  clrec      ; clear rest of record
        jsr  rd40        
        lda  erword      
        beq  wr45        
        jmp  cmderr      
wr45             
        jmp  okerr       

wr50             
        and  #lrf        
        bne  wr60       ; last rec, add
        lda  eoiflg      
        beq  wr30        
wr51             
        rts      
wr60             
        lda  data        
        pha      
        jsr  addrel     ; add to file
        pla      
        sta  data        
        lda  #lrf        
        jsr  clrflg      
        jmp  wr10        
;*
;*
;*
;********************************
;*
;*   clrec
;*
;*********************************

clrec   lda  #ovrflo    ; put 0's into rest of record
        jsr  tstflg      
        bne  clr10       

        lda  #0          
        sta  data        
        jsr  relput      

        jmp  clrec       

clr10   rts      

;*
;*
;*******************************
;*
;*   sdirty
;*
;*******************************
;*

sdirty  lda  #dyfile     
        jsr  setflg      
        jsr  gaflgs      
        ora  #$40        
        ldx  lbused      
        sta  buf0,x      
        rts      

;*
;*
;*******************************
;*
;*   cdirty
;*
;*******************************
;*

cdirty  jsr  gaflgs      
        and  #$bf        
        ldx  lbused      
        sta  buf0,x      
        rts      
        .page  
        .subttl 'rel3.src'    
;********************************
;*
;*
;* rdrel
;*
;*
;********************************
;*
;*

rdrel            
        lda  #lrf        
        jsr  tstflg      
        bne  rd05       ; no record error
;
rd10             
        jsr  getpre      
        lda  buftab,x    
        cmp  lstchr,y    
        beq  rd40        

        inc  buftab,x    
        bne  rd20        

        jsr  nrbuf       
rd15             
        jsr  getpre      
rd20             
        lda  (buftab,x)          
rd25             
        sta  chndat,y    
        lda  #rndrdy     
        sta  chnrdy,y    
        lda  buftab,x    
        cmp  lstchr,y    
        beq  rd30        
        rts      
rd30             
        lda  #rndeoi     
        sta  chnrdy,y    
        rts      
rd40             
        jsr  nxtrec      
        jsr  getpre      
        lda  data        
        jmp  rd25        

rd05             
        ldx  lindx      ; no record char set up
        lda  #cr         
        sta  chndat,x    
        lda  #rndeoi     
        sta  chnrdy,x    
;no record error
        lda  #norec      
        jsr  cmderr      
	.page   
	.subttl 'rel4.src'          
;*
;*
;**********************************
;*
;*
;*  setlst
;*
;*
;**********************************
;*
;*
setlst  ldx  lindx       
        lda  nr,x        
        sta  r1          
        dec  r1          
        cmp  #2          
        bne  setl01      
        lda  #$ff        
        sta  r1          
setl01           
        lda  rs,x        
        sta  r2          

        jsr  getpnt      
        ldx  lindx       
        cmp  r1          
        bcc  setl10      
        beq  setl10      

        jsr  dblbuf      
        jsr  fndlst      
        bcc  setl05      

        ldx  lindx       
        sta  lstchr,x    
        jmp  dblbuf      

setl05  jsr  dblbuf      
        lda  #$ff        
        sta  r1          

setl10  jsr  fndlst      
        bcs  setl40      

        jsr  getpnt      

setl40  ldx  lindx       
        sta  lstchr,x    
        rts      
;*
;*
;*
;*********************************
;*
;*
;*  fndlst
;*
;*
;*********************************
;*
;*
fndlst           
        jsr  set00       
        ldy  r1         ; offset to start at

fndl10  lda  (dirbuf),y          
        bne  fndl20      

        dey      
        cpy  #2          
        bcc  fndl30      

        dec  r2         ; limit counter
        bne  fndl10      

fndl30  dec  r2          
        clc     	;  not found here
        rts      

fndl20  tya     	; found the end char
        sec      
        rts      
	.page
	.subttl 'romsf.src'

	*=rom   	; +$300 rom patch area
cchksm  *=*+1		; <<<<< TO BE DETERMINED

	.byte '(C)1985 COMMODORE ELECTRONICS LTD., ALL RIGHTS RESERVED'

ptch0a	and  #$ff-3
	ora  tmp
	sta  dskcnt	
ptch0b	lda  pcr2	; disable SO
	and  #$ff-2
	sta  pcr2
	rts

	  ; track cutoffs	
trackn  .byte 41,31,25,18
	
worktable ; table of track densities extras for those who address tracks > 35
	.byte $60,$60,$60,$60,$60,$60,$60,$60,$60,$60
	.byte $60,$60,$60,$60,$60,$60,$60
	.byte $40,$40,$40,$40,$40,$40,$40
	.byte $20,$20,$20,$20,$20,$20
	.byte $00,$00,$00,$00,$00
	.byte $00,$00,$00,$00,$00,$00

ptch0c	sta cmd		; save off cmd for DOS
	pha
	lda #1		; trk one
	sta hdrs,x
	pla
	rts		; return

ptch0d  sei		; no irq's
	lda  pcr2
	ora  #$0e
	sta  pcr2	; enable SO
	jsr  jclear	; clear track (val in .x)
	jsr  kill	; disable write
	lda  pcr2
	and  #$ff-$0e
	sta  pcr2	; disable SO
	cli
	rts

	
*=rom+256        	; c0 patch space
	.page  
	.subttl 'romtblsf.src'        

dirtrk   .byte  18      ; directory track #
bamsiz   .byte  4       ; # bytes/track in bam
mapoff   .byte  4       ; offset of bam in sector
dsknam   .byte  $90     ; offset of disk name in bam sector
;
;   command search table
cmdtbl   .byte  'VIDMBUP&CRSN'
; validate-dir init-drive duplicate
; memory-op block-op user
; position dskcpy utlodr rename scratch new
ncmds    =*-cmdtbl       
;  jump table low
cjumpl   .byte    <verdir,<intdrv,<duplct          
	 .byte    <mem,<block,<user       
	 .byte    <record         
	 .byte    <utlodr         
	 .byte    <dskcpy         
	 .byte    <rename,<scrtch,<new    
*=cjumpl+ncmds           
;  jump table high
cjumph   .byte    >verdir,>intdrv,>duplct          
	 .byte    >mem,>block,>user       
 	 .byte    >record         
 	 .byte    >utlodr         
 	 .byte    >dskcpy         
	 .byte    >rename,>scrtch,>new    
*=cjumph+ncmds           
val=0           ; validate (verify) cmd #

; structure images for cmds
pcmd     =9      
	.byte    %01010001      ;  dskcpy
struct   =*-pcmd        	;  cmds not parsed
	.byte    %11011101      ;  rename
	.byte    %00011100      ;  scratch
	.byte    %10011110      ;  new
ldcmd    =*-struct      	;  load cmd image
	.byte    %00011100      ;  load
;            --- ---
;            pgdrpgdr
;            fs1 fs2

;   bit reps:  not pattern
;              not greater than one file 
;              not default drive(s) 
;              required filename

modlst   .byte  'RWAM'          ;  mode table
nmodes   =*-modlst       
;file type table
tplst    .byte  'DSPUL'          
typlst   .byte  'DSPUR'         ; del, seq, prog, user, relative
ntypes   =*-typlst       
tp1lst   .byte  'EERSE'          
tp2lst   .byte  'LQGRL'          
ledmsk   .byte  led0,led1        
;
; error flag vars for bit
;
er00     .byte  0        
er0      .byte  $3f      
er1      .byte  $7f      
er2      .byte  $bf      
er3      .byte  $ff      
;
numsec          		; (4) sectors/track
	 .byte    17,18,19,21     
vernum   .byte  fm4040          ; format type
nzones   .byte  4       	; # of zones

; maxtrk          		; maximum track #  +1
trknum          		; zone boundaries track numbers
	 .byte    36,31,25,18     
offset          		; for recovery
	 .byte    1,$ff,$ff,1,0   
;
bufind           
	 .byte    $03,$04,$05,$06,$07,$07         
;

;.end
	.page
	.subttl "signature.src'

; test 256k-bit of rom
; using signature generation test bits 15,  11,  8,  6
; exit if ok

signature
	php
	sei		; no irqs
	ldx  #0
	stx  $00  	; sig_lo
	stx  $01        ; sig_hi
	lda  #3		; skip checksum & signature bytes
	sta  ip		; even page
	tay		
	lda  #$80	; start at $8000
	sta  ip+1	; high order
2$	lda  (ip),y	; get a byte
	sta  $02  
	ldx  #8		; 8 bits in a byte right?
3$	lda  $02  
	and  #1		; bit 0
	sta  $03  
	lda  $01  	; get sig_hi	
	bpl  4$		; test bit 15
	
	inc  $03  
4$	ror  a 
	bcc  5$		; test bit 8

	inc  $03  	
5$      ror  a
	ror  a
	ror  a
	bcc  6$
	
	inc  $03  
6$	lda  $00  	; sig_lo
	rol  a
	rol  a
	bcc  7$		; test bit 6
	
	inc  $03  
7$	ror  $03  	; sum into carry
	rol  $00  	; carry into bit 0 low byte
	rol  $01  	; carry into bit 0 high byte

	ror  $02  	; ready for next bit
	dex
	bne  3$

	inc  ip 	
	bne  2$

	inc  ip+1	; next page
	bne  2$

	dey
	dey
	dey		; .y = 0

	lda  $00  
	cmp  signature_lo
	bne  sig_err

	lda  $01  	
	cmp  signature_hi
	bne  sig_err

	sty  $00  	; clear
	sty  $01  	
	sty  $02  	
	sty  $03  

	plp
	rts


sig_err ldx  #3		; 4 blinks
	stx  temp
	jmp  perr	; bye bye ....
killp           	; kill protection
	.nlist
; pha
; lda #1
; sta cflg2 ;tell contoller
;killp2
; lda cflg2 ;wait until he's got it
; bne killp2
;
; pla
	.list
        rts      
	.subttl 'wd1770.src'
	.page
wdtest  .macro  
	.ife <*!.$03	; lower two bits cannot be zero
	nop		; fill address error
	.endif	
	.endm

nodrrd  .macro		; read nodrv,x absolute
	.byte $bd,$ff,$00
	.endm

nodrwr  .macro		; write nodrv,x absolute
	.byte $9d,$ff,$00
	.endm
