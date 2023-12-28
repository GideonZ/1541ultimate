
library ieee;
use ieee.std_logic_1164.all;

package bootrom_pkg is
    type t_boot_data    is array(natural range <>) of std_logic_vector(31 downto 0);

    constant c_bootrom : t_boot_data(0 to 1023) := (
        X"30047073", X"00001117", X"ffc10113", X"00001197", X"5f418193", X"00000517", X"0d050513", X"30551073",
        X"34151073", X"30001073", X"30401073", X"34401073", X"32001073", X"30601073", X"b0001073", X"b8001073",
        X"b0201073", X"b8201073", X"00000093", X"00000213", X"00000293", X"00000313", X"00000393", X"00001597",
        X"b4c58593", X"00001617", X"d9c60613", X"00001697", X"d9468693", X"00d65c63", X"0005a703", X"00e62023",
        X"00458593", X"00460613", X"fedff06f", X"00001717", X"d7470713", X"00001797", X"d7078793", X"00f75863",
        X"00072023", X"00470713", X"ff5ff06f", X"00000513", X"00000593", X"088000ef", X"34051073", X"10500073",
        X"0000006f", X"00000013", X"00000013", X"00000013", X"00000013", X"00000013", X"00000013", X"00000013",
        X"00000013", X"ff810113", X"00812023", X"00912223", X"34202473", X"02044663", X"34102473", X"00041483",
        X"0034f493", X"00240413", X"34141073", X"00300413", X"00941863", X"34102473", X"00240413", X"34141073",
        X"10000437", X"04900493", X"00940fa3", X"00012403", X"00412483", X"00810113", X"30200073", X"fe010113",
        X"00812c23", X"00112e23", X"00912a23", X"01212823", X"01312623", X"01412423", X"129000ef", X"00050413",
        X"2fc000ef", X"00045663", X"00030537", X"1f0000ef", X"10020a37", X"001a4503", X"100249b7", X"10022937",
        X"0ff57513", X"24c000ef", X"0019c503", X"100264b7", X"0ff57513", X"23c000ef", X"03a00513", X"121000ef",
        X"80694503", X"0ff57513", X"228000ef", X"8064c503", X"0ff57513", X"21c000ef", X"05e00513", X"101000ef",
        X"000a00a3", X"000980a3", X"80694503", X"0ff57513", X"200000ef", X"8064c503", X"0ff57513", X"1f4000ef",
        X"05e00513", X"0d9000ef", X"00700793", X"00fa00a3", X"00f980a3", X"80694503", X"0ff57513", X"1d4000ef",
        X"8064c503", X"0ff57513", X"1c8000ef", X"05e00513", X"0ad000ef", X"0c040a63", X"02300513", X"0a1000ef",
        X"6a0000ef", X"05d000ef", X"00050413", X"230000ef", X"00100713", X"0e877463", X"100607b7", X"20e78423",
        X"00300713", X"20e78023", X"00a00713", X"20e78023", X"20078023", X"20078023", X"2007a483", X"2007a403",
        X"2007a903", X"00048513", X"1f4000ef", X"00040513", X"1ec000ef", X"00090513", X"1e4000ef", X"00001537",
        X"b1450513", X"0dc000ef", X"fff00793", X"0af40c63", X"001407b7", X"00048513", X"fff40413", X"ffe78793",
        X"10060737", X"0487fa63", X"100607b7", X"00001537", X"20078423", X"b2450513", X"0a8000ef", X"100007b7",
        X"00a7c783", X"0e07f793", X"01879793", X"4187d793", X"0207ce63", X"00090513", X"084000ef", X"00000013",
        X"00000013", X"ff9ff06f", X"00001537", X"b0450513", X"070000ef", X"f31ff06f", X"20072683", X"ffc40413",
        X"00450513", X"fed52e23", X"f9dff06f", X"00001537", X"b3050513", X"04c000ef", X"fc5ff06f", X"0fc02703",
        X"1571c7b7", X"abe78793", X"00f71e63", X"00001537", X"b3850513", X"02c000ef", X"0e002e23", X"0f802503",
        X"01c000ef", X"00001537", X"b4050513", X"014000ef", X"00000013", X"00000013", X"ff9ff06f", X"00050067",
        X"ff010113", X"00812423", X"00112623", X"00050413", X"00044503", X"00051a63", X"00c12083", X"00812403",
        X"01010113", X"00008067", X"00140413", X"730000ef", X"fe1ff06f", X"00351513", X"00b56533", X"101007b7",
        X"0ff57513", X"10a78423", X"f8000713", X"10e785a3", X"100785a3", X"00100713", X"10e785a3", X"0c800793",
        X"00000013", X"fff78793", X"fe079ce3", X"00008067", X"ff010113", X"00812423", X"00001437", X"b9440413",
        X"00455793", X"00f407b3", X"00912223", X"00050493", X"0007c503", X"00f4f493", X"00112623", X"00940433",
        X"6bc000ef", X"00044503", X"00812403", X"00c12083", X"00412483", X"01010113", X"6a40006f", X"ff010113",
        X"00812423", X"00050413", X"00855513", X"00112623", X"00912223", X"00058493", X"f99ff0ef", X"0ff47513",
        X"f91ff0ef", X"00812403", X"00c12083", X"00048513", X"00412483", X"01010113", X"f09ff06f", X"ff010113",
        X"00812423", X"00050413", X"01855513", X"00112623", X"f61ff0ef", X"01045513", X"0ff57513", X"f55ff0ef",
        X"00845513", X"0ff57513", X"f49ff0ef", X"0ff47513", X"f41ff0ef", X"00812403", X"00c12083", X"02000513",
        X"01010113", X"6180006f", X"fd010113", X"101007b7", X"02112623", X"02812423", X"02912223", X"03212023",
        X"01312e23", X"01412c23", X"01512a23", X"01612823", X"01712623", X"01812423", X"01912223", X"01a12023",
        X"00f00713", X"000056b7", X"10e78623", X"00010537", X"00000793", X"5aa68693", X"01400613", X"00179713",
        X"00d7c5b3", X"00e50733", X"00b71023", X"00178793", X"fec796e3", X"000059b7", X"00000493", X"00000413",
        X"5aa98993", X"00010b37", X"01300c93", X"00001d37", X"00001bb7", X"00001c37", X"01400a93", X"00149793",
        X"00fb07b3", X"0134c933", X"0007da03", X"01091913", X"01095913", X"03490a63", X"028cc663", X"01041513",
        X"b48d0593", X"01055513", X"eb5ff0ef", X"b4cb8593", X"00090513", X"ea9ff0ef", X"b34c0593", X"000a0513",
        X"e9dff0ef", X"00140413", X"00148493", X"fb5498e3", X"04040663", X"00001537", X"b5450513", X"dc5ff0ef",
        X"00000513", X"02c12083", X"02812403", X"02412483", X"02012903", X"01c12983", X"01812a03", X"01412a83",
        X"01012b03", X"00c12b83", X"00812c03", X"00412c83", X"00012d03", X"03010113", X"00008067", X"00001537",
        X"b6050513", X"d7dff0ef", X"00100513", X"fb9ff06f", X"ff010113", X"00812423", X"101007b7", X"00112623",
        X"00912223", X"101006b7", X"100783a3", X"00000413", X"07800793", X"10268613", X"00500593", X"00b60023",
        X"00000013", X"00000013", X"00000013", X"10d6c703", X"0ff77713", X"00070463", X"00140413", X"fff78793",
        X"fc079ee3", X"101007b7", X"1077c483", X"0ff47513", X"d81ff0ef", X"02f00513", X"464000ef", X"0ff4f493",
        X"00048513", X"d6dff0ef", X"02000513", X"450000ef", X"00848533", X"00c12083", X"00812403", X"00412483",
        X"01010113", X"00008067", X"fd010113", X"101007b7", X"02112623", X"02812423", X"02912223", X"03212023",
        X"01312e23", X"01412c23", X"01512a23", X"01612823", X"01712623", X"01812423", X"01912223", X"01a12023",
        X"00900713", X"10e78623", X"40000593", X"10b79023", X"00200693", X"10d78123", X"ffff8737", X"10e79023",
        X"10078123", X"ffffc737", X"10e79023", X"00004737", X"10078123", X"45070613", X"10c79023", X"10078123",
        X"33300513", X"10a79023", X"10078123", X"10b79023", X"10d78123", X"00100693", X"10d78123", X"10d78123",
        X"10d78123", X"10d78123", X"23300693", X"10d79023", X"10078123", X"10c79023", X"7d070713", X"10078123",
        X"10e79023", X"10078123", X"10c79023", X"10078123", X"3e800793", X"00000013", X"fff78793", X"fe079ce3",
        X"101007b7", X"10079023", X"00300713", X"10e78123", X"00a00513", X"348000ef", X"00200493", X"00000a93",
        X"00000a13", X"16800b93", X"00100c13", X"00800c93", X"fff00b13", X"03048513", X"324000ef", X"03a00513",
        X"31c000ef", X"00000993", X"00000413", X"00000913", X"0ff4fd13", X"00098593", X"00048513", X"bd9ff0ef",
        X"e31ff0ef", X"05751a63", X"00140793", X"01878c63", X"40145413", X"40898433", X"000d0a13", X"0ff47a93",
        X"00100913", X"00198993", X"03999463", X"00a00513", X"2cc000ef", X"02091863", X"fff48493", X"f9649ce3",
        X"00001537", X"b7050513", X"b59ff0ef", X"0340006f", X"00078413", X"fa1ff06f", X"fc091ae3", X"00000793",
        X"fc5ff06f", X"00001537", X"b6c50513", X"b35ff0ef", X"030a0513", X"288000ef", X"030a8513", X"280000ef",
        X"101007b7", X"40000713", X"10e79023", X"00200713", X"10e78123", X"00090863", X"000a8593", X"000a0513",
        X"b35ff0ef", X"101007b7", X"23200713", X"10e79023", X"10078123", X"40000713", X"10e79023", X"00200713",
        X"10e78123", X"02c12083", X"02812403", X"02412483", X"01c12983", X"01812a03", X"01412a83", X"01012b03",
        X"00c12b83", X"00812c03", X"00412c83", X"00012d03", X"00090513", X"02012903", X"03010113", X"00008067",
        X"fd010113", X"101007b7", X"02112623", X"02812423", X"02912223", X"03212023", X"01312e23", X"01412c23",
        X"01512a23", X"01612823", X"01712623", X"01812423", X"01912223", X"00100713", X"10e78623", X"000017b7",
        X"38878793", X"00000013", X"fff78793", X"fe079ce3", X"10100937", X"101007b7", X"00900713", X"10e78623",
        X"00300493", X"10490a13", X"00100993", X"00100a93", X"10590413", X"00500b13", X"10000bb7", X"0000cc37",
        X"02000c93", X"015a0023", X"06000693", X"08000793", X"3e800713", X"00000013", X"fff70713", X"fe071ce3",
        X"00044703", X"01879793", X"4187d793", X"0ff77713", X"0007c863", X"01871793", X"4187d793", X"0007cc63",
        X"01340023", X"fff68693", X"00068663", X"00070793", X"fc1ff06f", X"016a0023", X"01340023", X"01340023",
        X"3e800793", X"00000013", X"fff78793", X"fe079ce3", X"cd9ff0ef", X"02051a63", X"00abc783", X"0e07f793",
        X"01879793", X"4187d793", X"0007cc63", X"350c0793", X"00000013", X"fff78793", X"fe079ce3", X"119905a3",
        X"fff48493", X"f60490e3", X"02300513", X"0d0000ef", X"101007b7", X"00000513", X"10378793", X"00100613",
        X"0c800693", X"00c78023", X"0007c703", X"0ff77713", X"04070c63", X"0ff57513", X"9b9ff0ef", X"00a00513",
        X"09c000ef", X"a85ff0ef", X"04050663", X"00001537", X"b7850513", X"02812403", X"02c12083", X"02412483",
        X"02012903", X"01c12983", X"01812a03", X"01412a83", X"01012b03", X"00c12b83", X"00812c03", X"00412c83",
        X"03010113", X"8fdff06f", X"00150513", X"f8d51ce3", X"fb5ff06f", X"00001537", X"b8c50513", X"fb9ff06f",
        X"10000737", X"00c74783", X"01879693", X"00d74783", X"00e74503", X"00f74703", X"0ff7f793", X"01079793",
        X"0ff77713", X"00d7e7b3", X"0ff57513", X"00e7e7b3", X"00851513", X"00f56533", X"00008067", X"000017b7",
        X"e007a783", X"ff010113", X"00812423", X"00112623", X"00050413", X"00078463", X"000780e7", X"10000737",
        X"01274783", X"0107f793", X"fe079ce3", X"0ff47413", X"00870823", X"00c12083", X"00812403", X"01010113",
        X"00008067", X"74736554", X"4d207265", X"6c75646f", X"000a2e65", X"6c707061", X"74616369", X"0a6e6f69",
        X"00000000", X"6e6e7552", X"0a676e69", X"00000000", X"6b636f4c", X"0000000a", X"6967614d", X"000a2163",
        X"74706d45", X"00000a79", X"0000003a", X"203d2120", X"00000000", X"204d4152", X"6f727265", X"000a2e72",
        X"204d4152", X"21214b4f", X"0000000d", X"00004b4f", X"4c494146", X"00000000", X"6165520a", X"74207964",
        X"7572206f", X"656c626d", X"00000a21", X"6f6f420a", X"000a2121", X"33323130", X"37363534", X"42413938",
        X"46454443", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000",
        X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000", X"00000000"

    );
end package;
