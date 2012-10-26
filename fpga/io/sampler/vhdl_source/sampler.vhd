library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.sampler_pkg.all;

entity sampler is
generic (
    g_num_voices    : positive := 8 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;
    
    mem_req     : out t_mem_req;
    mem_resp    : in  t_mem_resp;
    
    irq         : out std_logic;
    
    sample_L    : out signed(17 downto 0);
    sample_R    : out signed(17 downto 0);
    new_sample  : out std_logic );

end entity;

architecture gideon of sampler is
    signal voice_state        : t_voice_state_array(0 to g_num_voices-1) := (others => c_voice_state_init);
    signal voice_sample_reg_h : t_sample_byte_array(0 to g_num_voices-1) := (others => (others => '0'));
    signal voice_sample_reg_l : t_sample_byte_array(0 to g_num_voices-1) := (others => (others => '0'));
    
    signal voice_i       : integer range 0 to g_num_voices-1;
    signal fetch_en      : std_logic;
    signal fetch_addr    : unsigned(25 downto 0);
    signal fetch_tag     : std_logic_vector(7 downto 0);
    signal interrupt     : std_logic_vector(g_num_voices-1 downto 0);
    signal interrupt_clr : std_logic_vector(g_num_voices-1 downto 0);

    signal current_control    : t_voice_control;

    signal first_chan   : std_logic;
    signal cur_sam      : signed(15 downto 0);
    signal cur_vol      : unsigned(5 downto 0);
    signal cur_pan      : unsigned(3 downto 0);

begin
    i_regs: entity work.sampler_regs
    generic map (
        g_num_voices => g_num_voices )
    port map (
        clock       => clock,
        reset       => reset,
        
        io_req      => io_req,
        io_resp     => io_resp,
        
        rd_addr     => voice_i,
        control     => current_control,
        irq_status  => interrupt,
        irq_clear   => interrupt_clr );

    irq <= '1' when unsigned(interrupt) /= 0 else '0';

    process(clock)
        variable current_state      : t_voice_state;
        variable next_state         : t_voice_state;
        variable sample_reg         : signed(15 downto 0);
        variable v                  : integer range 0 to 15;
    begin
        if rising_edge(clock) then
            if voice_i = g_num_voices-1 then
                voice_i <= 0;
            else
                voice_i <= voice_i + 1;            
            end if;
 
            for i in interrupt'range loop
                if interrupt_clr(i)='1' then
                    interrupt(i) <= '0';
                end if;
            end loop;
            
            fetch_en <= '0';
            current_state   := voice_state(0); 
            sample_reg      := voice_sample_reg_h(voice_i) & voice_sample_reg_l(voice_i);
            
            next_state := current_state;

            case current_state.state is
            when idle =>
                if current_control.enable then
                    next_state.state      := fetch1;
                    next_state.position   := (others => '0');
                    next_state.divider    := current_control.rate;
                    next_state.sample_out := (others => '0');
                end if;
            
            when playing =>
                if current_state.divider = 0 then
                    next_state.divider    := current_control.rate;
                    next_state.sample_out := sample_reg;
                    next_state.state      := fetch1;
                    if (current_state.position = current_control.repeat_b) then
                        if current_control.enable and current_control.repeat then
                            next_state.position := current_control.repeat_a;
                        end if;
                    elsif current_state.position = current_control.length then
                        next_state.state    := finished;
                        if current_control.interrupt then
                            interrupt(voice_i) <= '1';
                        end if;                        
                    end if;
                else
                    next_state.divider := current_state.divider - 1;
                end if;
                if not current_control.enable and not current_control.repeat then
                    next_state.state := idle;
                end if;

            when finished =>
                if not current_control.enable then
                    next_state.state := idle;
                end if;

            when fetch1 =>
                fetch_en <= '1';
                fetch_addr <= current_control.start_addr + current_state.position;
                if current_control.mode = mono8 then
                    fetch_tag  <= "110" & std_logic_vector(to_unsigned(voice_i, 4)) & '1'; -- high
                    next_state.state := playing;
                    if current_control.interleave then
                        next_state.position := current_state.position + 2; -- this and the next byte
                    else
                        next_state.position := current_state.position + 1; -- this byte only
                    end if;
                else
                    fetch_tag  <= "110" & std_logic_vector(to_unsigned(voice_i, 4)) & '0'; -- low
                    next_state.position := current_state.position + 1;  -- go to the next byte
                    next_state.state := fetch2;
                end if;
            
            when fetch2 =>
                fetch_en   <= '1';
                fetch_addr <= current_control.start_addr + current_state.position;
                fetch_tag  <= "110" & std_logic_vector(to_unsigned(voice_i, 4)) & '1'; -- high
                next_state.state    := playing;
                if current_control.interleave then
                    next_state.position := current_state.position + 3; -- this and the two next bytes
                else
                    next_state.position := current_state.position + 1; -- this byte only
                end if;
            
            when others =>
                null;
            end case;

            cur_sam <= current_state.sample_out;
            cur_vol <= current_control.volume;
            cur_pan <= current_control.pan;

            if voice_i=0 then
                first_chan <= '1';
            else
                first_chan <= '0';
            end if;
            
            -- write port - state --
            voice_state <= voice_state(1 to g_num_voices-1) & next_state;
                        
            -- write port - sample data --
            if mem_resp.dack_tag(7 downto 5) = "110" then
                v := to_integer(unsigned(mem_resp.dack_tag(4 downto 1)));
                if mem_resp.dack_tag(0)='1' then
                    voice_sample_reg_h(v) <= signed(mem_resp.data);
                else
                    voice_sample_reg_l(v) <= signed(mem_resp.data);
                end if;
            end if;

            if reset='1' then
                voice_i <= 0;
                next_state.state := idle; -- shifted into the voice state vector automatically.
                interrupt <= (others => '0');
            end if;
        end if;
    end process;

    b_mem_fifo: block
        signal rack      : std_logic;
        signal fifo_din  : std_logic_vector(33 downto 0);
        signal fifo_dout : std_logic_vector(33 downto 0);
    begin
        fifo_din <= fetch_tag & std_logic_vector(fetch_addr);
        
        i_fifo: entity work.srl_fifo
        generic map (
            Width     => 34,
            Depth     => 15,
            Threshold => 10 )
        port map (
            clock       => clock,
            reset       => reset,
            GetElement  => rack,
            PutElement  => fetch_en,
            FlushFifo   => '0',
            DataIn      => fifo_din,
            DataOut     => fifo_dout,
            SpaceInFifo => open,
            DataInFifo  => mem_req.request );
    
        mem_req.read_writen <= '1';
        mem_req.address     <= unsigned(fifo_dout(25 downto 0));
        mem_req.tag         <= fifo_dout(33 downto 26);
        mem_req.data        <= X"00";

        rack <= '1' when (mem_resp.rack='1' and mem_resp.rack_tag(7 downto 5)="110") else '0';
    end block;

    i_accu: entity work.sampler_accu
    port map (
        clock       => clock,
        reset       => reset,
        
        first_chan  => first_chan,
        sample_in   => cur_sam,
        volume_in   => cur_vol,
        pan_in      => cur_pan,
        
        sample_L    => sample_L,
        sample_R    => sample_R,
        new_sample  => new_sample );

end gideon;
