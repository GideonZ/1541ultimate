/*

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/
#ifndef _OPCODES_H_
#define _OPCODES_H_

#define ADR_IMPLIED   0
#define ADR_ACCU      1
#define ADR_IMMEDIATE 2
#define ADR_LONG      3
#define ADR_ADDR      4
#define ADR_ZP        5
#define ADR_LONG_X    6
#define ADR_ADDR_X    7
#define ADR_ZP_X      8
#define ADR_ADDR_X_I  9
#define ADR_ZP_X_I    10
#define ADR_ZP_S      11
#define ADR_ZP_S_I_Y  12
#define ADR_LONG_Y    13
#define ADR_ADDR_Y    14
#define ADR_ZP_Y      15
#define ADR_ZP_LI_Y   16
#define ADR_ZP_I_Y    17
#define ADR_ADDR_LI   18
#define ADR_ZP_LI     19
#define ADR_ADDR_I    20
#define ADR_ZP_I      21
#define ADR_REL_L     22
#define ADR_REL       23
#define ADR_MOVE      24

#define OPCODES_65816 111
#define OPCODES_6502 68
#define OPCODES_65C02 79
#define OPCODES_6502i 98
#define OPCODES_CPU64 81
#define OPCODES_65DTV02 89
#define ____ 0x42
// 0x42 =WDM                  
extern unsigned char c65816[];
#define MNEMONIC65816 "adcandaslbccbcsbeqbgebitbltbmibnebplbrabrkbrlbvcbvsclccldcliclvcmpcopcpxcpydeadecdexdeyeorgccgcsgeqggegltgmignegplgragvcgvsinaincinxinyjmljmpjsljsrldaldxldylsrmvnmvpnoporapeapeiperphaphbphdphkphpphxphyplaplbpldplpplxplyreprolrorrtirtlrtssbcsecsedseisepstastpstxstystzswatadtastaxtaytcdtcstdatdctrbtsatsbtsctsxtxatxstxytyatyxwaixbaxce"
//                      1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95 96 97 98 99 100101102103104105106107108109110111
extern unsigned char c6502[];
#define MNEMONIC6502 "adcandaslbccbcsbeqbgebitbltbmibnebplbrkbvcbvsclccldcliclvcmpcpxcpydecdexdeyeorgccgcsgeqggegltgmignegplgvcgvsincinxinyjmpjsrldaldxldylsrnoporaphaphpplaplprolrorrtirtssbcsecsedseistastxstytaxtaytsxtxatxstya";
//                     1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68
extern unsigned char c65c02[];
#define MNEMONIC65C02 "adcandaslbccbcsbeqbgebitbltbmibnebplbrabrkbvcbvsclccldcliclvcmpcpxcpydeadecdexdeyeorgccgcsgeqggegltgmignegplgragvcgvsinaincinxinyjmpjsrldaldxldylsrnoporaphaphpphxphyplaplpplxplyrolrorrtirtssbcsecsedseistastxstystztaxtaytrbtsbtsxtxatxstya";
//                      1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79
extern unsigned char c6502i[];
#define MNEMONIC6502i "adcahxalrancandanearraslasraxsbccbcsbeqbgebitbltbmibnebplbrkbvcbvsclccldcliclvcmpcpxcpydcmdcpdecdexdeyeorgccgcsgeqggegltgmignegplgvcgvsincinsinxinyisbiscjamjmpjsrlaelaslaxldaldsldxldylsrlxanoporaphaphpplaplprlarolrorrrartirtssaxsbcsbxsecsedseishashsshxshyslosrestastxstytastaxtaytsxtxatxstyaxaa";
//                      1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95 96 97 98
extern unsigned char ccpu64[];
#define MNEMONICCPU64 "adcandaslbccbcsbeqbgebitbltbmibnebplbrabrkbvcbvsclccldcliclvcmpcpxcpydeadecdexdeyeorgccgcsgeqggegltgmignegplgvcgvsinaincinxinyjmljmpjsljsrldaldxldylsrnoporaphaphbphpphxphyplaplbplpplxplyrolrorrtirtlrtssbcsecsedseistastxstystztaxtaytsxtxatxstya";
//                       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81
extern unsigned char c65dtv02[];
#define MNEMONIC65DTV02 "adcalrandanearraslasrbccbcsbeqbgebitbltbmibnebplbrabrkbvcbvsclccldcliclvcmpcpxcpydcmdcpdecdexdeyeorgccgcsgeqggegltgmignegplgragvcgvsincinsinxinyisbiscjmpjsrlaxldaldxldylsrlxanoporaphaphpplaplprlarolrorrrartirtssacsaxsbcsecsedseisirslosrestastxstytaxtaytsxtxatxstyaxaa";
//                        1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89

#endif
