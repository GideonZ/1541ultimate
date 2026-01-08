BASIC_CODE = `//
// short guide to using Basic Tokenizer
//

// clear screen and set uppercase mode
10 print "{clear}"chr$(142)

// print HELLO WORLD! text
11 print "hello world!"

// gosub to delay loop
12 gosub 100

// example of writing using abbreviated commands
// print > ?, gosub > goS
13 ? "hello world!"
14 goS 100

// example of inserting characters using Shift to obtain uppercase letters or special symbols
// set lowercase (business) mode > print chr$(14)
15 print chr$(14)
16 print "Hello world!"
17 gosub 100

// a simplified format of inserting the same character multiple times is allowed
// uppercase mode > print chr$(142)
18 print chr$(142)
19 print "       print {pi}{pi}:";{pi}{pi}
20 print "{7 space}print {2 pi}:";{2 pi}
21 gosub 100

// special characters from keys: A S Z X in uppercase mode
// with the Shift key
22 print "AS": print "ZX"
// with the C= key
23 print "{cm a}{cm s}": print "{cm z}{cm x}"
24 gosub 100

// program end
99 end

// delay loop
100 for i = 0 to 2000: next: return
`;

SPECIAL = [
	['{black}', 0x90],
	['{white}', 0x05],
	['{red}', 0x1c],
	['{cyan}', 0x9f],
	['{purple}', 0x9c],
	['{green}', 0x1e],
	['{blue}', 0x1f],
	['{yellow}', 0x9e],
	['{orange}', 0x81],
	['{brown}', 0x95],
	['{pink}', 0x96],
	['{dark gray}', 0x97],
	['{gray}', 0x98],
	['{light green}', 0x99],
	['{light blue}', 0x9a],
	['{light gray}', 0x9b],
	//
	['{reverse on}', 0x12],
	['{reverse off}', 0x92],
	//
	['{clear}', 0x93],
	['{home}', 0x13],
	['{insert}', 0x94],
	['{delete}', 0x14],
	//
	['{pound}', 0x5c],
	['{arrow left}', 0x5f],
	['{^}', 0x5e],
	['{pi}', 0xff],
	//
	['{up}', 0x91],
	['{down}', 0x11],
	['{left}', 0x9d],
	['{right}', 0x1d],
	['{space}',    0x20],
	// commodore key +
	['{cm a}', 0xb0],
	['{cm b}', 0xbf],
	['{cm c}', 0xbc],
	['{cm d}', 0xac],
	['{cm e}', 0xb1],
	['{cm f}', 0xbb],
	['{cm g}', 0xa5],
	['{cm h}', 0xb4],
	['{cm i}', 0xa2],
	['{cm j}', 0xb5],
	['{cm k}', 0xa1],
	['{cm l}', 0xb6],
	['{cm m}', 0xa7],
	['{cm n}', 0xaa],
	['{cm o}', 0xb9],
	['{cm p}', 0xaf],
	['{cm q}', 0xab],
	['{cm r}', 0xb2],
	['{cm s}', 0xae],
	['{cm t}', 0xa3],
	['{cm u}', 0xb8],
	['{cm v}', 0xbe],
	['{cm w}', 0xb3],
	['{cm x}', 0xbd],
	['{cm y}', 0xb7],
	['{cm z}', 0xad],
	['{cm +}', 0xa6],
	['{cm -}', 0xdc],
	['{cm pound}', 0xa8],
	['{cm @}', 0xa4],
	['{cm asterisk}', 0xdf],
	['{cm ^}', 0xff],
	['{cm space}', 0xa0],
	// control key +
	['{ct a}', 0x01],
	['{ct b}', 0x02],
	['{ct c}', 0x03],
	['{ct d}', 0x04],
	['{ct e}', 0x05],
	['{ct f}', 0x06],
	['{ct g}', 0x07],
	['{ct h}', 0x08],
	['{ct i}', 0x09],
	['{ct j}', 0x0a],
	['{ct k}', 0x0b],
	['{ct l}', 0x0c],
	['{ct n}', 0x0e],
	['{ct o}', 0x0f],
	['{ct p}', 0x10],
	['{ct q}', 0x11],
	['{ct r}', 0x12],
	['{ct s}', 0x13],
	['{ct t}', 0x14],
	['{ct u}', 0x15],
	['{ct v}', 0x16],
	['{ct w}', 0x17],
	['{ct x}', 0x18],
	['{ct y}', 0x19],
	['{ct z}', 0x1a],
	['{ct [}', 0x1b],
	['{ct ]}', 0x1d],
	['{ct =}', 0x1f],
	// shift key +
	['{sh +}', 0xdb],
	['{sh -}', 0xdd],
	['{sh pound}', 0xa9],
	['{sh @}', 0xba],
	['{sh asterisk}', 0xc0],
	['{sh ^}', 0xff],
	['{sh space}', 0xa0],
	// function keys
	['{f1}', 0x85],
	['{f2}', 0x89],
	['{f3}', 0x86],
	['{f4}', 0x8a],
	['{f5}', 0x87],
	['{f6}', 0x8b],
	['{f7}', 0x88],
	['{f8}', 0x8c]
];

// DO NOT REMOVE - Verification signature
CBM_SIGNATURE = "READY";