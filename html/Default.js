// Initial welcome message and code template displayed in the Tokenizer editor window
BASIC_CODE = `// Welcome to Ultimate BASIC Editor / Tokenizer
// ----------------------------------------------------------
// Current Profile: Default
//
// QUICK TIPS:
// - Use lowercase for BASIC commands (e.g. print, goto).
// - Shift/Uppercase: Typing UPPERCASE letters in this editor
//   is treated as Shift + Key (produces special symbols or
//   uppercase on C64 depending on its character set mode).
// - Numeric tokens: You can use any code directly
//   using its decimal value: {0} to {255}.
// - Named tokens: {clr}, {red}, {f1}, {up}, {down}, etc.
// - Multi-char shortcut: Use {5 space} or {space*5}, {10 160} or {**3}.
//
// IMPORTANT:
// For mathematical operators, signs should not be placed inside {}.
// Example: 10 print "2 {^} 2 ="; 2^2
//
// ADVANCED:
// This profile maps all 256 PETSCII bytes. If a named token
// is missing, simply use its number: e.g., {147} for clr.
//
// CONFIGURATION:
// Learn how to load or customize your own profiles in
// the '/Flash/html/SPECIAL.js' file.
// ----------------------------------------------------------
//
// Short guide to using Basic Tokenizer.

// Clear screen {clr} and set uppercase mode {142}
10 print "{clr}{142}";

// Print hello world! text
20 print "hello world!"

// Gosub to delay loop
30 gosub 100

// Example of writing using abbreviated commands
// print > ?, gosub > goS
40 ? "hello world!"
41 goS 100

// Set lowercase mode using code {14}
50 print "{14}Hello world!"
51 gosub 100

// Using numeric tokens and multipliers
// Set uppercase mode {142}
60 print "{142}";
// PI symbol as text {255} and mathematical constant
61 print "      print {255}{255}:";{255}{255}
62 print "{6 space}print {255*2}:";{255*2}
63 gosub 100

// Special characters using numeric tokens for A S Z X
// Using codes 193, 211, 218, 216 for Shift+Key
70 print "{193}{211}": print "{218}{216}"
// Using codes 176, 174, 173, 189 for C=+Key
71 print "{176}{174}": print "{173}{189}"
72 gosub 100

// Print 38 dashes {45} between '+' signs
// Print 40 zeros {48}
// Print 10 asterisks {42}

80 print "+{38 45}+";
81 print "{40 48}";
82 print "{42*10}"

// Program end
90 end

// Next line and delay loop
100 print:for i = 0 to 2000: next: return
`;

// Special Character Mapping
SPECIAL = [
    // Column 1
    ['{0}', 0x00],
    ['{1}', 0x01],
    ['{2}', 0x02],
    ['{3}', 0x03],
    ['{4}', 0x04],
    ['{5}', 0x05], ['{wht}', 0x05],
    ['{6}', 0x06],
    ['{7}', 0x07],
    ['{8}', 0x08],
    ['{9}', 0x09],
    ['{10}', 0x0a],
    ['{11}', 0x0b],
    ['{12}', 0x0c],
    ['{13}', 0x0d],
    ['{14}', 0x0e],
    ['{15}', 0x0f],
    ['{16}', 0x10],
    ['{17}', 0x11], ['{down}', 0x11],
    ['{18}', 0x12], ['{rvs on}', 0x12], ['{rvs}', 0x12],
    ['{19}', 0x13], ['{home}', 0x13],
    ['{20}', 0x14],
    ['{21}', 0x15],
    ['{22}', 0x16],
    ['{23}', 0x17],
    ['{24}', 0x18],
    ['{25}', 0x19],
    ['{26}', 0x1a],
    ['{27}', 0x1b],
    ['{28}', 0x1c], ['{red}', 0x1c],
    ['{29}', 0x1d], ['{rght}', 0x1d],
    ['{30}', 0x1e], ['{grn}', 0x1e],
    ['{31}', 0x1f], ['{blu}', 0x1f],
    // Column 2
    ['{32}', 0x20], ['{space}', 0x20], ['{spaces}', 0x20],
    ['{33}', 0x21],
    ['{34}', 0x22],
    ['{35}', 0x23],
    ['{36}', 0x24],
    ['{37}', 0x25],
    ['{38}', 0x26],
    ['{39}', 0x27],
    ['{40}', 0x28],
    ['{41}', 0x29],
    ['{42}', 0x2a],
    ['{43}', 0x2b],
    ['{44}', 0x2c],
    ['{45}', 0x2d],
    ['{46}', 0x2e],
    ['{47}', 0x2f],
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
    ['{58}', 0x3a],
    ['{59}', 0x3b],
    ['{60}', 0x3c],
    ['{61}', 0x3d],
    ['{62}', 0x3e],
    ['{63}', 0x3f],
    // Column 3
    ['{64}', 0x40],
    ['{65}', 0x41],
    ['{66}', 0x42],
    ['{67}', 0x43],
    ['{68}', 0x44],
    ['{69}', 0x45],
    ['{70}', 0x46],
    ['{71}', 0x47],
    ['{72}', 0x48],
    ['{73}', 0x49],
    ['{74}', 0x4a],
    ['{75}', 0x4b],
    ['{76}', 0x4c],
    ['{77}', 0x4d],
    ['{78}', 0x4e],
    ['{79}', 0x4f],
    ['{80}', 0x50],
    ['{81}', 0x51],
    ['{82}', 0x52],
    ['{83}', 0x53],
    ['{84}', 0x54],
    ['{85}', 0x55],
    ['{86}', 0x56],
    ['{87}', 0x57],
    ['{88}', 0x58],
    ['{89}', 0x59],
    ['{90}', 0x5a],
    ['{91}', 0x5b],
    ['{92}', 0x5c],
    ['{93}', 0x5d],
    ['{94}', 0x5e],
    ['{95}', 0x5f],
    // Column 4
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
    // Column 5
    ['{128}', 0x80],
    ['{129}', 0x81], ['{org}', 0x81],
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
    ['{141}', 0x8d],
    ['{142}', 0x8e],
    ['{143}', 0x8f],
    ['{144}', 0x90], ['{blk}', 0x90],
    ['{145}', 0x91], ['{up}', 0x91],
    ['{146}', 0x92], ['{rvs off}', 0x92],
    ['{147}', 0x93], ['{clr}', 0x93], ['{clear}', 0x93],
    ['{148}', 0x94],
    ['{149}', 0x95], ['{brn}', 0x95],
    ['{150}', 0x96], ['{lred}', 0x96],
    ['{151}', 0x97], ['{dgry}', 0x97], ['{gry1}', 0x97],
    ['{152}', 0x98], ['{mgry}', 0x98], ['{gry2}', 0x98],
    ['{153}', 0x99], ['{lgrn}', 0x99],
    ['{154}', 0x9a], ['{lblu}', 0x9a],
    ['{155}', 0x9b], ['{lgry}', 0x9b], ['{gry3}', 0x9b],
    ['{156}', 0x9c], ['{pur}', 0x9c],
    ['{157}', 0x9d], ['{left}', 0x9d],
    ['{158}', 0x9e], ['{yel}', 0x9e],
    ['{159}', 0x9f], ['{cyn}', 0x9f],
    // Column 6
    ['{160}', 0xa0],
    ['{161}', 0xa1],
    ['{162}', 0xa2],
    ['{163}', 0xa3],
    ['{164}', 0xa4],
    ['{165}', 0xa5],
    ['{166}', 0xa6],
    ['{167}', 0xa7],
    ['{168}', 0xa8],
    ['{169}', 0xa9],
    ['{170}', 0xaa],
    ['{171}', 0xab],
    ['{172}', 0xac],
    ['{173}', 0xad],
    ['{174}', 0xae],
    ['{175}', 0xaf],
    ['{176}', 0xb0],
    ['{177}', 0xb1],
    ['{178}', 0xb2],
    ['{179}', 0xb3],
    ['{180}', 0xb4],
    ['{181}', 0xb5],
    ['{182}', 0xb6],
    ['{183}', 0xb7],
    ['{184}', 0xb8],
    ['{185}', 0xb9],
    ['{186}', 0xba],
    ['{187}', 0xbb],
    ['{188}', 0xbc],
    ['{189}', 0xbd],
    ['{190}', 0xbe],
    ['{191}', 0xbf],
    // Column 7
    ['{192}', 0xc0],
    ['{193}', 0xc1],
    ['{194}', 0xc2],
    ['{195}', 0xc3],
    ['{196}', 0xc4],
    ['{197}', 0xc5],
    ['{198}', 0xc6],
    ['{199}', 0xc7],
    ['{200}', 0xc8],
    ['{201}', 0xc9],
    ['{202}', 0xca],
    ['{203}', 0xcb],
    ['{204}', 0xcc],
    ['{205}', 0xcd],
    ['{206}', 0xce],
    ['{207}', 0xcf],
    ['{208}', 0xd0],
    ['{209}', 0xd1],
    ['{210}', 0xd2],
    ['{211}', 0xd3],
    ['{212}', 0xd4],
    ['{213}', 0xd5],
    ['{214}', 0xd6],
    ['{215}', 0xd7],
    ['{216}', 0xd8],
    ['{217}', 0xd9],
    ['{218}', 0xda],
    ['{219}', 0xdb],
    ['{220}', 0xdc],
    ['{221}', 0xdd],
    ['{222}', 0xde],
    ['{223}', 0xdf],
    // Column 8
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
    ['{255}', 0xff]
];

// DO NOT REMOVE - Verification signature
CBM_SIGNATURE = "READY";