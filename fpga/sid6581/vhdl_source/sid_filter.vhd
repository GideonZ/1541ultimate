-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2010 Gideon's Logic Architectures'
--
-------------------------------------------------------------------------------
-- 
-- Author: Gideon Zweijtzer (gideon.zweijtzer (at) gmail.com)
--
-- Note that this file is copyrighted, and is not supposed to be used in other
-- projects without written permission from the author.
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.my_math_pkg.all;
use work.io_bus_pkg.all;

library unisim;
use unisim.vcomponents.all;

entity sid_filter is
generic (
    g_divider   : natural := 221 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    io_req      : in  t_io_req := c_io_req_init;
    io_resp     : out t_io_resp;

    filt_co     : in  unsigned(10 downto 0);
    filt_res    : in  unsigned(3 downto 0);

    valid_in    : in  std_logic := '0';
    error_out   : out std_logic;     
    input       : in  signed(17 downto 0);
    high_pass   : out signed(17 downto 0);
    band_pass   : out signed(17 downto 0);
    low_pass    : out signed(17 downto 0);

    valid_out   : out std_logic );
end sid_filter;

architecture dsvf of sid_filter is
    signal filter_q : signed(17 downto 0);
    signal filter_f : signed(17 downto 0);
    signal input_sc : signed(17 downto 0);
    signal filt_ram : std_logic_vector(15 downto 0);
    signal xa           : signed(17 downto 0);
    signal xb           : signed(17 downto 0);
    signal sum_b        : signed(17 downto 0);
    signal sub_a        : signed(17 downto 0);
    signal sub_b        : signed(17 downto 0);
    signal x_reg        : signed(17 downto 0) := (others => '0');
    signal bp_reg       : signed(17 downto 0);
    signal hp_reg       : signed(17 downto 0);
    signal lp_reg       : signed(17 downto 0);
    signal temp_reg     : signed(17 downto 0);
    signal error        : std_logic := '0';
    signal divider      : integer range 0 to g_divider-1;

    signal instruction  : std_logic_vector(7 downto 0);
    type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);
    constant c_program  : t_byte_array := (X"80", X"12", X"81", X"4C", X"82", X"20");

    alias  xa_select    : std_logic is instruction(0);
    alias  xb_select    : std_logic is instruction(1);
    alias  sub_a_sel    : std_logic is instruction(2);
    alias  sub_b_sel    : std_logic is instruction(3);
    alias  sum_to_lp    : std_logic is instruction(4);
    alias  sum_to_bp    : std_logic is instruction(5);
    alias  sub_to_hp    : std_logic is instruction(6);
    alias  mult_enable  : std_logic is instruction(7);

begin
    -- Derive the actual 'f' and 'q' parameters
    i_q_table: entity work.Q_table
    port map (
        Q_reg       => filt_res,
        filter_q    => filter_q ); -- 2.16 format

    -- I prefer to infer ram than to instantiate a vendor specific
    -- block, but this is the fastest, as the sizes of the ports differ.
    -- Secondly, a nice init value can be given, for as long as we don't connect
    -- the IO bus.
    i_filt_coef: RAMB16_S9_S18
    generic map (
        INIT_00 => X"0329032803280327032703260326032503240324032303230322032203210320",
        INIT_01 => X"03320332033103300330032f032f032e032e032d032c032c032b032b032a032a",
        INIT_02 => X"033b033b033a033a033903380338033703370336033603350334033403330333",
        INIT_03 => X"034403440343034303420341034103400340033f033f033e033e033d033c033c",
        INIT_04 => X"035603550354035303510350034f034e034d034c034b03490348034703460345",
        INIT_05 => X"03680367036603650364036203610360035f035e035d035c035b035903580357",
        INIT_06 => X"037a037903780377037603750374037203710370036f036e036d036c036a0369",
        INIT_07 => X"038d038b038a038903880387038603850383038203810380037f037e037d037c",
        INIT_08 => X"03b803b603b303b003ad03aa03a703a403a2039f039c0399039603930391038e",
        INIT_09 => X"03e603e303e003dd03db03d803d503d203cf03cc03c903c703c403c103be03bb",
        INIT_0A => X"04130411040e040b04080405040203ff03fd03fa03f703f403f103ee03ec03e9",
        INIT_0B => X"0441043e043b0438043604330430042d042a042704240422041f041c04190416",
        INIT_0C => X"04aa04a3049d0496048f04880481047a0474046d0466045f04580451044b0444",
        INIT_0D => X"05170511050a050304fc04f504ee04e804e104da04d304cc04c504bf04b804b1",
        INIT_0E => X"0585057e0577057005690562055c0555054e05470540053a0533052c0525051e",
        INIT_0F => X"05f205eb05e405dd05d705d005c905c205bb05b405ae05a705a005990592058b",
        INIT_10 => X"072c0717070306ee06da06c506b1069d06880674065f064b06360622060d05f9",
        INIT_11 => X"0874085f084b08360822080d07f907e407d007bb07a70792077e076907550740",
        INIT_12 => X"09bb09a70992097e096909550940092c0917090308ee08da08c508b1089c0888",
        INIT_13 => X"0b030aee0ada0ac50ab10a9c0a880a740a5f0a4b0a360a220a0d09f909e409d0",
        INIT_14 => X"0dd30da40d760d470d190cea0cbb0c8d0c5e0c2f0c010bd20ba30b750b460b17",
        INIT_15 => X"10bd108f1060103210030fd40fa60f770f480f1a0eeb0ebc0e8e0e5f0e300e02",
        INIT_16 => X"13a81379134b131c12ed12bf129012611233120411d511a711781149111b10ec",
        INIT_17 => X"169216641635160615d815a9157a154c151d14ee14c0149114621434140513d7",
        INIT_18 => X"1b6c1b1c1acc1a7d1a2d19dd198e193e18ee189f184f17ff17b01760171116c1",
        INIT_19 => X"206620161fc71f771f271ed81e881e381de91d991d491cfa1caa1c5a1c0b1bbb",
        INIT_1A => X"26b5264f25e92582251c24b5244f23e92382231c22b5224f21e92182211c20b5",
        INIT_1B => X"2d1c2cb52c4f2be92b822b1c2ab52a4f29e92982291c28b5284f27e92782271c",
        INIT_1C => X"34d8345a33dd336032e3326631e9316b30ee30712ff42f772efa2e7d2dff2d82",
        INIT_1D => X"3caa3c2d3bb03b333ab53a3839bb393e38c1384437c7374936cc364f35d23555",
        INIT_1E => X"467d45dd453e449f43ff436042c14222418240e340443fa43f053e663dc63d27",
        INIT_1F => X"54b95381524851104fff4eee4ddd4ccc4c164b604aaa49f4493e488847d2471c",
        INIT_20 => X"4ac84a3149994901486a47d2473a46a2460b457344db4444438e42d84222416b",
        INIT_21 => X"54b55416537752d85238519950fa505a4fbb4f1c4e7d4ddd4d3e4c9f4bff4b60",
        INIT_22 => X"5d555ccc5c445bbb5b335aaa5a2159995910588857ff577756ee566655dd5555",
        INIT_23 => X"65dd655564cc644463bb633362aa62216199611060885fff5f775eee5e665ddd",
        INIT_24 => X"6e106d8e6d0b6c886c056b826aff6a7c69fa697768f4687167ee676b66e96666",
        INIT_25 => X"763e75bb753874b5743273b0732d72aa722771a47121709f701c6f996f166e93",
        INIT_26 => X"7e6b7de87d667ce37c607bdd7b5a7ad77a5579d2794f78cc784977c6774476c1",
        INIT_27 => X"8699861685938510848d840b83888305828281ff817c80fa80777ff47f717eee",
        INIT_28 => X"8f718ee38e558dc68d388caa8c1c8b8d8aff8a7189e3895588c6883887aa871c",
        INIT_29 => X"985597c6973896aa961c958d94ff947193e3935592c6923891aa911c908d8fff",
        INIT_2A => X"a138a0aaa01c9f8d9eff9e719de39d559cc69c389baa9b1c9a8d99ff997198e3",
        INIT_2B => X"aa1ca98da8ffa871a7e3a755a6c6a638a5aaa51ca48da3ffa371a2e3a255a1c6",
        INIT_2C => X"b2ffb271b1e3b154b0c6b038afaaaf1cae8dadffad71ace3ac54abc6ab38aaaa",
        INIT_2D => X"bbe3bb54bac6ba38b9aab91cb88db7ffb771b6e3b654b5c6b538b4aab41cb38d",
        INIT_2E => X"c4c6c438c3aac31cc28dc1ffc171c0e3c054bfc6bf38beaabe1cbd8dbcffbc71",
        INIT_2F => X"cdaacd1ccc8dcbffcb71cae3ca54c9c6c938c8aac81cc78dc6ffc671c5e3c554",
        INIT_30 => X"d338d2e3d28dd238d1e3d18dd138d0e3d08dd038cfe3cf8dcf38cee3ce8dce38",
        INIT_31 => X"d88dd838d7e3d78dd738d6e3d68dd638d5e3d58dd538d4e3d48dd438d3e3d38d",
        INIT_32 => X"dde3dd8ddd38dce3dc8ddc38dbe3db8ddb38dae3da8dda38d9e3d98dd938d8e3",
        INIT_33 => X"e338e2e3e28de238e1e3e18de138e0e3e08de038dfe3df8ddf38dee3de8dde38",
        INIT_34 => X"e738e6f9e6bbe67ce63ee5ffe5c0e582e543e505e4c6e488e449e40ae3cce38d",
        INIT_35 => X"eb21eae3eaa4ea65ea27e9e8e9aae96be92de8eee8afe871e832e7f4e7b5e777",
        INIT_36 => X"ef0aeeccee8dee4fee10edd2ed93ed54ed16ecd7ec99ec5aec1bebddeb9eeb60",
        INIT_37 => X"f2f4f2b5f276f238f1f9f1bbf17cf13ef0fff0c0f082f043f005efc6ef88ef49",
        INIT_38 => X"f532f510f4eef4ccf4aaf488f465f443f421f3fff3ddf3bbf399f376f354f332",
        INIT_39 => X"f754f732f710f6eef6ccf6aaf688f665f643f621f5fff5ddf5bbf599f576f554",
        INIT_3A => X"f976f954f932f910f8eef8ccf8aaf888f865f843f821f7fff7ddf7bbf799f776",
        INIT_3B => X"fb99fb76fb54fb32fb10faeefaccfaaafa88fa65fa43fa21f9fff9ddf9bbf999",
        INIT_3C => X"fcbdfcacfc9afc89fc78fc67fc56fc44fc33fc22fc11fc00fbeefbddfbccfbbb",
        INIT_3D => X"fdd0fdbffdaefd9cfd8bfd7afd69fd58fd46fd35fd24fd13fd02fcf0fcdffcce",
        INIT_3E => X"fee3fed2fec1feb0fe9efe8dfe7cfe6bfe5afe48fe37fe26fe15fe04fdf2fde1",
        INIT_3F => X"fff6ffe5ffd4ffc3ffb2ffa0ff8fff7eff6dff5cff4aff39ff28ff17ff06fef4" )
    port map (
        DOA   => open,
        DOPA  => open,
        ADDRA => std_logic_vector(io_req.address(10 downto 0)),
        CLKA  => clock,
        DIA   => io_req.data,
        DIPA  => "0",
        ENA   => io_req.write,
        SSRA  => '0',
        WEA   => io_req.write,
    
        DOB   => filt_ram,
        DOPB  => open,
        ADDRB => std_logic_vector(filt_co(10 downto 1)),
        CLKB  => clock,
        DIB   => X"0000",
        DIPB  => "00",
        ENB   => '1',
        SSRB  => '0',
        WEB   => '0' );

    process(clock)
        variable filt_f   : signed(17 downto 3);
    begin
        if rising_edge(clock) then
            filter_f <= "00" & signed(filt_ram(15 downto 0));
            io_resp <= c_io_resp_init;
            io_resp.ack <= io_req.read or io_req.write;
        end if;
    end process;
  

--    process(clock)
--        variable filt_f   : signed(17 downto 3);
--    begin
--        if rising_edge(clock) then
--            filt_f   := "000" & signed(filt_co) & "0";
--            filter_f <= '0' & (filt_f + 2) & "00";
--        end if;
--    end process;

    --input_sc <= input;
    input_sc <= shift_right(input, 1);

    -- operations to execute the filter:
    -- bp_f      = f * bp_reg      
    -- q_contrib = q * bp_reg      
    -- lp        = bp_f + lp_reg   
    -- temp      = input - lp      
    -- hp        = temp - q_contrib
    -- hp_f      = f * hp          
    -- bp        = hp_f + bp_reg   
    -- bp_reg    = bp              
    -- lp_reg    = lp              

    -- x_reg     = f * bp_reg           -- 10000000 -- 80
    -- lp_reg    = x_reg + lp_reg       -- 00010010 -- 12
    -- q_contrib = q * bp_reg           -- 10000001 -- 81
    -- temp      = input - lp           -- 00000000 -- 00 (can be merged with previous!)
    -- hp_reg    = temp - q_contrib     -- 01001100 -- 4C
    -- x_reg     = f * hp_reg           -- 10000010 -- 82
    -- bp_reg    = x_reg + bp_reg       -- 00100000 -- 20

    
    -- now perform the arithmetic
    xa    <= filter_f when xa_select='0' else filter_q;
    xb    <= bp_reg   when xb_select='0' else hp_reg;
    sum_b <= bp_reg   when xb_select='0' else lp_reg;
    sub_a <= input_sc when sub_a_sel='0' else temp_reg;
    sub_b <= lp_reg   when sub_b_sel='0' else x_reg;
    
    process(clock)
        variable x_result   : signed(35 downto 0);
        variable sum_result : signed(17 downto 0);
        variable sub_result : signed(17 downto 0);
    begin
        if rising_edge(clock) then
            x_result := xa * xb;
            if mult_enable='1' then
                x_reg <= x_result(33 downto 16);
                if (x_result(35 downto 33) /= "000") and (x_result(35 downto 33) /= "111") then
                    error <= not error;
                end if;
            end if;

            sum_result := sum_limit(x_reg, sum_b);
            temp_reg   <= sum_result;
            if sum_to_lp='1' then
                lp_reg <= sum_result;
            end if;
            if sum_to_bp='1' then
                bp_reg <= sum_result;
            end if;
            
            sub_result := sub_limit(sub_a, sub_b);
            temp_reg   <= sub_result;
            if sub_to_hp='1' then
                hp_reg <= sub_result;
            end if;

            -- control part
            instruction <= (others => '0');
            if reset='1' then
                hp_reg <= (others => '0');            
                lp_reg <= (others => '0');            
                bp_reg <= (others => '0');            
                divider <= 0;
            elsif divider = g_divider-1 then
                divider <= 0;
            else
                divider <= divider + 1;
                if divider < c_program'length then
                    instruction <= c_program(divider);
                end if;
            end if;
            if divider = c_program'length then
                valid_out <= '1';
            else
                valid_out <= '0';
            end if;
        end if;
    end process;

    high_pass <= hp_reg;
    band_pass <= bp_reg;
    low_pass  <= lp_reg;
    error_out <= error;
end dsvf;
