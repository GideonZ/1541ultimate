library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity zpu_profiler is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    cpu_address : in  std_logic_vector(26 downto 0);
    cpu_instr   : in  std_logic;
    cpu_req     : in  std_logic;
    cpu_write   : in  std_logic;
    cpu_rack    : in  std_logic;
    cpu_dack    : in  std_logic;

    enable      : in  std_logic;
    clear       : in  std_logic;

    section     : in  std_logic_vector(3 downto 0);
    match       : in  std_logic_vector(3 downto 0);

    my_read     : in  std_logic;
    my_rack     : out std_logic;
    my_dack     : out std_logic;
    rdata       : out std_logic_vector(7 downto 0) );
end zpu_profiler;

architecture gideon of zpu_profiler is
    type t_count_array is array (natural range <>) of unsigned(31 downto 0);
    signal counters : t_count_array(0 to 7) := (others => (others => '0'));
    signal emulation_access     : std_logic;
    signal io_access            : std_logic;
    signal instruction_access   : std_logic;
    signal other_access         : std_logic;
    signal writes               : std_logic;
    signal req_d                : std_logic;
    signal count_out            : std_logic_vector(31 downto 0) := (others => '0');
    signal cpu_address_d        : std_logic_vector(1 downto 0);
    signal section_d            : std_logic_vector(section'range);
    signal section_entry        : std_logic;
    signal section_match        : std_logic;
begin
    process(clock)
    begin
        if rising_edge(clock) then
            emulation_access   <= '0';
            io_access          <= '0';
            instruction_access <= '0';
            other_access       <= '0';
            writes             <= '0';
            section_entry      <= '0';
            section_match      <= '0';

            -- first, split time in different "enables" for the counters
            if cpu_rack='1' then
                if cpu_address(26 downto 10) = "00000000000000000" then
                    emulation_access <= '1';
                elsif cpu_address(26)='1' then
                    io_access <= '1';
                elsif cpu_instr = '1' then
                    instruction_access <= '1';
                else
                    other_access <= '1';
                end if;
                if cpu_write = '1' then
                    writes <= '1';
                end if;
            end if;

            section_d <= section;
            if section = match and section_d /= match then
                section_entry <= '1';
            end if;
            if section = match then
                section_match <= '1';
            end if;

            -- now, count our stuff
            if clear='1' then
                counters <= (others => (others => '0'));
            elsif enable='1' then
                counters(0) <= counters(0) + 1;
                if emulation_access = '1' then
                    counters(1) <= counters(1) + 1;
                end if;
                if io_access = '1' then
                    counters(2) <= counters(2) + 1;
                end if;
                if instruction_access = '1' then
                    counters(3) <= counters(3) + 1;
                end if;
                if other_access = '1' then
                    counters(4) <= counters(4) + 1;
                end if;
                if writes = '1' then
                    counters(5) <= counters(5) + 1;
                end if;
                if section_entry='1' then
                    counters(6) <= counters(6) + 1;
                end if;                        
                if section_match = '1' then
                    counters(7) <= counters(7) + 1;
                end if;
            end if;

            -- output register (read)
            if my_read='1' then
                if cpu_address(1 downto 0)="00" then
                    count_out <= std_logic_vector(counters(to_integer(unsigned(cpu_address(4 downto 2)))));
                end if;
            end if;
            cpu_address_d <= cpu_address(1 downto 0);
            case cpu_address_d is
                when "00" => rdata <= count_out(31 downto 24);
                when "01" => rdata <= count_out(23 downto 16);
                when "10" => rdata <= count_out(15 downto  8);
                when others => rdata <= count_out(7 downto 0);
            end case;
            
            req_d   <= my_read;
            my_dack <= req_d;
        end if;
    end process;

    my_rack <= my_read;
    
end gideon;