library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity floppy_sound is
generic (
    g_tag          : std_logic_vector(7 downto 0) := X"04";
    sound_base     : unsigned(27 downto 16) := X"103";
    motor_hum_addr : unsigned(15 downto 0) := X"0000";
    flop_slip_addr : unsigned(15 downto 0) := X"1200";
    track_in_addr  : unsigned(15 downto 0) := X"2400";
    track_out_addr : unsigned(15 downto 0) := X"2E00";
    head_bang_addr : unsigned(15 downto 0) := X"3800";
    insert_addr    : unsigned(15 downto 0) := X"4000";
    remove_addr    : unsigned(15 downto 0) := X"6000";
    motor_len      : integer := 4410;
    track_in_len   : unsigned(15 downto 0) := X"089D"; -- 100 ms
    track_out_len  : unsigned(15 downto 0) := X"089D"; -- 100 ms
    head_bang_len  : unsigned(15 downto 0) := X"089D"; -- 100 ms
    insert_len     : unsigned(15 downto 0) := X"2000"; -- 400 ms
    remove_len     : unsigned(15 downto 0) := X"2000" ); -- 400 ms

port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    tick_4MHz       : in  std_logic;
    
    do_trk_out      : in  std_logic;
    do_trk_in       : in  std_logic;
    do_head_bang    : in  std_logic;
    do_insert       : in  std_logic := '0';
    do_remove       : in  std_logic := '0';
    en_hum          : in  std_logic;
    en_slip         : in  std_logic;
    
	-- memory interface
	mem_req		    : out t_mem_req;
    mem_resp        : in  t_mem_resp;

    -- audio
    sample_out      : out signed(12 downto 0) := (others => '0'));

end floppy_sound;
    
architecture gideon of floppy_sound is
    constant c_rate_div : integer := 4000000 / 22050;

    signal rate_count   : integer range 0 to c_rate_div;
    signal motor_sample : signed(7 downto 0);
    signal head_sample  : signed(7 downto 0);
    signal sample_tick  : std_logic;
    type   t_voice_state is (idle, play);
    type   t_serve_state is (idle, wait_voice1, serve_voice2, wait_voice2);
    
    signal voice1       : t_voice_state;
    signal serve_state  : t_serve_state;
    signal voice1_cnt   : unsigned(13 downto 0); -- max 16K
    signal voice1_addr  : unsigned(15 downto 0);
    signal voice2_cnt   : unsigned(13 downto 0); -- max 16K
    signal mem_addr_i   : unsigned(15 downto 0);

    signal mem_rack     : std_logic;
    signal mem_dack     : std_logic;
begin
    mem_req.tag         <= g_tag;
    mem_req.read_writen <= '1'; -- always read
    mem_req.address     <= sound_base(25 downto 16) & mem_addr_i;
    mem_req.data        <= X"00";
    mem_req.size        <= "00"; -- 1 byte at a time
    mem_rack <= '1' when mem_resp.rack_tag = g_tag else '0';
    mem_dack <= '1' when mem_resp.dack_tag = g_tag else '0';

    process(clock)
        variable signed_sum : signed(12 downto 0);
    begin
        if rising_edge(clock) then
            sample_tick <= '0';
            if tick_4MHz = '1' then
                if rate_count = 0 then
                    signed_sum := motor_sample + (head_sample(head_sample'high) & head_sample & "0000");
                    sample_out  <= signed_sum;
                    rate_count  <= c_rate_div;
                    sample_tick <= '1';
                else
                    rate_count <= rate_count - 1;
                end if;
            end if;

            case serve_state is
            when idle =>
                if sample_tick='1' then
                    case voice1 is
                    when play =>
                        if voice1_cnt = 0 then
                            voice1 <= idle;
                        else
                            mem_req.request  <= '1';
                            mem_addr_i <= voice1_addr;
                            serve_state <= wait_voice1;
                        end if;
                    when others =>
                        head_sample <= X"00";
                        serve_state <= serve_voice2;
                    end case;
                end if;

            when wait_voice1 =>
                if mem_rack='1' then
                    mem_req.request <= '0';
                end if;
                if mem_dack='1' then
                    head_sample <= signed(mem_resp.data);
                    voice1_cnt  <= voice1_cnt - 1;
                    voice1_addr <= voice1_addr + 1;
                    serve_state <= serve_voice2;
                end if;

            when serve_voice2 =>
                if en_hum = '1' then
                    mem_req.request     <= '1';
                    mem_addr_i <= motor_hum_addr(15 downto 0) + ("00" & voice2_cnt);
                    serve_state <= wait_voice2;                    
                elsif en_slip = '1' then
                    mem_req.request     <= '1';
                    mem_addr_i <= flop_slip_addr(15 downto 0) + ("00" & voice2_cnt);
                    serve_state <= wait_voice2;                    
                else
                    motor_sample <= X"00";
                    serve_state <= idle;
                end if;
            
            when wait_voice2 =>
                if mem_rack='1' then
                    mem_req.request <= '0';
                end if;
                if mem_dack='1' then
                    motor_sample <= signed(mem_resp.data);
                    if voice2_cnt = motor_len-1 then
                        voice2_cnt <= (others => '0');
                    else
                        voice2_cnt   <= voice2_cnt + 1;
                    end if;
                    serve_state  <= idle;
                end if;
            
            when others =>
                null;
            end case;

            if do_trk_out = '1' then
                voice1       <= play;
                voice1_cnt   <= track_out_len(voice1_cnt'range);
                voice1_addr  <= track_out_addr(voice1_addr'range);
            end if;
            if do_trk_in = '1' then
                voice1       <= play;
                voice1_cnt   <= track_in_len(voice1_cnt'range);
                voice1_addr  <= track_in_addr(voice1_addr'range);
            end if;
            if do_head_bang = '1' then
                voice1       <= play;
                voice1_cnt   <= head_bang_len(voice1_cnt'range);
                voice1_addr  <= head_bang_addr(voice1_addr'range);
            end if;
            if do_insert = '1' then
                voice1       <= play;
                voice1_cnt   <= insert_len(voice1_cnt'range);
                voice1_addr  <= insert_addr(voice1_addr'range);
            end if;
            if do_remove = '1' then
                voice1       <= play;
                voice1_cnt   <= remove_len(voice1_cnt'range);
                voice1_addr  <= remove_addr(voice1_addr'range);
            end if;

            
            if reset='1' then
                mem_req.request <= '0';
                serve_state     <= idle;
                voice1          <= idle;
                voice1_cnt      <= (others => '0');
                voice2_cnt      <= (others => '0');
                voice1_addr     <= (others => '0');
				sample_out      <= (others => '0');
				motor_sample    <= (others => '0');
            end if;
        end if;
    end process;
end gideon;
