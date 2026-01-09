// Initial welcome message and code template displayed in the Tokenizer editor window
BASIC_CODE = `!- Welcome to Ultimate BASIC Editor / Tokenizer
!- ----------------------------------------------------------
!- Current Profile: CBM prg Studio Mapping Standard
!-
!- QUICK TIPS:
!- - Use lowercase for BASIC commands (e.g. print, goto).
!- - Special keys use curly braces: {clear}, {red}, {f1}, etc.
!- - Control/Commodore keys: {ct a} / {cm a}.
!- - Shift/Uppercase: Typing UPPERCASE letters in this editor
!-   is treated as Shift + Key (produces special symbols or
!-   uppercase on C64 depending on its character set mode).
!- - Multi-char shortcut: Use {5 space} or {3 asterisk}.
!-   (Note: This is an extra feature. In CBM prg Studio this
!-   would be written as {space*5} or {asterisk*3}).
!-
!- IMPORTANT:
!- For mathematical operators, signs should not be placed inside {}.
!- Example: 10 PRINT"2 {^} 2 ="2^2
!-
!- COMPATIBILITY NOTE:
!- To follow CBM prg Studio standards, use ' !- ' for
!- comments.
!-
!- CONFIGURATION:
!- Learn how to load custom profiles in 'SPECIAL.js' file.
!- ----------------------------------------------------------
!-
!- Short guide to using Basic Tokenizer.
!-

!- clear screen and set uppercase mode
10 print "{clear}"chr$(142)

!- print HELLO WORLD! text
11 print "hello world!"

!- gosub to delay loop
12 gosub 100

!- example of writing using abbreviated commands
!- print > ?, gosub > goS
13 ? "hello world!"
14 goS 100

!- example of inserting characters using Shift to obtain uppercase letters or special symbols
!- set lowercase (business) mode > print chr$(14)
15 print chr$(14)
16 print "Hello world!"
17 gosub 100

!- a simplified format of inserting the same character multiple times is allowed
!- uppercase mode > print chr$(142)
18 print chr$(142)
19 print "       print {pi}{pi}:";{pi}{pi}
20 print "{7 space}print {2 pi}:";{2 pi}
21 gosub 100

!- special characters from keys: A S Z X in uppercase mode
!- with the Shift key
22 print "AS": print "ZX"
!- with the C= key
23 print "{cm a}{cm s}": print "{cm z}{cm x}"
24 gosub 100

!- Print 38 '-' characters between '+' signs.
98 print "+{38 -}+"

!- program end
99 end

!- delay loop
100 for i = 0 to 2000: next: return
`;

// Special Character Mapping (CBM prg Studio Standard)
SPECIAL = [
  ['{0}', 0x00], ['{null}', 0x00], ['{ct @}', 0x00],
  ['{1}', 0x01], ['{ct a}', 0x01],
  ['{2}', 0x02], ['{ct b}', 0x02],
  ['{3}', 0x03], ['{ct c}', 0x03],
  ['{4}', 0x04], ['{ct d}', 0x04],
  ['{5}', 0x05], ['{white}', 0x05], ['{ct e}', 0x05],
  ['{6}', 0x06], ['{ct f}', 0x06], ['{ct arrow left}', 0x06],
  ['{7}', 0x07], ['{ct g}', 0x07],
  ['{8}', 0x08], ['{ct h}', 0x08],
  ['{9}', 0x09], ['{ct i}', 0x09],
  ['{10}', 0x0a], ['{ct j}', 0x0a],
  ['{11}', 0x0b], ['{ct k}', 0x0b],
  ['{12}', 0x0c], ['{ct l}', 0x0c],
  ['{13}', 0x0d], ['{return}', 0x0d], ['{ct m}', 0x0d],
  ['{14}', 0x0e], ['{ct n}', 0x0e],
  ['{15}', 0x0f], ['{ct o}', 0x0f],
  ['{16}', 0x10], ['{ct p}', 0x10],
  ['{17}', 0x11], ['{down}', 0x11], ['{ct q}', 0x11],
  ['{18}', 0x12], ['{reverse on}', 0x12], ['{ct r}', 0x12],
  ['{19}', 0x13], ['{home}', 0x13], ['{ct s}', 0x13],
  ['{20}', 0x14], ['{delete}', 0x14], ['{ct t}', 0x14],
  ['{21}', 0x15], ['{ct u}', 0x15],
  ['{22}', 0x16], ['{ct v}', 0x16],
  ['{23}', 0x17], ['{ct w}', 0x17],
  ['{24}', 0x18], ['{ct x}', 0x18],
  ['{25}', 0x19], ['{ct y}', 0x19],
  ['{26}', 0x1a], ['{ct z}', 0x1a],
  ['{27}', 0x1b], ['{ct [}', 0x1b],
  ['{28}', 0x1c], ['{red}', 0x1c], ['{ct pound}', 0x1c],
  ['{29}', 0x1d], ['{right}', 0x1d], ['{ct ]}', 0x1d],
  ['{30}', 0x1e], ['{green}', 0x1e], ['{ct ^}', 0x1e],
  ['{31}', 0x1f], ['{blue}', 0x1f], ['{ct =}', 0x1f],
  ['{32}', 0x20], ['{space}', 0x20],
  ['{33}', 0x21], ['{!}', 0x21],
  ['{34}', 0x22], ['{"}', 0x22],
  ['{35}', 0x23], ['{#}', 0x23],
  ['{36}', 0x24], ['{$}', 0x24],
  ['{37}', 0x25], ['{%}', 0x25],
  ['{38}', 0x26], ['{&}', 0x26],
  ['{39}', 0x27], ["{'}", 0x27],
  ['{40}', 0x28], ['{(}', 0x28],
  ['{41}', 0x29], ['{)}', 0x29],
  ['{42}', 0x2a], ['{*}', 0x2a],
  ['{43}', 0x2b], ['{+}', 0x2b],
  ['{44}', 0x2c], ['{,}', 0x2c],
  ['{45}', 0x2d], ['{-}', 0x2d],
  ['{46}', 0x2e], ['{.}', 0x2e],
  ['{47}', 0x2f], ['{/}', 0x2f],
  ['{48}', 0x30],
  ['{49}', 0x31],
  ['{50}', 0x32],
  ['{51}', 0x33],
  ['{52}', 0x34],
  ['{53}', 0x35],
  ['{54}', 0x36],
  ['{55}', 0x37],
  ['{56}', 0x38],
  ['{57}', 0x39],
  ['{58}', 0x3a], ['{:}', 0x3a],
  ['{59}', 0x3b], ['{;}', 0x3b],
  ['{60}', 0x3c], ['{<}', 0x3c],
  ['{61}', 0x3d], ['{=}', 0x3d],
  ['{62}', 0x3e], ['{>}', 0x3e],
  ['{63}', 0x3f], ['{?}', 0x3f],
  ['{64}', 0x40], ['{@}', 0x40],
  ['{65}', 0x41], ['{a}', 0x41],
  ['{66}', 0x42], ['{b}', 0x42],
  ['{67}', 0x43], ['{c}', 0x43],
  ['{68}', 0x44], ['{d}', 0x44],
  ['{69}', 0x45], ['{e}', 0x45],
  ['{70}', 0x46], ['{f}', 0x46],
  ['{71}', 0x47], ['{g}', 0x47],
  ['{72}', 0x48], ['{h}', 0x48],
  ['{73}', 0x49], ['{i}', 0x49],
  ['{74}', 0x4a], ['{j}', 0x4a],
  ['{75}', 0x4b], ['{k}', 0x4b],
  ['{76}', 0x4c], ['{l}', 0x4c],
  ['{77}', 0x4d], ['{m}', 0x4d],
  ['{78}', 0x4e], ['{n}', 0x4e],
  ['{79}', 0x4f], ['{o}', 0x4f],
  ['{80}', 0x50], ['{p}', 0x50],
  ['{81}', 0x51], ['{q}', 0x51],
  ['{82}', 0x52], ['{r}', 0x52],
  ['{83}', 0x53], ['{s}', 0x53],
  ['{84}', 0x54], ['{t}', 0x54],
  ['{85}', 0x55], ['{u}', 0x55],
  ['{86}', 0x56], ['{v}', 0x56],
  ['{87}', 0x57], ['{w}', 0x57],
  ['{88}', 0x58], ['{x}', 0x58],
  ['{89}', 0x59], ['{y}', 0x59],
  ['{90}', 0x5a], ['{z}', 0x5a],
  ['{91}', 0x5b], ['{[}', 0x5b],
  ['{92}', 0x5c], ['{pound}', 0x5c],
  ['{93}', 0x5d], ['{]}', 0x5d],
  ['{94}', 0x5e], ['{^}', 0x5e],
  ['{95}', 0x5f], ['{arrow left}', 0x5f],
  ['{96}', 0x60],
  ['{97}', 0x61],
  ['{98}', 0x62],
  ['{99}', 0x63],
  ['{100}', 0x64],
  ['{101}', 0x65],
  ['{102}', 0x66],
  ['{103}', 0x67],
  ['{104}', 0x68],
  ['{105}', 0x69],
  ['{106}', 0x6a],
  ['{107}', 0x6b],
  ['{108}', 0x6c],
  ['{109}', 0x6d],
  ['{110}', 0x6e],
  ['{111}', 0x6f],
  ['{112}', 0x70],
  ['{113}', 0x71],
  ['{114}', 0x72],
  ['{115}', 0x73],
  ['{116}', 0x74],
  ['{117}', 0x75],
  ['{118}', 0x76],
  ['{119}', 0x77],
  ['{120}', 0x78],
  ['{121}', 0x79],
  ['{122}', 0x7a],
  ['{123}', 0x7b],
  ['{124}', 0x7c],
  ['{125}', 0x7d],
  ['{126}', 0x7e],
  ['{127}', 0x7f],
  ['{128}', 0x80], // command (end)
  ['{129}', 0x81], ['{orange}', 0x81], // command (for)
  ['{130}', 0x82], // command (next)
  ['{131}', 0x83], // command (data)
  ['{132}', 0x84], // command (input#)
  ['{133}', 0x85], ['{f1}', 0x85], // command (input)
  ['{134}', 0x86], ['{f3}', 0x86], // command (dim)
  ['{135}', 0x87], ['{f5}', 0x87], // command (read)
  ['{136}', 0x88], ['{f7}', 0x88], // command (let)
  ['{137}', 0x89], ['{f2}', 0x89], // command (goto)
  ['{138}', 0x8a], ['{f4}', 0x8a], // command (run)
  ['{139}', 0x8b], ['{f6}', 0x8b], // command (if)
  ['{140}', 0x8c], ['{f8}', 0x8c], // command (restore)
  ['{141}', 0x8d], ['{sh return}', 0x8d], // command (gosub)
  ['{142}', 0x8e], // command (return)
  ['{143}', 0x8f], // command (rem)
  ['{144}', 0x90], ['{black}', 0x90], // command (stop)
  ['{145}', 0x91], ['{up}', 0x91], // command (on)
  ['{146}', 0x92], ['{reverse off}', 0x92], // command (wait)
  ['{147}', 0x93], ['{clear}', 0x93], // command (load)
  ['{148}', 0x94], ['{insert}', 0x94], // command (save)
  ['{149}', 0x95], ['{brown}', 0x95], // command (verify)
  ['{150}', 0x96], ['{pink}', 0x96], // command (def)
  ['{151}', 0x97], ['{dark gray}', 0x97], // command (poke)
  ['{152}', 0x98], ['{gray}', 0x98], // command (print#)
  ['{153}', 0x99], ['{light green}', 0x99], // command (print)
  ['{154}', 0x9a], ['{light blue}', 0x9a], // command (cont)
  ['{155}', 0x9b], ['{light gray}', 0x9b], // command (list)
  ['{156}', 0x9c], ['{purple}', 0x9c], // command (clr)
  ['{157}', 0x9d], ['{left}', 0x9d], // command (cmd)
  ['{158}', 0x9e], ['{yellow}', 0x9e], // command (sys)
  ['{159}', 0x9f], ['{cyan}', 0x9f], // command (open)
  ['{160}', 0xa0], ['{cm space}', 0xa0], ['{sh space}', 0xa0], // command (close)
  ['{161}', 0xa1], ['{cm k}', 0xa1], // command (get)
  ['{162}', 0xa2], ['{cm i}', 0xa2], // command (new)
  ['{163}', 0xa3], ['{cm t}', 0xa3], // command (tab()
  ['{164}', 0xa4], ['{cm @}', 0xa4], // command (to)
  ['{165}', 0xa5], ['{cm g}', 0xa5], // command (fn)
  ['{166}', 0xa6], ['{cm +}', 0xa6], // command (spc()
  ['{167}', 0xa7], ['{cm m}', 0xa7], // command (then)
  ['{168}', 0xa8], ['{cm pound}', 0xa8], // command (not)
  ['{169}', 0xa9], ['{sh pound}', 0xa9], // command (step)
  ['{170}', 0xaa], ['{cm n}', 0xaa], // command (+)
  ['{171}', 0xab], ['{cm q}', 0xab], // command (-)
  ['{172}', 0xac], ['{cm d}', 0xac], // command (*)
  ['{173}', 0xad], ['{cm z}', 0xad], // command (/)
  ['{174}', 0xae], ['{cm s}', 0xae], // command (^)
  ['{175}', 0xaf], ['{cm p}', 0xaf], // command (and)
  ['{176}', 0xb0], ['{cm a}', 0xb0], // command (or)
  ['{177}', 0xb1], ['{cm e}', 0xb1], // command (>)
  ['{178}', 0xb2], ['{cm r}', 0xb2], // command (=)
  ['{179}', 0xb3], ['{cm w}', 0xb3], // command (<)
  ['{180}', 0xb4], ['{cm h}', 0xb4], // command (sgn)
  ['{181}', 0xb5], ['{cm j}', 0xb5], // command (int)
  ['{182}', 0xb6], ['{cm l}', 0xb6], // command (abs)
  ['{183}', 0xb7], ['{cm y}', 0xb7], // command (usr)
  ['{184}', 0xb8], ['{cm u}', 0xb8], // command (fre)
  ['{185}', 0xb9], ['{cm o}', 0xb9], // command (pos)
  ['{186}', 0xba], ['{sh @}', 0xba], // command (sqr)
  ['{187}', 0xbb], ['{cm f}', 0xbb], // command (rnd)
  ['{188}', 0xbc], ['{cm c}', 0xbc], // command (log)
  ['{189}', 0xbd], ['{cm x}', 0xbd], // command (exp)
  ['{190}', 0xbe], ['{cm v}', 0xbe], // command (cos)
  ['{191}', 0xbf], ['{cm b}', 0xbf], // command (sin)
  ['{192}', 0xc0], ['{sh asterisk}', 0xc0], // command (tan)
  ['{193}', 0xc1], ['{A}', 0xc1], // command (atn)
  ['{194}', 0xc2], ['{B}', 0xc2], // command (peek)
  ['{195}', 0xc3], ['{C}', 0xc3], // command (len)
  ['{196}', 0xc4], ['{D}', 0xc4], // command (str$)
  ['{197}', 0xc5], ['{E}', 0xc5], // command (val)
  ['{198}', 0xc6], ['{F}', 0xc6], // command (asc)
  ['{199}', 0xc7], ['{G}', 0xc7], // command (chr$)
  ['{200}', 0xc8], ['{H}', 0xc8], // command (left$)
  ['{201}', 0xc9], ['{I}', 0xc9], // command (right$)
  ['{202}', 0xca], ['{J}', 0xca], // command (mid$)
  ['{203}', 0xcb], ['{K}', 0xcb], // command (go)
  ['{204}', 0xcc], ['{L}', 0xcc],
  ['{205}', 0xcd], ['{M}', 0xcd],
  ['{206}', 0xce], ['{N}', 0xce],
  ['{207}', 0xcf], ['{O}', 0xcf],
  ['{208}', 0xd0], ['{P}', 0xd0],
  ['{209}', 0xd1], ['{Q}', 0xd1],
  ['{210}', 0xd2], ['{R}', 0xd2],
  ['{211}', 0xd3], ['{S}', 0xd3],
  ['{212}', 0xd4], ['{T}', 0xd4],
  ['{213}', 0xd5], ['{U}', 0xd5],
  ['{214}', 0xd6], ['{V}', 0xd6],
  ['{215}', 0xd7], ['{W}', 0xd7],
  ['{216}', 0xd8], ['{X}', 0xd8],
  ['{217}', 0xd9], ['{Y}', 0xd9],
  ['{218}', 0xda], ['{Z}', 0xda],
  ['{219}', 0xdb], ['{sh +}', 0xdb],
  ['{220}', 0xdc], ['{cm -}', 0xdc],
  ['{221}', 0xdd], ['{sh -}', 0xdd],
  ['{222}', 0xde],
  ['{223}', 0xdf], ['{cm asterisk}', 0xdf],
  ['{224}', 0xe0],
  ['{225}', 0xe1],
  ['{226}', 0xe2],
  ['{227}', 0xe3],
  ['{228}', 0xe4],
  ['{229}', 0xe5],
  ['{230}', 0xe6],
  ['{231}', 0xe7],
  ['{232}', 0xe8],
  ['{233}', 0xe9],
  ['{234}', 0xea],
  ['{235}', 0xeb],
  ['{236}', 0xec],
  ['{237}', 0xed],
  ['{238}', 0xee],
  ['{239}', 0xef],
  ['{240}', 0xf0],
  ['{241}', 0xf1],
  ['{242}', 0xf2],
  ['{243}', 0xf3],
  ['{244}', 0xf4],
  ['{245}', 0xf5],
  ['{246}', 0xf6],
  ['{247}', 0xf7],
  ['{248}', 0xf8],
  ['{249}', 0xf9],
  ['{250}', 0xfa],
  ['{251}', 0xfb],
  ['{252}', 0xfc],
  ['{253}', 0xfd],
  ['{254}', 0xfe],
  ['{255}', 0xff], ['{pi}', 0xff], ['{cm ^}', 0xff], ['{sh ^}', 0xff]
];

// DO NOT REMOVE - Verification signature
CBM_SIGNATURE = "READY";