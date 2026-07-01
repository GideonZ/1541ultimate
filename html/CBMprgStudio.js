// CUSTOM TOKENIZER PROFILE - EXTERNAL DATA SOURCE
// ---------------------------------------------------------------------------
// This file serves as an external profile for the BASIC Tokenizer.
// Use it to define custom mnemonic sets for various BASIC versions,
// from Standard 2.0 to Expanded BASIC variants.
//
// STRUCTURE REQUIREMENTS:
// 1. Global Variables: Do NOT use 'const' or 'let' for the main data sets.
//    Assign data directly to variables to ensure global visibility.
//
// 2. Mandatory Data Sets:
//    - BASIC_CODE: A backtick-quoted string (``). Content not starting with
//      a line number is treated as informational text (instructions,
//      welcome message), while the rest is handled as BASIC code.
//    - SPECIAL:    Mappings for special characters and structural commands
//      translated into specific tokens within the 0x00-0xFF range.
//    - TOKENS:     Complete set of BASIC Instructions and Commands. Each
//      mnemonic is mapped to a unique token in the 0x00-0xFF range.
//    - TOOLTIP:    Hex-to-Description object mapping for UI tooltips.
//
// 3. READY Signature:
//    The final line MUST be: 'window.CBM_SIGNATURE = "READY";'. This signals
//    the main application that the data load is complete and safe to process.
// ---------------------------------------------------------------------------
//
// Initial welcome message and code template displayed in the Tokenizer editor window
BASIC_CODE = `!- Welcome to Ultimate BASIC Editor / Tokenizer
!- ----------------------------------------------------------
!- Current Profile: CBM prg Studio Mapping Standard Basic
!-
!- QUICK TIPS:
!- - Use lowercase for BASIC commands (e.g. print, goto).
!- - Special keys use curly braces: {clear}, {red}, {f1}, etc.
!- - Control/Commodore keys: {ct a} / {cm a}.
!- - Shift/Uppercase: Typing UPPERCASE letters in this editor
!-   is treated as Shift + Key (produces special symbols or
!-   uppercase on C64 depending on its character set mode).
!- - Multi-character shortcuts: You can use {space*5} or {**3}.
!-   These are compliant with CBM prg Studio, though
!-   alternative formats such as {5 space}, {3 asterisk},
!-   or {3 *} are also acceptable.
!- - C64 BASIC keyword abbreviations are permitted,
!-   e.g., goto > gO, print > ?, then > tH.
!-
!- IMPORTANT:
!- For mathematical operators, signs should not be placed
!- inside {}. Example: 10 PRINT"2 {^} 2 ="2^2
!-
!- COMPATIBILITY NOTE:
!- To follow CBM prg Studio standards, use ' !- ' for
!- comments.
!-
!- CONFIGURATION:
!- Learn how to load custom profiles in 'SPECIAL.js' file.
!- Path: /Flash/html/SPECIAL.js
!- ----------------------------------------------------------
!-
!- Short guide to using Basic Tokenizer.
!-

!- clear screen, set uppercase mode and white color
10 print "{clear}{uppercase}{white}";
!- print HELLO WORLD! text
11 print "hello world!"

!- gosub to delay loop
20 gosub 100

!- example of writing using abbreviated commands
!- print > ?, gosub > goS
30 ? "hello world!"
31 goS 100

!- example of inserting characters using Shift to obtain uppercase letters or special symbols
!- set lowercase (business) mode > print chr$(14)
40 print "{lowercase}Hello world!"
41 gosub 100

!- Token shortcut example: Use {153} as a substitute for the PRINT command.
50 {153} "token id replaces the keyword."
!- Use {141} as a substitute for the GOSUB command.
51 {141} 100

!- a simplified format of inserting the same character multiple times is allowed
!- uppercase mode > print chr$(142)
60 print "{uppercase}";
61 print "      print {pi}{pi}:";{pi}{pi}
62 print "99999999999999999999"
63 print "{space*6}print {pi*2}:";{pi*2}
64 print "{nine*20}"
65 gosub 100

!- special characters from keys: A S Z X in uppercase mode
!- with the Shift key
70 print "AS": print "ZX"
!- with the C= key
71 print "{cm a}{cm s}": print "{cm z}{cm x}"
72 gosub 100

!- print 38 '-' characters between '+' signs.
!- print 40 '0' characters
!- print 10 '*' characters
80 print "+{-*38}+";
81 print "{zero*40}";
82 print "{**10}"
83 gosub 100

!- colour setup
91 print "{ct 1}black","{ct 2}white","{ct 3}red","{ct 4}cyan"
92 print "{black}ctrl+1","{white}ctrl+2","{red}ctrl+3","{cyan}ctrl+4
93 print "{white}"

!- program end
99 end

!- next line and delay loop
100 print:for i = 0 to 2000: next: return
`;
//
// Special Character Mapping
SPECIAL = [
	//Column 1
	['{0}', 0x00], ['{null}', 0x00], ['{ct @}', 0x00],
	['{1}', 0x01], ['{ct a}', 0x01],
	['{2}', 0x02], ['{ct b}', 0x02],
	['{3}', 0x03], ['{ct c}', 0x03],
	['{4}', 0x04], ['{ct d}', 0x04],
	['{5}', 0x05], ['{white}', 0x05], ['{ct e}', 0x05], ['{ct 2}', 0x05],
	['{6}', 0x06], ['{ct f}', 0x06],
	['{7}', 0x07], ['{ct g}', 0x07],
	['{8}', 0x08], ['{switch off}', 0x08], ['{ct h}', 0x08],
	['{9}', 0x09], ['{switch on}', 0x09],['{ct i}', 0x09],
	['{10}', 0x0a], ['{ct j}', 0x0a],
	['{11}', 0x0b], ['{ct k}', 0x0b],
	['{12}', 0x0c], ['{ct l}', 0x0c],
	['{13}', 0x0d], ['{return}', 0x0d], ['{ct m}', 0x0d],
	['{14}', 0x0e], ['{lowercase}', 0x0e], ['{ct n}', 0x0e],
	['{15}', 0x0f], ['{ct o}', 0x0f],
	['{16}', 0x10], ['{ct p}', 0x10],
	['{17}', 0x11], ['{down}', 0x11], ['{ct q}', 0x11],
	['{18}', 0x12], ['{reverse on}', 0x12], ['{ct r}', 0x12], ['{ct 9}', 0x12],
	['{19}', 0x13], ['{home}', 0x13], ['{ct s}', 0x13],
	['{20}', 0x14], ['{delete}', 0x14], ['{ct t}', 0x14],
	['{21}', 0x15], ['{ct u}', 0x15],
	['{22}', 0x16], ['{ct v}', 0x16],
	['{23}', 0x17], ['{ct w}', 0x17],
	['{24}', 0x18], ['{ct x}', 0x18],
	['{25}', 0x19], ['{ct y}', 0x19],
	['{26}', 0x1a], ['{ct z}', 0x1a],
	['{27}', 0x1b], ['{ct [}', 0x1b],
	['{28}', 0x1c], ['{red}', 0x1c], ['{ct pound}', 0x1c], ['{ct 3}', 0x1c],
	['{29}', 0x1d], ['{right}', 0x1d], ['{ct ]}', 0x1d],
	['{30}', 0x1e], ['{green}', 0x1e], ['{ct ^}', 0x1e], ['{ct 6}', 0x1e],
	['{31}', 0x1f], ['{blue}', 0x1f], ['{ct =}', 0x1f], ['{ct 7}', 0x1f],
	//Column 2
	['{32}', 0x20], ['{space}', 0x20],
	['{33}', 0x21], ['{!}', 0x21],
	['{34}', 0x22], ['{quote}', 0x22],
	['{35}', 0x23], ['{#}', 0x23],
	['{36}', 0x24], ['{$}', 0x24],
	['{37}', 0x25], ['{%}', 0x25],
	['{38}', 0x26], ['{&}', 0x26],
	['{39}', 0x27], ["{'}", 0x27],
	['{40}', 0x28], ['{(}', 0x28],
	['{41}', 0x29], ['{)}', 0x29],
	['{42}', 0x2a], ['{*}', 0x2a], ['{asterisk}', 0x2a],
	['{43}', 0x2b], ['{+}', 0x2b],
	['{44}', 0x2c], ['{,}', 0x2c],
	['{45}', 0x2d], ['{-}', 0x2d],
	['{46}', 0x2e], ['{.}', 0x2e],
	['{47}', 0x2f], ['{/}', 0x2f],
	['{48}', 0x30], ['{zero}', 0x30],
	['{49}', 0x31], ['{one}', 0x31],
	['{50}', 0x32], ['{two}', 0x32],
	['{51}', 0x33], ['{three}', 0x33],
	['{52}', 0x34], ['{four}', 0x34],
	['{53}', 0x35], ['{five}', 0x35],
	['{54}', 0x36], ['{six}', 0x36],
	['{55}', 0x37], ['{seven}', 0x37],
	['{56}', 0x38], ['{eight}', 0x38],
	['{57}', 0x39], ['{nine}', 0x39],
	['{58}', 0x3a], ['{:}', 0x3a],
	['{59}', 0x3b], ['{;}', 0x3b],
	['{60}', 0x3c], ['{<}', 0x3c],
	['{61}', 0x3d], ['{=}', 0x3d],
	['{62}', 0x3e], ['{>}', 0x3e],
	['{63}', 0x3f], ['{?}', 0x3f],
	//Column 3
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
	//Column 4
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
	//Column 5
	['{128}', 0x80],
	['{129}', 0x81], ['{orange}', 0x81], ['{cm 1}', 0x81],
	['{130}', 0x82],
	['{131}', 0x83],
	['{132}', 0x84],
	['{133}', 0x85], ['{f1}', 0x85],
	['{134}', 0x86], ['{f3}', 0x86],
	['{135}', 0x87], ['{f5}', 0x87],
	['{136}', 0x88], ['{f7}', 0x88],
	['{137}', 0x89], ['{f2}', 0x89],
	['{138}', 0x8a], ['{f4}', 0x8a],
	['{139}', 0x8b], ['{f6}', 0x8b],
	['{140}', 0x8c], ['{f8}', 0x8c],
	['{141}', 0x8d], ['{sh return}', 0x8d],
	['{142}', 0x8e], ['{uppercase}', 0x8e],
	['{143}', 0x8f],
	['{144}', 0x90], ['{black}', 0x90], ['{ct 1}', 0x90],
	['{145}', 0x91], ['{up}', 0x91],
	['{146}', 0x92], ['{reverse off}', 0x92], ['{ct 0}', 0x92],
	['{147}', 0x93], ['{clear}', 0x93],
	['{148}', 0x94], ['{insert}', 0x94],
	['{149}', 0x95], ['{brown}', 0x95], ['{cm 2}', 0x95],
	['{150}', 0x96], ['{pink}', 0x96], ['{cm 3}', 0x96],
	['{151}', 0x97], ['{dark gray}', 0x97], ['{cm 4}', 0x97],
	['{152}', 0x98], ['{gray}', 0x98], ['{cm 5}', 0x98],
	['{153}', 0x99], ['{light green}', 0x99], ['{cm 6}', 0x99],
	['{154}', 0x9a], ['{light blue}', 0x9a], ['{cm 7}', 0x9a],
	['{155}', 0x9b], ['{light gray}', 0x9b], ['{cm 8}', 0x9b],
	['{156}', 0x9c], ['{purple}', 0x9c], ['{ct 5}', 0x9c],
	['{157}', 0x9d], ['{left}', 0x9d],
	['{158}', 0x9e], ['{yellow}', 0x9e], ['{ct 8}', 0x9e],
	['{159}', 0x9f], ['{cyan}', 0x9f], ['{ct 4}', 0x9f],
	//Column 6
	['{160}', 0xa0], ['{sh space}', 0xa0],
	['{161}', 0xa1], ['{cm k}', 0xa1],
	['{162}', 0xa2], ['{cm i}', 0xa2],
	['{163}', 0xa3], ['{cm t}', 0xa3],
	['{164}', 0xa4], ['{cm @}', 0xa4],
	['{165}', 0xa5], ['{cm g}', 0xa5],
	['{166}', 0xa6], ['{cm +}', 0xa6],
	['{167}', 0xa7], ['{cm m}', 0xa7],
	['{168}', 0xa8], ['{cm pound}', 0xa8],
	['{169}', 0xa9], ['{sh pound}', 0xa9],
	['{170}', 0xaa], ['{cm n}', 0xaa],
	['{171}', 0xab], ['{cm q}', 0xab],
	['{172}', 0xac], ['{cm d}', 0xac],
	['{173}', 0xad], ['{cm z}', 0xad],
	['{174}', 0xae], ['{cm s}', 0xae],
	['{175}', 0xaf], ['{cm p}', 0xaf],
	['{176}', 0xb0], ['{cm a}', 0xb0],
	['{177}', 0xb1], ['{cm e}', 0xb1],
	['{178}', 0xb2], ['{cm r}', 0xb2],
	['{179}', 0xb3], ['{cm w}', 0xb3],
	['{180}', 0xb4], ['{cm h}', 0xb4],
	['{181}', 0xb5], ['{cm j}', 0xb5],
	['{182}', 0xb6], ['{cm l}', 0xb6],
	['{183}', 0xb7], ['{cm y}', 0xb7],
	['{184}', 0xb8], ['{cm u}', 0xb8],
	['{185}', 0xb9], ['{cm o}', 0xb9],
	['{186}', 0xba], ['{sh @}', 0xba],
	['{187}', 0xbb], ['{cm f}', 0xbb],
	['{188}', 0xbc], ['{cm c}', 0xbc],
	['{189}', 0xbd], ['{cm x}', 0xbd],
	['{190}', 0xbe], ['{cm v}', 0xbe],
	['{191}', 0xbf], ['{cm b}', 0xbf],
	//Column 7
	['{192}', 0xc0], ['{sh asterisk}', 0xc0],
	['{193}', 0xc1], ['{A}', 0xc1],
	['{194}', 0xc2], ['{B}', 0xc2],
	['{195}', 0xc3], ['{C}', 0xc3],
	['{196}', 0xc4], ['{D}', 0xc4],
	['{197}', 0xc5], ['{E}', 0xc5],
	['{198}', 0xc6], ['{F}', 0xc6],
	['{199}', 0xc7], ['{G}', 0xc7],
	['{200}', 0xc8], ['{H}', 0xc8],
	['{201}', 0xc9], ['{I}', 0xc9],
	['{202}', 0xca], ['{J}', 0xca],
	['{203}', 0xcb], ['{K}', 0xcb],
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
	//Column 8
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
	['{255}', 0xff], ['{pi}', 0xff]
];
//
// BASIC Instructions and Commands Mapping
TOKENS = [
	['restore', 140],	['reS', 140],		['resT', 140],		['restO', 140],		['restoR', 140],
	['input#', 132],	['iN', 132],		['inP', 132],		['inpU', 132],		['inpuT', 132],
	['return', 142],	['reT', 142],		['retU', 142],		['retuR', 142],
	['verify', 149],	['vE', 149],		['veR', 149],		['verI', 149],		['veriF', 149],
	['print#', 152],	['pR', 152],		['prI', 152],		['priN', 152],		['prinT', 152],
	['right$', 201],	['rI', 201],		['riG', 201],		['rigH', 201],		['righT', 201],
	['input', 133],		//No abbreviation
	['gosub', 141],		['goS', 141],		['gosU', 141],
	['print', 153],		['?', 153],
	['close', 160],		['clO', 160],		['cloS', 160],
	['left$', 200],		['leF', 200],		['lefT', 200],
	['next', 130],		['nE', 130],		['neX', 130],
	['data', 131],		['dA', 131],
	['read', 135],		['rE', 135],
	['goto', 137],		['gO', 137],		['goT', 137],
	['stop', 144],		['sT', 144],		['stO', 144],
	['wait', 146],		['wA', 146],		['waI', 146],
	['load', 147],		['lO', 147],		['loA', 147],
	['save', 148],		['sA', 148],		['saV', 148],
	['poke', 151],		['pO', 151],		['poK', 151],
	['cont', 154],		['cO', 154],		['coN', 154],
	['list', 155],		['lI', 155],		['liS', 155],
	['open', 159],		['oP', 159],		['opE', 159],
	['tab(', 163],		['tA', 163],		['taB', 163],
	['spc(', 166],		['sP', 166],		['spC', 166],
	['then', 167],		['tH', 167],		['thE', 167],
	['step', 169],		['stE', 169],
	['peek', 194],		['pE', 194],		['peE', 194],
	['str$', 196],		['stR', 196],
	['chr$', 199],		['cH', 199],		['chR', 199],
	['mid$', 202],		['mI', 202],		['miD', 202],
	['end', 128],		['eN', 128],
	['for', 129],		['fO', 129],
	['dim', 134],		['dI', 134],
	['let', 136],		['lE', 136],
	['run', 138],		['rU', 138],
	['rem', 143],		//No abbreviation
	['def', 150],		['dE', 150],
	['clr', 156],		['cL', 156],
	['cmd', 157],		['cM', 157],
	['sys', 158],		['sY', 158],
	['get', 161],		['gE', 161],
	['new', 162],		//No abbreviation
	['not', 168],		['nO', 168],
	['and', 175],		['aN', 175],
	['sgn', 180],		['sG', 180],
	['int', 181],		//No abbreviation
	['abs', 182],		['aB', 182],
	['usr', 183],		['uS', 183],
	['fre', 184],		['fR', 184],
	['pos', 185],		//No abbreviation
	['sqr', 186],		['sQ', 186],
	['rnd', 187],		['rN', 187],
	['log', 188],		//No abbreviation
	['exp', 189],		['eX', 189],
	['cos', 190],		//No abbreviation
	['sin', 191],		['sI', 191],
	['tan', 192],		//No abbreviation
	['atn', 193],		['aT', 193],
	['len', 195],		//No abbreviation
	['val', 197],		['vA', 197],
	['asc', 198],		['aS', 198],
	['if', 139],		//No abbreviation
	['on', 145],		//No abbreviation
	['to', 164],		//No abbreviation
	['fn', 165],		//No abbreviation
	['or', 176],		//No abbreviation
	['go', 203],		//No abbreviation
	//
	['+', 170],
	['-', 171],
	['*', 172],
	['/', 173],
	['^', 174],
	['>', 177],
	['=', 178],
	['<', 179]
];
//
// PETSCII Control and Character Tooltip Mapping
// Provides descriptions for 0x00-0xFF range, including:
// Function, Key Mapping (C64 style), Type, and Input method.
TOOLTIP = {
	//Column 1
	0x00: "FUNCTION: NULL / END OF STRING\nKEY MAPPING: CONTROL @\nTYPE: INSTRUCTION (STRING TERMINATOR)\nINPUT: AS SYMBOL | CHR$(0)",
	0x01: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL A\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(1)",
	0x02: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL B\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(2)",
	0x03: "FUNCTION: STOP (FOR GETIN)\nKEY MAPPING: CONTROL C | RUN/STOP\nTYPE: INSTRUCTION (FOR GETIN)\nINPUT: AS SYMBOL | CHR$(3)",
	0x04: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL D\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(4)",
	0x05: "FUNCTION: WHITE\nKEY MAPPING: CONTROL 2 | CONTROL E\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(5)",
	0x06: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL F | CONTROL ←\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(6)",
	0x07: "FUNCTION: BELL\nKEY MAPPING: CONTROL G\nTYPE: INSTRUCTION (AUDIO ALERT: WORKS ON PRINTERS/BBS TERMINALS ONLY).\nINPUT: AS SYMBOL | CHR$(7)",
	0x08: "FUNCTION: SWITCH CHARACTER SETS DISABLE (SHIFT C=)\nKEY MAPPING: CONTROL H\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(8)",
	0x09: "FUNCTION: SWITCH CHARACTER SETS ENABLE (SHIFT C=)\nKEY MAPPING: CONTROL I\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(9)",
	0x0A: "FUNCTION: LINE FEED\nKEY MAPPING: CONTROL J\nTYPE: INSTRUCTION (PRINTER)\nINPUT: AS SYMBOL | CHR$(10)",
	0x0B: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL K\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(11)",
	0x0C: "FUNCTION: FORM FEED\nKEY MAPPING: CONTROL L\nTYPE: INSTRUCTION (PRINTER: NEW PAGE)\nINPUT: AS SYMBOL | CHR$(12)",
	0x0D: "FUNCTION: RETURN (IMMEDIATE ACTION)\nKEY MAPPING: RETURN | CONTROL M\nTYPE: INSTRUCTION (CARRIAGE RETURN: NEW LINE AND MOVE TO COLUMN 0)\nINPUT: CHR$(13)",
	0x0E: "FUNCTION: SWITCH TEXT TO LOWERCASE\nKEY MAPPING: CONTROL N\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(14)",
	0x0F: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL O\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(15)",
	0x10: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL P\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(16)",
	0x11: "FUNCTION: CURSOR DOWN\nKEY MAPPING: CURSOR DOWN | CONTROL Q\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(17)",
	0x12: "FUNCTION: REVERSE ON\nKEY MAPPING: CONTROL 9 | CONTROL R\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(18)",
	0x13: "FUNCTION: HOME\nKEY MAPPING: HOME | CONTROL S\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(19)",
	0x14: "FUNCTION: DELETE\nKEY MAPPING: DELETE | CONTROL T\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(20)",
	0x15: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL U\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(21)",
	0x16: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL V\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(22)",
	0x17: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL W\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(23)",
	0x18: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL X\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(24)",
	0x19: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL Y\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(25)",
	0x1A: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL Z\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(26)",
	0x1B: "FUNCTION: UNDEFINED (NO ACTION)\nKEY MAPPING: CONTROL [\nTYPE: UNDEFINED (NO ACTION)\nINPUT: AS SYMBOL | CHR$(27)",
	0x1C: "FUNCTION: RED\nKEY MAPPING: CONTROL 3 | CONTROL £\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(28)",
	0x1D: "FUNCTION: CURSOR RIGHT\nKEY MAPPING: CURSOR RIGHT | CONTROL ]\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(29)",
	0x1E: "FUNCTION: GREEN\nKEY MAPPING: CONTROL 6 | CONTROL ↑\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(30)",
	0x1F: "FUNCTION: BLUE\nKEY MAPPING: CONTROL 7 | CONTROL =\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(31)",
	//Column 2
	0x20: "FUNCTION: GRAPHIC SYMBOL | SPACE\nKEY MAPPING: SPACE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(32)",
	0x21: "FUNCTION: GRAPHIC SYMBOL | !\nKEY MAPPING: SHIFT 1\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(33)",
	0x22: "FUNCTION: GRAPHIC SYMBOL | QUOTE\nKEY MAPPING: SHIFT 2\nTYPE: CHARACTER\nINPUT: AS SYMBOL (STRING DELIMITER) | CHR$(34)",
	0x23: "FUNCTION: GRAPHIC SYMBOL | #\nKEY MAPPING: SHIFT 3\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(35)",
	0x24: "FUNCTION: GRAPHIC SYMBOL | $\nKEY MAPPING: SHIFT 4\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(36)",
	0x25: "FUNCTION: GRAPHIC SYMBOL | %\nKEY MAPPING: SHIFT 5\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(37)",
	0x26: "FUNCTION: GRAPHIC SYMBOL | &\nKEY MAPPING: SHIFT 6\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(38)",
	0x27: "FUNCTION: GRAPHIC SYMBOL | '\nKEY MAPPING: SHIFT 7\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(39)",
	0x28: "FUNCTION: GRAPHIC SYMBOL | (\nKEY MAPPING: SHIFT 8\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(40)",
	0x29: "FUNCTION: GRAPHIC SYMBOL | )\nKEY MAPPING: SHIFT 9\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(41)",
	0x2A: "FUNCTION: GRAPHIC SYMBOL | *\nKEY MAPPING: *\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(42)",
	0x2B: "FUNCTION: GRAPHIC SYMBOL | +\nKEY MAPPING: +\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(43)",
	0x2C: "FUNCTION: GRAPHIC SYMBOL | ,\nKEY MAPPING: ,\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(44)",
	0x2D: "FUNCTION: GRAPHIC SYMBOL | -\nKEY MAPPING: -\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(45)",
	0x2E: "FUNCTION: GRAPHIC SYMBOL | .\nKEY MAPPING: .\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(46)",
	0x2F: "FUNCTION: GRAPHIC SYMBOL | /\nKEY MAPPING: /\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(47)",
	0x30: "FUNCTION: GRAPHIC SYMBOL | 0\nKEY MAPPING: 0\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(48)",
	0x31: "FUNCTION: GRAPHIC SYMBOL | 1\nKEY MAPPING: 1\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(49)",
	0x32: "FUNCTION: GRAPHIC SYMBOL | 2\nKEY MAPPING: 2\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(50)",
	0x33: "FUNCTION: GRAPHIC SYMBOL | 3\nKEY MAPPING: 3\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(51)",
	0x34: "FUNCTION: GRAPHIC SYMBOL | 4\nKEY MAPPING: 4\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(52)",
	0x35: "FUNCTION: GRAPHIC SYMBOL | 5\nKEY MAPPING: 5\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(53)",
	0x36: "FUNCTION: GRAPHIC SYMBOL | 6\nKEY MAPPING: 6\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(54)",
	0x37: "FUNCTION: GRAPHIC SYMBOL | 7\nKEY MAPPING: 7\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(55)",
	0x38: "FUNCTION: GRAPHIC SYMBOL | 8\nKEY MAPPING: 8\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(56)",
	0x39: "FUNCTION: GRAPHIC SYMBOL | 9\nKEY MAPPING: 9\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(57)",
	0x3A: "FUNCTION: GRAPHIC SYMBOL | :\nKEY MAPPING: :\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(58)",
	0x3B: "FUNCTION: GRAPHIC SYMBOL | ;\nKEY MAPPING: ;\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(59)",
	0x3C: "FUNCTION: GRAPHIC SYMBOL | <\nKEY MAPPING: SHIFT , | C= ,\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(60)",
	0x3D: "FUNCTION: GRAPHIC SYMBOL | =\nKEY MAPPING: =\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(61)",
	0x3E: "FUNCTION: GRAPHIC SYMBOL | >\nKEY MAPPING: SHIFT . | C= .\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(62)",
	0x3F: "FUNCTION: GRAPHIC SYMBOL | ?\nKEY MAPPING: SHIFT / | C= /\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(63)",
	//Column 3
	0x40: "FUNCTION: GRAPHIC SYMBOL | @\nKEY MAPPING: @\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(64)",
	0x41: "FUNCTION: GRAPHIC SYMBOL | A\nKEY MAPPING: A\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(65)",
	0x42: "FUNCTION: GRAPHIC SYMBOL | B\nKEY MAPPING: B\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(66)",
	0x43: "FUNCTION: GRAPHIC SYMBOL | C\nKEY MAPPING: C\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(67)",
	0x44: "FUNCTION: GRAPHIC SYMBOL | D\nKEY MAPPING: D\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(68)",
	0x45: "FUNCTION: GRAPHIC SYMBOL | E\nKEY MAPPING: E\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(69)",
	0x46: "FUNCTION: GRAPHIC SYMBOL | F\nKEY MAPPING: F\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(70)",
	0x47: "FUNCTION: GRAPHIC SYMBOL | G\nKEY MAPPING: G\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(71)",
	0x48: "FUNCTION: GRAPHIC SYMBOL | H\nKEY MAPPING: H\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(72)",
	0x49: "FUNCTION: GRAPHIC SYMBOL | I\nKEY MAPPING: I\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(73)",
	0x4A: "FUNCTION: GRAPHIC SYMBOL | J\nKEY MAPPING: J\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(74)",
	0x4B: "FUNCTION: GRAPHIC SYMBOL | K\nKEY MAPPING: K\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(75)",
	0x4C: "FUNCTION: GRAPHIC SYMBOL | L\nKEY MAPPING: L\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(76)",
	0x4D: "FUNCTION: GRAPHIC SYMBOL | M\nKEY MAPPING: M\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(77)",
	0x4E: "FUNCTION: GRAPHIC SYMBOL | N\nKEY MAPPING: N\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(78)",
	0x4F: "FUNCTION: GRAPHIC SYMBOL | O\nKEY MAPPING: O\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(79)",
	0x50: "FUNCTION: GRAPHIC SYMBOL | P\nKEY MAPPING: P\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(80)",
	0x51: "FUNCTION: GRAPHIC SYMBOL | Q\nKEY MAPPING: Q\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(81)",
	0x52: "FUNCTION: GRAPHIC SYMBOL | R\nKEY MAPPING: R\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(82)",
	0x53: "FUNCTION: GRAPHIC SYMBOL | S\nKEY MAPPING: S\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(83)",
	0x54: "FUNCTION: GRAPHIC SYMBOL | T\nKEY MAPPING: T\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(84)",
	0x55: "FUNCTION: GRAPHIC SYMBOL | U\nKEY MAPPING: U\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(85)",
	0x56: "FUNCTION: GRAPHIC SYMBOL | V\nKEY MAPPING: V\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(86)",
	0x57: "FUNCTION: GRAPHIC SYMBOL | W\nKEY MAPPING: W\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(87)",
	0x58: "FUNCTION: GRAPHIC SYMBOL | X\nKEY MAPPING: X\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(88)",
	0x59: "FUNCTION: GRAPHIC SYMBOL | Y\nKEY MAPPING: Y\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(89)",
	0x5A: "FUNCTION: GRAPHIC SYMBOL | Z\nKEY MAPPING: Z\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(90)",
	0x5B: "FUNCTION: GRAPHIC SYMBOL | [\nKEY MAPPING: [ | SHIFT :\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(91)",
	0x5C: "FUNCTION: GRAPHIC SYMBOL | £\nKEY MAPPING: £\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(92)",
	0x5D: "FUNCTION: GRAPHIC SYMBOL | ]\nKEY MAPPING: ] | SHIFT ;\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(93)",
	0x5E: "FUNCTION: GRAPHIC SYMBOL | ↑\nKEY MAPPING: ↑\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(94)",
	0x5F: "FUNCTION: GRAPHIC SYMBOL | ←\nKEY MAPPING: ← | SHIFT ← | C= ←\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(95)",
	//Column 4
	0x60: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xC0\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(96)",
	0x61: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xC1\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(97)",
	0x62: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xC2\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(98)",
	0x63: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xC3\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(99)",
	0x64: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xC4\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(100)",
	0x65: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xC5\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(101)",
	0x66: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xC6\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(102)",
	0x67: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xC7\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(103)",
	0x68: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xC8\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(104)",
	0x69: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xC9\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(105)",
	0x6A: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xCA\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(106)",
	0x6B: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xCB\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(107)",
	0x6C: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xCC\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(108)",
	0x6D: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xCD\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(109)",
	0x6E: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xCE\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(110)",
	0x6F: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xCF\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(111)",
	0x70: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xD0\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(112)",
	0x71: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xD1\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(113)",
	0x72: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xD2\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(114)",
	0x73: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xD3\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(115)",
	0x74: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xD4\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(116)",
	0x75: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xD5\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(117)",
	0x76: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xD6\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(118)",
	0x77: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xD7\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(119)",
	0x78: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xD8\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(120)",
	0x79: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xD9\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(121)",
	0x7A: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xDA\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(122)",
	0x7B: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xDB\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(123)",
	0x7C: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xDC\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(124)",
	0x7D: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xDD\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(125)",
	0x7E: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xDE\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(126)",
	0x7F: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xDF\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(127)",
	//Column 5
	0x80: "FUNCTION: UNUSED\nKEY MAPPING: NONE\nTYPE: UNUSED\nINPUT: CHR$(128)\nKEYWORD TOKEN: END",
	0x81: "FUNCTION: ORANGE\nKEY MAPPING: C= 1\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(129)\nKEYWORD TOKEN: FOR",
	0x82: "FUNCTION: UNUSED\nKEY MAPPING: NONE\nTYPE: UNUSED\nINPUT: CHR$(130)\nKEYWORD TOKEN: NEXT",
	0x83: "FUNCTION: UNUSED\nKEY MAPPING: NONE\nTYPE: UNUSED\nINPUT: CHR$(131)\nKEYWORD TOKEN: DATA",
	0x84: "FUNCTION: UNUSED\nKEY MAPPING: NONE\nTYPE: UNUSED\nINPUT: CHR$(132)\nKEYWORD TOKEN: INPUT#",
	0x85: "FUNCTION: F1\nKEY MAPPING: F1 (NON-PRINTABLE)\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(133)\nKEYWORD TOKEN: INPUT",
	0x86: "FUNCTION: F3\nKEY MAPPING: F3 (NON-PRINTABLE)\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(134)\nKEYWORD TOKEN: DIM",
	0x87: "FUNCTION: F5\nKEY MAPPING: F5 (NON-PRINTABLE)\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(135)\nKEYWORD TOKEN: READ",
	0x88: "FUNCTION: F7\nKEY MAPPING: F7 (NON-PRINTABLE)\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(136)\nKEYWORD TOKEN: LET",
	0x89: "FUNCTION: F2\nKEY MAPPING: SHIFT F1 | C= F1 (NON-PRINTABLE)\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(137)\nKEYWORD TOKEN: GOTO",
	0x8A: "FUNCTION: F4\nKEY MAPPING: SHIFT F3 | C= F3 (NON-PRINTABLE)\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(138)\nKEYWORD TOKEN: RUN",
	0x8B: "FUNCTION: F6\nKEY MAPPING: SHIFT F5 | C= F5 (NON-PRINTABLE)\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(139)\nKEYWORD TOKEN: IF",
	0x8C: "FUNCTION: F8\nKEY MAPPING: SHIFT F7 | C= F7 (NON-PRINTABLE)\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(140)\nKEYWORD TOKEN: RESTORE",
	0x8D: "FUNCTION: SHIFTED RETURN\nKEY MAPPING: SHIFT RETURN | C= RETURN\nTYPE: INSTRUCTION\nINPUT: CHR$(141)\nKEYWORD TOKEN: GOSUB",
	0x8E: "FUNCTION: SWITCH TEXT TO UPPERCASE\nKEY MAPPING: NONE\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(142)\nKEYWORD TOKEN: RETURN",
	0x8F: "FUNCTION: UNUSED\nKEY MAPPING: NONE\nTYPE: UNUSED\nINPUT: CHR$(143)\nKEYWORD TOKEN: REM",
	0x90: "FUNCTION: BLACK\nKEY MAPPING: CONTROL 1\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(144)\nKEYWORD TOKEN: STOP",
	0x91: "FUNCTION: CRSR UP\nKEY MAPPING: SHIFT CRSR DOWN\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(145)\nKEYWORD TOKEN: ON",
	0x92: "FUNCTION: RVS OFF\nKEY MAPPING: CONTROL 0\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(146)\nKEYWORD TOKEN: WAIT",
	0x93: "FUNCTION: HOME\nKEY MAPPING: SHIFT HOME\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(147)\nKEYWORD TOKEN: LOAD",
	0x94: "FUNCTION: INST\nKEY MAPPING: SHIFT DEL\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(148)\nKEYWORD TOKEN: SAVE",
	0x95: "FUNCTION: BROWN\nKEY MAPPING: C= 2\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(149)\nKEYWORD TOKEN: VERIFY",
	0x96: "FUNCTION: PINK\nKEY MAPPING: C= 3\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(150)\nKEYWORD TOKEN: DEF",
	0x97: "FUNCTION: DARK GRAY\nKEY MAPPING: C= 4\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(151)\nKEYWORD TOKEN: POKE",
	0x98: "FUNCTION: GRAY\nKEY MAPPING: C= 5\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(152)\nKEYWORD TOKEN: PRINT#",
	0x99: "FUNCTION: LIGHT GREEN\nKEY MAPPING: C= 6\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(153)\nKEYWORD TOKEN: PRINT",
	0x9A: "FUNCTION: LIGHT BLUE\nKEY MAPPING: C= 7\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(154)\nKEYWORD TOKEN: CONT",
	0x9B: "FUNCTION: LIGHT GRAY\nKEY MAPPING: C= 8\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(155)\nKEYWORD TOKEN: LIST",
	0x9C: "FUNCTION: PURPLE\nKEY MAPPING: CONTROL 5\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(156)\nKEYWORD TOKEN: CLR",
	0x9D: "FUNCTION: CRSR LEFT\nKEY MAPPING: SHIFT CRSR RIGHT\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(157)\nKEYWORD TOKEN: CMD",
	0x9E: "FUNCTION: YELLOW\nKEY MAPPING: CONTROL 8\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(158)\nKEYWORD TOKEN: SYS",
	0x9F: "FUNCTION: CYAN\nKEY MAPPING: CONTROL 4\nTYPE: INSTRUCTION\nINPUT: AS SYMBOL | CHR$(159)\nKEYWORD TOKEN: OPEN",
	//Column 6
	0xA0: "FUNCTION: GRAPHIC SYMBOL | SHIFTED SPACE\nKEY MAPPING: SHIFT SPACE | C= SPACE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(160)\nKEYWORD TOKEN: CLOSE",
	0xA1: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= K\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(161)\nKEYWORD TOKEN: GET",
	0xA2: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= I\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(162)\nKEYWORD TOKEN: NEW",
	0xA3: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= T\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(163)\nKEYWORD TOKEN: TAB(",
	0xA4: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= @\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(164)\nKEYWORD TOKEN: TO",
	0xA5: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= G\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(165)\nKEYWORD TOKEN: FN",
	0xA6: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= +\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(166)\nKEYWORD TOKEN: SPC(",
	0xA7: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= M\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(167)\nKEYWORD TOKEN: THEN",
	0xA8: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= £\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(168)\nKEYWORD TOKEN: NOT",
	0xA9: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT £\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(169)\nKEYWORD TOKEN: STEP",
	0xAA: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= N\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(170)\nKEYWORD TOKEN: + (Addition)",
	0xAB: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= Q\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(171)\nKEYWORD TOKEN: − (Subtraction)",
	0xAC: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= D\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(172)\nKEYWORD TOKEN: * (Multiplication)",
	0xAD: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= Z\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(173)\nKEYWORD TOKEN: / (Division)",
	0xAE: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= S\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(174)\nKEYWORD TOKEN: ↑ (Power)",
	0xAF: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= P\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(175)\nKEYWORD TOKEN: AND",
	0xB0: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= A\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(176)\nKEYWORD TOKEN: OR",
	0xB1: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= E\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(177)\nKEYWORD TOKEN: > (Greater-than operator)",
	0xB2: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= R\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(178)\nKEYWORD TOKEN: = (Equals operator)",
	0xB3: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= W\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(179)\nKEYWORD TOKEN: < (Less-than operator) ",
	0xB4: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= H\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(180)\nKEYWORD TOKEN: SGN",
	0xB5: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= J\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(181)\nKEYWORD TOKEN: INT",
	0xB6: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= L\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(182)\nKEYWORD TOKEN: ABS",
	0xB7: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= Y\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(183)\nKEYWORD TOKEN: USR",
	0xB8: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= U\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(184)\nKEYWORD TOKEN: FRE",
	0xB9: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= O\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(185)\nKEYWORD TOKEN: POS",
	0xBA: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT @\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(186)\nKEYWORD TOKEN: SQR",
	0xBB: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= F\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(187)\nKEYWORD TOKEN: RND",
	0xBC: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= C\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(188)\nKEYWORD TOKEN: LOG",
	0xBD: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= X\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(189)\nKEYWORD TOKEN: EXP",
	0xBE: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= V\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(190)\nKEYWORD TOKEN: COS",
	0xBF: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= B\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(191)\nKEYWORD TOKEN: SIN",
	//Column 7
	0xC0: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT *\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(192)\nKEYWORD TOKEN: TAN",
	0xC1: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT A\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(193)\nKEYWORD TOKEN: ATN",
	0xC2: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT B\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(194)\nKEYWORD TOKEN: PEEK",
	0xC3: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT C\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(195)\nKEYWORD TOKEN: LEN",
	0xC4: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT D\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(196)\nKEYWORD TOKEN: STR$",
	0xC5: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT E\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(197)\nKEYWORD TOKEN: VAL",
	0xC6: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT F\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(198)\nKEYWORD TOKEN: ASC",
	0xC7: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT G\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(199)\nKEYWORD TOKEN: CHR$",
	0xC8: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT H\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(200)\nKEYWORD TOKEN: LEFT$",
	0xC9: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT I\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(201)\nKEYWORD TOKEN: RIGHT$",
	0xCA: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT J\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(202)\nKEYWORD TOKEN: MID$",
	0xCB: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT K\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(203)\nKEYWORD TOKEN: GO",
	0xCC: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT L\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(204)",
	0xCD: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT M\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(205)",
	0xCE: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT N\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(206)",
	0xCF: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT O\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(207)",
	0xD0: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT P\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(208)",
	0xD1: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT Q\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(209)",
	0xD2: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT R\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(210)",
	0xD3: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT S\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(211)",
	0xD4: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT T\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(212)",
	0xD5: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT U\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(213)",
	0xD6: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT V\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(214)",
	0xD7: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT W\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(215)",
	0xD8: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT X\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(216)",
	0xD9: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT Y\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(217)",
	0xDA: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT Z\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(218)",
	0xDB: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT +\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(219)",
	0xDC: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= -\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(220)",
	0xDD: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT -\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(221)",
	0xDE: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(222)",
	0xDF: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: C= *\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(223)",
	//Column 8
	0xE0: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xA0\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(224)",
	0xE1: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xA1\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(225)",
	0xE2: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xA2\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(226)",
	0xE3: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xA3\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(227)",
	0xE4: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xA4\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(228)",
	0xE5: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xA5\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(229)",
	0xE6: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xA6\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(230)",
	0xE7: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xA7\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(231)",
	0xE8: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xA8\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(232)",
	0xE9: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xA9\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(233)",
	0xEA: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xAA\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(234)",
	0xEB: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xAB\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(235)",
	0xEC: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xAC\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(236)",
	0xED: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xAD\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(237)",
	0xEE: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xAE\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(238)",
	0xEF: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xAF\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(239)",
	0xF0: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xB0\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(240)",
	0xF1: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xB1\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(241)",
	0xF2: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xB2\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(242)",
	0xF3: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xB3\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(243)",
	0xF4: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xB4\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(244)",
	0xF5: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xB5\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(245)",
	0xF6: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xB6\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(246)",
	0xF7: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xB7\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(247)",
	0xF8: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xB8\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(248)",
	0xF9: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xB9\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(249)",
	0xFA: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xBA\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(250)",
	0xFB: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xBB\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(251)",
	0xFC: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xBC\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(252)",
	0xFD: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xBD\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(253)",
	0xFE: "FUNCTION: GRAPHIC SYMBOL | SAME AS 0xBE\nKEY MAPPING: NONE\nTYPE: CHARACTER\nINPUT: AS SYMBOL | CHR$(254)",
	0xFF: "FUNCTION: GRAPHIC SYMBOL\nKEY MAPPING: SHIFT ↑ | C= ↑\nTYPE: CHARACTER | NUMERIC CONSTANT: π\nINPUT: AS SYMBOL | CHR$(255)"
};
//
// DO NOT REMOVE - Verification signature
CBM_SIGNATURE = "READY";