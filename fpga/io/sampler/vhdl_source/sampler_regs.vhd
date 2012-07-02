library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;
use work.sampler_pkg.all;

entity sampler_regs is
generic (
    g_num_voices    : positive := 8 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;
    
    rd_addr     : in  integer range 0 to g_num_voices-1;
    control     : out t_voice_control );
end entity;

architecture gideon of sampler_regs is
    type t_boolean_array is array (natural range <>) of boolean;
    type t_mode_array    is array (natural range <>) of t_sample_mode;
    type t_u8_array      is array (natural range <>) of unsigned(7 downto 0);
    type t_u6_array      is array (natural range <>) of unsigned(5 downto 0);
    type t_u4_array      is array (natural range <>) of unsigned(3 downto 0);
    type t_u2_array      is array (natural range <>) of unsigned(1 downto 0);

    signal enable      : t_boolean_array(0 to g_num_voices-1) := (others => false);
    signal repeat      : t_boolean_array(0 to g_num_voices-1) := (others => false);
    signal interrupt   : t_boolean_array(0 to g_num_voices-1) := (others => false);
    signal mode        : t_mode_array(0 to g_num_voices-1);
    signal start_addr3 : t_u2_array(0 to g_num_voices-1) := (others => "00");
    signal start_addr2 : t_u8_array(0 to g_num_voices-1) := (others => X"00");
    signal start_addr1 : t_u8_array(0 to g_num_voices-1) := (others => X"00");
    signal start_addr0 : t_u8_array(0 to g_num_voices-1) := (others => X"00");
    signal length2     : t_u8_array(0 to g_num_voices-1) := (others => X"00");
    signal length1     : t_u8_array(0 to g_num_voices-1) := (others => X"00");
    signal length0     : t_u8_array(0 to g_num_voices-1) := (others => X"00");
    signal rate_h      : t_u8_array(0 to g_num_voices-1) := (others => X"00");
    signal rate_l      : t_u8_array(0 to g_num_voices-1) := (others => X"00");
    signal volume      : t_u6_array(0 to g_num_voices-1) := (others => "100000");
    signal pan         : t_u4_array(0 to g_num_voices-1) := (others => X"8");

    signal wr_addr       : integer range 0 to g_num_voices-1;

    
begin
    wr_addr <= to_integer(io_req.address(7 downto 4));

    control.enable      <= enable(rd_addr);
    control.repeat      <= repeat(rd_addr);
    control.interrupt   <= interrupt(rd_addr);
    control.mode        <= mode(rd_addr);
    control.start_addr  <= start_addr3(rd_addr) & start_addr2(rd_addr) & start_addr1(rd_addr) & start_addr0(rd_addr);
    control.length      <= length2(rd_addr) & length1(rd_addr) & length0(rd_addr);
    control.rate        <= rate_h(rd_addr) & rate_l(rd_addr);
    control.volume      <= volume(rd_addr);
    control.pan         <= pan(rd_addr);
        
    process(clock)
    begin
        if rising_edge(clock) then
            -- write port - control -
            io_resp <= c_io_resp_init;
            io_resp.ack  <= io_req.read or io_req.write;
            if io_req.write='1' then
                case io_req.address(3 downto 0) is
                when c_sample_control       =>
                    enable(wr_addr)    <= (io_req.data(0) = '1');
                    repeat(wr_addr)    <= (io_req.data(1) = '1');
                    interrupt(wr_addr) <= (io_req.data(2) = '1');
                    if io_req.data(5 downto 4) = "00" then
                        mode(wr_addr) <= mono8;
                    else
                        mode(wr_addr) <= mono16;
                    end if;

                when c_sample_volume        =>
                    volume(wr_addr) <= unsigned(io_req.data(5 downto 0));

                when c_sample_pan           =>
                    pan(wr_addr) <= unsigned(io_req.data(3 downto 0));

                when c_sample_start_addr_h  =>
                    start_addr3(wr_addr) <= unsigned(io_req.data(1 downto 0));

                when c_sample_start_addr_mh =>
                    start_addr2(wr_addr) <= unsigned(io_req.data);

                when c_sample_start_addr_ml =>
                    start_addr1(wr_addr) <= unsigned(io_req.data);

                when c_sample_start_addr_l  =>
                    start_addr0(wr_addr) <= unsigned(io_req.data);

                when c_sample_length_h      =>
                    length2(wr_addr) <= unsigned(io_req.data);

                when c_sample_length_m      =>
                    length1(wr_addr) <= unsigned(io_req.data);

                when c_sample_length_l      =>
                    length0(wr_addr) <= unsigned(io_req.data);

                when c_sample_rate_h        =>
                    rate_h(wr_addr) <= unsigned(io_req.data);

                when c_sample_rate_l        =>
                    rate_l(wr_addr) <= unsigned(io_req.data);

                when others =>
                    null;
                end case;                   
            end if;

        end if;
    end process;

end gideon;
