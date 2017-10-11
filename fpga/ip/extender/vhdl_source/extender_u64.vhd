--------------------------------------------------------------------------------
-- Entity: extender
-- Date:2017-04-22  
-- Author: Gideon     
--
-- Description: This unit is meant to fit in an external PLD for I/O extension
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity extender_u64 is
port  (
    reset_n     : in    std_logic;
    data        : inout std_logic_vector(7 downto 0);	
    read        : in    std_logic;
    write       : in    std_logic;
    address     : in    unsigned(2 downto 0);
    cia1_pa     : inout std_logic_vector(7 downto 0);
    cia1_pb     : inout std_logic_vector(7 downto 0);
    cia2_pa     : inout std_logic_vector(7 downto 0);
    cia2_pb     : inout std_logic_vector(7 downto 0);
    lightpen    : out   std_logic    
);

end entity;

architecture arch of extender_u64 is
    signal cia1_pa_t : std_logic_vector(7 downto 0) := (others => '0');
    signal cia1_pa_o : std_logic_vector(7 downto 0) := (others => '0');
    signal cia1_pb_t : std_logic_vector(7 downto 0) := (others => '0');
    signal cia1_pb_o : std_logic_vector(7 downto 0) := (others => '0');
    signal cia2_pa_t : std_logic_vector(7 downto 0) := (others => '0');
    signal cia2_pa_o : std_logic_vector(7 downto 0) := (others => '0');
    signal cia2_pb_t : std_logic_vector(7 downto 0) := (others => '0');
    signal cia2_pb_o : std_logic_vector(7 downto 0) := (others => '0');
    signal read_data : std_logic_vector(7 downto 0);
    signal read_2pa  : std_logic_vector(7 downto 0);
begin
    -- CIA1 Port A and B have pull ups on the board (joy / keyboard: read pin)
    -- CIA2 Port A:
    -- 0: VA14 (should read '1' when input, else read output)
    -- 1: VA15 (should read '1' when input, else read output)
    -- 2: UserPort pin (needs pull up), read actual pin
    -- 3: ATN Out (should read '1' when input, else read output)
    -- 4: CLOCK Out (should read '1' when input, else read output)
    -- 5: DATA Out (should read '1' when input, else read output)
    -- 6: CLOCK In - connected to CLK pin of IEC bus, reads the correct value
    -- 7: DATA In - connected to DATA pin of IEC bus, reads the correct value
    -- CIA2 Port B:
    -- 0: UserPort pin (needs pull up, read pin)
    -- 1: UserPort pin (needs pull up, read pin)
    -- 2: UserPort pin (needs pull up, read pin)
    -- 3: UserPort pin (needs pull up, read pin)
    -- 4: UserPort pin (needs pull up, read pin)
    -- 5: UserPort pin (needs pull up, read pin)
    -- 6: UserPort pin (needs pull up, read pin)
    -- 7: UserPort pin (needs pull up, read pin)

    read_2pa(0) <= cia2_pa_o(0) or not cia2_pa_t(0);
    read_2pa(1) <= cia2_pa_o(1) or not cia2_pa_t(1);
    read_2pa(2) <= cia2_pa(2);
    read_2pa(3) <= cia2_pa_o(3) or not cia2_pa_t(3);
    read_2pa(4) <= cia2_pa_o(4) or not cia2_pa_t(4);
    read_2pa(5) <= cia2_pa_o(5) or not cia2_pa_t(5);
    read_2pa(6) <= cia2_pa(6);
    read_2pa(7) <= cia2_pa(7);

    process(cia1_pa, cia1_pb, cia2_pa, read_2pa, address)
    begin
        case address is
        when "000" =>
            read_data <= cia1_pa;
        when "001" =>
            read_data <= cia1_pb;
--        when "010" =>
--            read_data <= cia1_pa_t;
--        when "011" =>
--            read_data <= cia1_pb_t;
        when "100" =>
            read_data <= read_2pa;
        when "101" =>
            read_data <= cia2_pa;
--        when "110" =>
--            read_data <= cia2_pa_t;
--        when "111" =>
--            read_data <= cia2_pb_t;
        when others =>
            read_data <= X"FF";
        end case;            
    end process; 
    
    data <= read_data when read = '0' else "ZZZZZZZZ";

    process(write, reset_n)
    begin
        if reset_n = '0' then
            cia1_pa_t <= (others => '0'); 
            cia1_pa_o <= (others => '0'); 
            cia1_pb_t <= (others => '0'); 
            cia1_pb_o <= (others => '0'); 
            cia2_pa_t <= (others => '0'); 
            cia2_pa_o <= (others => '0'); 
            cia2_pb_t <= (others => '0'); 
            cia2_pb_o <= (others => '0'); 
        elsif rising_edge(write) then
            case address is
            when "000" => -- CIA1 PA
                cia1_pa_o <= data;
            when "001" => -- CIA1 PB
                cia1_pb_o <= data;
            when "010" => -- CIA1 PA DDR
                cia1_pa_t <= data;
            when "011" => -- CIA1 PB DDR
                cia1_pb_t(5 downto 0) <= data(5 downto 0); -- we never drive the B ports, bit 6 and 7, the FPGA does
            when "100" => -- CIA2 PA
                cia2_pa_o <= data;
            when "101" => -- CIA2 PB
                cia2_pb_o <= data;
            when "110" => -- CIA2 PA DDR
                cia2_pa_t <= data;
            when "111" => -- CIA2 PB DDR
                cia2_pb_t(5 downto 0) <= data(5 downto 0); -- we never drive the B ports, bit 6 and 7, the FPGA does
            when others =>
                null;
            end case;
        end if;
    end process;
    
    r1: for i in 0 to 7 generate
        cia1_pa(i) <= '0' when cia1_pa_o(i) = '0' and cia1_pa_t(i) = '1' else 'Z';
        cia1_pb(i) <= '0' when cia1_pb_o(i) = '0' and cia1_pb_t(i) = '1' else 'Z';
    end generate;
    
--    cia2_pa <= "ZZZZZZZZ";
    cia2_pb <= "ZZZZZZZZ";
    
    r2: for i in 0 to 7 generate
        cia2_pa(i) <= cia2_pa_o(i) when cia2_pa_t(i) = '1' else 'Z';
--        cia2_pb(i) <= '0' when cia2_pb_o(i) = '0' and cia2_pb_t(i) = '1' else 'Z';
    end generate;

    lightpen <= cia1_pb(4);
end architecture;
