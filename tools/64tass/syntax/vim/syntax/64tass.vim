"
" $Id: 64tass.vim 1418 2017-03-31 16:54:52Z soci $
" 
" Place it into this folder ~/.vim/syntax/
" Enable with: set syntax=64tass
"
" Options:
" let tass64_case_sensitive = 1 " Case sensitive or not
" let tass64_6502i = 1          " Use illegal opcodes
" let tass64_65dtv02 = 1        " Use dtv opcodes
" let tass64_65c02 = 1          " Use 65C02 opcodes
" let tass64_r65c02 = 1         " Use Rockwell 65C02 opcodes
" let tass64_w65c02 = 1         " Use WDC 65C02 opcodes
" let tass64_w65816 = 1         " Use WDC 65816 opcodes
" let tass64_65el02 = 1         " Use 65EL02 opcodes
" let tass64_65ce02 = 1         " Use 65CE02 opcodes
" let tass64_4510 = 1           " Use 4510 opcodes

if version < 600
    syntax clear
elseif exists("b:current_syntax")
    finish
endif

if !exists("tass64_case_sensitive")
    syn case ignore
endif

syn cluster tass64Statements contains=tass64Mne,tass64Macro,tass64Comment
syn cluster tass64Expression contains=tass64Float,tass64Dec,tass64Hex,tass64Bin,tass64Gap,tass64String,tass64Oper1,tass64Anon,tass64Ident,tass64MacroRef,tass64Comment
syn cluster tass64Expression2 contains=tass64Oper2,tass64Comment

" Line start
syn match tass64start /^/ skipwhite nextgroup=@tass64Statements,tass64Label,tass64Array

" Labels
syn match tass64Label /\v[[:lower:][:upper:]_][[:lower:][:upper:]0-9_]*>:?|[+-]%([ \t;]@=|$)|\*%([ \t;=]@=|$)/ skipwhite contained nextgroup=tass64Assign,@tass64Statements
syn match tass64Label /\v[[:lower:][:upper:]_][[:lower:][:upper:]0-9_]*>\.@=/ contained nextgroup=tass64Oper3
syn match tass64Oper3 /\./ contained nextgroup=tass64Label

" Assignments
syn match tass64Assign /\v%([<>*.]{2}|[<>]\?|[-+%*/^&|x:])?\=/ skipwhite contained nextgroup=@tass64Expression

" Macro invocation
syn match tass64Macro /\v[#.][[:lower:][:upper:]_][[:lower:][:upper:]0-9_]*>/ skipwhite contained contains=tass64PreProc,tass64PreCondit,tass64Include,tass64Define,tass64Structure,tass64Type,tass64Debug nextgroup=@tass64Expression
syn match tass64Macro /\v[#.][[:lower:][:upper:]_][[:lower:][:upper:]0-9_]*>\.@=/ contained nextgroup=tass64Oper4
syn match tass64Macro2 /\v[[:lower:][:upper:]_][[:lower:][:upper:]0-9_]*>/ skipwhite contained nextgroup=@tass64Expression
syn match tass64Macro2 /\v[[:lower:][:upper:]_][[:lower:][:upper:]0-9_]*>\.@=/ contained nextgroup=tass64Oper4
syn match tass64Oper4 /\./ contained nextgroup=tass64Macro2

" Comment
syn keyword tass64Todo TODO FIXME XXX NOTE contained
syn match tass64Comment ';.*$' contained contains=@Spell,tass64Todo

" Multiline array
syn match tass64Array /\v[({[]/ skipwhite contained contains=tass64Oper1 nextgroup=@tass64Expression

" 6502 Mnemonics
syn match tass64Mne /\v%(adc|and|asl|bcc|bcs|beq|bit|bmi|bne|bpl|brk|bvc|bvs|clc|cld|cli|clv|cmp|cpx)>:@!/ skipwhite contained nextgroup=@tass64Expression
syn match tass64Mne /\v%(cpy|dec|dex|dey|eor|inc|inx|iny|jmp|jsr|lda|ldx|ldy|lsr|nop|ora|pha|php|pla)>:@!/ skipwhite contained nextgroup=@tass64Expression
syn match tass64Mne /\v%(plp|rol|ror|rti|rts|sbc|sec|sed|sei|sta|stx|sty|tax|tay|tsx|txa|txs|tya)>:@!/ skipwhite contained nextgroup=@tass64Expression
" 6502 Mnemonic aliases
syn match tass64Mne /\v%(bge|blt|gcc|gcs|geq|gge|glt|gmi|gne|gpl|gvc|gvs|shl|shr)>:@!/ skipwhite contained nextgroup=@tass64Expression
" 6502 registers
syn match tass64Reg /\v\.@<!<[axysp]>/ contained
" 6502i opcodes
if exists("tass64_6502i")
    syn match tass64Mne /\v%(anc|ane|arr|asr|dcp|isb|jam|lax|lds|rla|rra|sax|sbx|sha|shs|shx|shy|slo|sre)>:@!/ skipwhite contained nextgroup=@tass64Expression
    " 6502i opcode aliases
    syn match tass64Mne /\v%(ahx|alr|axs|dcm|ins|isc|lae|las|lxa|tas|xaa)>:@!/ skipwhite contained nextgroup=@tass64Expression
endif
" 65dtv02 opcodes
if exists("tass64_65dtv02")
    syn match tass64Mne /\v%(bra|sac|sir|ane|arr|asr|dcp|isb|lax|rla|rra|sax|slo|sre)>:@!/ skipwhite contained nextgroup=@tass64Expression
    " 65dtv02 opcode aliases
    syn match tass64Mne /\v%(gra|alr|dcm|ins|isc|lxa|xaa)>:@!/ skipwhite contained nextgroup=@tass64Expression
endif
" 65c02 opcodes
if exists("tass64_65c02") || exists("tass64_r65c02") || exists("tass64_w65c02") || exists("tass64_w65816") || exists("tass64_65el02")
    syn match tass64Mne /\v%(bra|phx|phy|plx|ply|stz|trb|tsb)>:@!/ skipwhite contained nextgroup=@tass64Expression
    " 65dtv02 opcode aliases
    syn match tass64Mne /\v%(clr|dea|gra|ina)>:@!/ skipwhite contained nextgroup=@tass64Expression
endif
" r65c02 opcodes
if exists("tass64_r65c02") || exists("tass64_w65c02") 
    syn match tass64Mne /\v%(bbr|bbs|rmb|smb)>:@!/ skipwhite contained nextgroup=@tass64Expression
endif
" w65c02 opcodes
if exists("tass64_w65c02") || exists("tass64_w65816")
    syn match tass64Mne /\v%(stp|wai)>:@!/ skipwhite contained nextgroup=@tass64Expression
    " w65c02 opcode aliases
    syn match tass64Mne /\vhlt>:@!/ skipwhite contained nextgroup=@tass64Expression
endif
" w65816 opcodes
if exists("tass64_w65816")
    syn match tass64Mne /\v%(brl|cop|jsl|mvn|mvp|pea|pei|per|phb|phd|phk|plb|pld|rep|rtl|sep|tcd|tcs|tdc|tsc|txy|tyx|xba|xce)>:@!/ skipwhite contained nextgroup=@tass64Expression
    " w65816 opcode aliases
    syn match tass64Mne /\v%(csp|clp|jml|swa|tad|tas|tda|tsa)>:@!/ skipwhite contained nextgroup=@tass64Expression
    " 65816 registers
    syn match tass64Reg /\v\.@<!<[dbk]>/ contained
endif
" 65el02 opcodes
if exists("tass64_65el02")
    syn match tass64Mne /\v%(div|ent|mmu|mul|nxa|nxt|pea|pei|per|phd|pld|rea|rei|rep|rer|rha|rhi|rhx|rhy|rla|rli|rlx|rly|sea|sep|stp|tad|tda|tix|trx|txi|txr|txy|tyx|wai|xba|xce|zea)>:@!/ skipwhite contained nextgroup=@tass64Expression
    " 65el02 opcode aliases
    syn match tass64Mne /\v%(clp|hlt)>:@!/ skipwhite contained nextgroup=@tass64Expression
    " 65el02 registers
    syn match tass64Reg /\v\.@<!<[dri]>/ contained
endif
" 65ce02 opcodes
if exists("tass64_65ce02") || exists("tass64_4501")
    syn match tass64Mne /\v%(bra|phx|phy|plx|ply|stz|trb|tsb|asr|asw|bsr|cle|cpz|dew|dez|inw|inz|ldz|neg|phw|phz|plz|row|see|tab|taz|tba|tsy|tys|tza)>:@!/ skipwhite contained nextgroup=@tass64Expression
    " 65ce02 opcode aliases
    syn match tass64Mne /\v%(dea|gra|ina|rtn)>:@!/ skipwhite contained nextgroup=@tass64Expression
    " 65el02 registers
    syn match tass64Reg /\v\.@<!<[bz]>/ contained
endif
" 4510 opcodes
if exists("tass64_4510")
    syn match tass64Mne /\v%(map|eom)>:@!/ skipwhite contained nextgroup=@tass64Expression
endif

" Assembler directives
syn match tass64PreProc /\v\.%(al|align|as|autsiz|bend|block|cdef|cpu)>/ contained
syn match tass64PreProc /\v\.%(databank|dpage|dsection|edef|enc|end|endp)>/ contained
syn match tass64PreProc /\v\.%(endweak|eor|for|goto|here|hidemac)>/ contained
syn match tass64PreProc /\v\.%(lbl|logical|mansiz|next)>/ contained
syn match tass64PreProc /\v\.%(offs|option|page|pend|proc|proff|pron|rept|section)>/ contained
syn match tass64PreProc /\v\.%(seed|send|showmac|var|weak|xl|xs)>/ contained
syn match tass64Define  /\v\.%(segment|macro|endm|function|endf)>/ contained
syn match tass64Include /\v\.%(include|binclude)>/ contained
syn match tass64PreCondit /\v\.%(elsif|ifmi|ifne|ifpl|else|fi|if|ifeq|endif|switch|endswitch|case|default|comment|endc|break|continue)>/ contained
syn match tass64Structure /\v\.%(union|endu|struct|ends|dstruct|dunion)>/ contained
syn match tass64Debug /\v\.%(error|cwarn|warn|cerror|check|assert)>/ contained

" Data statements
syn match tass64Type    /\v\.%(addr|binary|byte|char|dint|lint|dword|fill|long|null)>/ contained
syn match tass64Type    /\v\.%(ptext|rta|shift|shiftl|sint|text|word)>/ contained

" String escapes
syn match tass64Escapes1 /''/ contained conceal cchar='
syn match tass64Escapes2 /""/ contained conceal cchar="
syn match tass64Escapes /{[^}\"]*}/ contained
syn match tass64Escapes /{space}/ contained conceal cchar= 
syn match tass64Escapes /{cbm-9}/ contained conceal cchar=)
syn match tass64Escapes /{cbm-0}/ contained conceal cchar=0
syn match tass64Escapes /{shift-0}/ contained conceal cchar=0
syn match tass64Escapes /{shift-,}/ contained conceal cchar=<
syn match tass64Escapes /{shift-\.}/ contained conceal cchar=>
syn match tass64Escapes /{shift-\/}/ contained conceal cchar=?
syn match tass64Escapes /{shift-:}/ contained conceal cchar=[
syn match tass64Escapes /{shift-;}/ contained conceal cchar=]
if &enc == 'utf-8'
    syn match tass64Escapes "{pound}" contained conceal cchar=£
    syn match tass64Escapes "{up arrow}" contained conceal cchar=↑
    syn match tass64Escapes "{left arrow}" contained conceal cchar=←
    syn match tass64Escapes "{cbm-a}" contained conceal cchar=┌
    syn match tass64Escapes "{shift--}" contained conceal cchar=│
    syn match tass64Escapes "{shift-+}" contained conceal cchar=┼
    syn match tass64Escapes "{shift-\*}" contained conceal cchar=─
    syn match tass64Escapes "{cbm-q}" contained conceal cchar=├
    syn match tass64Escapes "{cbm-w}" contained conceal cchar=┤
    syn match tass64Escapes "{cbm-z}" contained conceal cchar=└
    syn match tass64Escapes "{cbm-s}" contained conceal cchar=┐
    syn match tass64Escapes "{cbm-x}" contained conceal cchar=┘
    syn match tass64Escapes "{cbm-e}" contained conceal cchar=┴
    syn match tass64Escapes "{cbm-r}" contained conceal cchar=┬
    syn match tass64Escapes "{cbm-d}" contained conceal cchar=▗
    syn match tass64Escapes "{cbm-f}" contained conceal cchar=▖
    syn match tass64Escapes "{cbm-c}" contained conceal cchar=▝
    syn match tass64Escapes "{cbm-v}" contained conceal cchar=▘
    syn match tass64Escapes "{cbm-b}" contained conceal cchar=▚
    syn match tass64Escapes "{cbm-k}" contained conceal cchar=▌
    syn match tass64Escapes "{cbm-j}" contained conceal cchar=▍
    syn match tass64Escapes "{cbm-h}" contained conceal cchar=▎
    syn match tass64Escapes "{cbm-g}" contained conceal cchar=▏
    syn match tass64Escapes "{cbm-m}" contained conceal cchar=▕
    syn match tass64Escapes "{cbm-t}" contained conceal cchar=▔
    syn match tass64Escapes "{cbm-i}" contained conceal cchar=▄
    syn match tass64Escapes "{cbm-o}" contained conceal cchar=▃
    syn match tass64Escapes "{cbm-p}" contained conceal cchar=▂
    syn match tass64Escapes "{cbm-@}" contained conceal cchar=▁
endif
" String formatting
syn match tass64Escapes "%%" contained conceal cchar=%
syn match tass64Escapes "%[-+' #0*]*\(\d*\|\*\)\(\.\(\d*\|\*\)\)\=[AabdxXFfeEgGcCsS]" contained

syn match tass64String  /\v[bnlsp]?'%(''|[^'])*'/ skipwhite contained contains=tass64Escapes,tass64Escapes1 nextgroup=@tass64Expression2
syn match tass64String  /\v[bnlsp]?"%(""|[^"])*"/ skipwhite contained contains=tass64Escapes,tass64Escapes2 nextgroup=@tass64Expression2

syn match tass64Delimiter /_/ contained
syn match tass64Expo    /\v[ep]/ contained
syn match tass64Bin     /\v\%%(%([01]_+)+[01]|[01])+>\.@!/ skipwhite contained contains=tass64Delimiter nextgroup=@tass64Expression2
syn match tass64Dec     /\v%(%(\d_+)+\d|\d)+>\.@!/ skipwhite contained contains=tass64Delimiter nextgroup=@tass64Expression2
syn match tass64Hex     /\v\$%(%(\x_+)+\x|\x)+>\.@!/ skipwhite contained contains=tass64Delimiter nextgroup=@tass64Expression2
syn match tass64Ident   /\v[[:lower:][:upper:]_][[:lower:][:upper:]0-9_]*>['"]@!/ skipwhite contained contains=tass64Reg,tass64Function,tass64Const nextgroup=@tass64Expression2
syn match tass64Gap     /\v\?/ skipwhite contained nextgroup=@tass64Expression2
syn match tass64Anon    /\v[-+]+%(\s*[)\]};,:]|\s*$)@=/ skipwhite contained nextgroup=@tass64Expression2
syn match tass64Float   /\v\%%(%(\.%(%([01]_+)+[01]|[01])+|%(%([01]_+)+[01]|[01])+\.\.@!%(%([01]_+)+[01]|[01])*)%([ep][+-]?%(%(\d_+)+\d|\d)+)?|%(%([01]_+)+[01]|[01])+[ep][+-]?%(%(\d_+)+\d|\d)+)/ skipwhite contained contains=tass64Expo,tass64Delimiter nextgroup=@tass64Expression2
syn match tass64Float   /\v\$%(%(\.%(%(\x_+)+\x|\x)+|%(%(\x_+)+\x|\x)+\.\.@!%(%(\x_+)+\x|\x)*)%(p[+-]?%(%(\d_+)+\d|\d)+)?|%(%(\x_+)+\x|\x)+p[+-]?%(%(\d_+)+\d|\d)+)/ skipwhite contained contains=tass64Expo,tass64Delimiter nextgroup=@tass64Expression2
syn match tass64Float   /\v%(\.%(%(\d_+)+\d|\d)+|%(%(\d_+)+\d|\d)+\.\.@!%(%(\d_+)+\d|\d)*)%([ep][+-]?%(%(\d_+)+\d|\d)+)?|%(%(\d_+)+\d|\d)+[ep][+-]?%(%(\d_+)+\d|\d)+/ skipwhite contained contains=tass64Expo,tass64Delimiter nextgroup=@tass64Expression2

" Macro parameter reference
syn match tass64MacroRef /\v\\%([@1-9]>|[[:lower:][:upper:]_][[:lower:][:upper:]0-9_]*>|\{[^}]*\})/ skipwhite contained nextgroup=@tass64Expression2

" Operators
syn match tass64Oper1   /\v[#*!~<>`^([{:]|([-+])%(\1|\s*[)\]};,:]|\s*$)@!/ skipwhite contained nextgroup=@tass64Expression
syn match tass64Oper1   /\v[)\]}]/ skipwhite contained nextgroup=@tass64Expression2

syn match tass64Oper2   /\v[<>&^|*.]{2}|[>=<!]\=|[><]\?|[-+/*%^|&,.?:<>=[(]|%(in|x)>/ skipwhite contained nextgroup=@tass64Expression
syn match tass64Oper2   /\v[<>&|*.<>]{2}\=|[><]\?\=|[-+/*%^|&:x]\=/ skipwhite contained nextgroup=@tass64Expression
syn match tass64Oper2   /\v,[xyzrsdbk]>|[\])}]/ skipwhite contained nextgroup=@tass64Expression2

" Functions
syn match tass64Function /\v\.@!<%(abs|acos|all|any|asin|atan|atan2|cbrt|ceil|cos|cosh|deg|exp|floor|format|frac|hypot|len|log|log10|pow|rad|random|range|repr|round|sign|sin|sinh|size|sort|sqrt|tan|tanh|trunc)>/ contained
syn match tass64Function /\v\.@!<%(address|bits|bool|bytes|code|dict|float|gap|int|list|str|tuple|type)>/ contained

" Predefined constants
syn match tass64Const    /\v\.@!<%(true|false|pi)>/ contained

" Colors
hi link tass64Mne       Statement
hi link tass64Reg       Statement
hi link tass64PreProc   PreProc
hi link tass64PreCondit PreCondit
hi link tass64Define    Define
hi link tass64Include   Include
hi link tass64Structure Structure
hi link tass64Type      Type
hi link tass64Todo      Todo
hi link tass64Comment   Comment
hi link tass64Bin       Number
hi link tass64Float     Float
hi link tass64Dec       Number
hi link tass64Hex       Number
hi link tass64Gap       Number
hi link tass64Const     Number
hi link tass64String    String
hi link tass64Macro     Macro
hi link tass64Macro2    Macro
hi link tass64Ident     Identifier
hi link tass64Anon      Identifier
hi link tass64MacroRef  PreProc
hi link tass64Escapes   SpecialChar
hi link tass64Escapes1  SpecialChar
hi link tass64Escapes2  SpecialChar
hi link tass64Function  Function
hi link tass64Expo      Float
hi link tass64Delimiter Delimiter
hi link tass64Debug     Debug

let b:current_syntax = "64tass"

