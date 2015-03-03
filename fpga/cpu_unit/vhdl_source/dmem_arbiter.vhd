--------------------------------------------------------------------------------
-- Entity: dm_simple
-- Date: 2014-12-28  
-- Author: Gideon     
--
-- Description: WB D-bus arbiter. Note that for this arbiter, it is required
--              that the address will remain valid throughout the cycle? 
--                
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library mblite;
use mblite.core_Pkg.all;

--    type dmem_in_type is record
--        dat_i : std_logic_vector(CFG_DMEM_WIDTH - 1 downto 0);
--        ena_i : std_logic;
--    end record;
--
--    type dmem_out_type is record
--        dat_o : std_logic_vector(CFG_DMEM_WIDTH - 1 downto 0);
--        adr_o : std_logic_vector(CFG_DMEM_SIZE - 1 downto 0);
--        sel_o : std_logic_vector(3 downto 0);
--        we_o  : std_logic;
--        ena_o : std_logic;
--    end record;

entity dmem_arbiter is
    port  (
        clock       : in  std_logic;
        reset       : in  std_logic;
        
        imem_i      : in  dmem_out_type;
        imem_o      : out dmem_in_type;

        dmem_i      : in  dmem_out_type;
        dmem_o      : out dmem_in_type;
        
        mmem_o      : out dmem_out_type;
        mmem_i      : in  dmem_in_type );

end entity;

architecture arch of dmem_arbiter is
    type t_state is (idle, data, instr);
    signal state, next_state            : t_state;
    signal delayed_inst, delayed_inst_r : std_logic;
begin
    -- when dmem_i.ena_i is '1' (mem controller is capable of processing a request),
    -- then we need to decide which of the inputs is going through. At this point,
    -- we prioritize port 0 over port 1 (fixed prio). So the forward path will be
    -- combinatoric. The return path depends on which port was last forwarded to
    -- the memory controller. I.e. regardless of who is presenting the new request,
    -- the 'old' request determines to which port the read data belongs.

    -- Problem! We only have one ena_i pin per port; which means that once we set
    -- ena_i to '1', that the new address presented on adr_o is expected to be
    -- processed. This means that we either need to latch the address if we want
    -- to serve another port, or serve the same port again. The latter is not feasible
    -- when one port has priority over the other.

    -- Note that the instruction port doesn't write. This eliminates the need for
    -- a multiplexer on the write-data (dat_o). If we require the address to remain
    -- valid of the ibus, this will also eliminate the need for a register and
    -- multiplexer for the address. The cache will enforce this (non-WB compliant!)

    process(state, imem_i, dmem_i, mmem_i, delayed_inst_r)
    begin
        imem_o.dat_i <= mmem_i.dat_i;
        imem_o.ena_i <= '0';
         
        dmem_o.dat_i <= mmem_i.dat_i;
        dmem_o.ena_i <= '0';
        
        mmem_o.dat_o <= dmem_i.dat_o;
        mmem_o.sel_o <= dmem_i.sel_o;
        mmem_o.ena_o <= '0';
        mmem_o.we_o  <= '0';
        mmem_o.adr_o <= (others => 'X');
        
        -- output multiplexer
        delayed_inst <= '0';
        if mmem_i.ena_i = '1' then -- memory controller can process request
            if dmem_i.ena_o = '1' then -- dport wants attention
                mmem_o.ena_o <= '1';
                mmem_o.we_o  <= dmem_i.we_o;
                mmem_o.adr_o <= dmem_i.adr_o;
                delayed_inst <= imem_i.ena_o;
                next_state <= data;
            elsif imem_i.ena_o = '1' or delayed_inst_r = '1' then -- iport wants attention
                mmem_o.ena_o <= '1';
                mmem_o.we_o  <= '0';
                mmem_o.adr_o <= imem_i.adr_o;
                next_state <= instr;
                delayed_inst <= '0';
            else
                next_state <= idle;
                delayed_inst <= '0';
            end if;
        else -- memory controller cannot process request
            next_state <= state;
            delayed_inst <= delayed_inst_r or imem_i.ena_o;
        end if;

        -- input multiplexer
        case state is
        when idle =>
            imem_o.ena_i <= '1';
            dmem_o.ena_i <= '1';
            
        when data =>
            imem_o.ena_i <= '0';
            dmem_o.ena_i <= mmem_i.ena_i;
            
        when instr =>
            imem_o.ena_i <= mmem_i.ena_i;
            dmem_o.ena_i <= '0';
            
        when others =>
            null;
        end case;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            if reset='1' then
                state <= idle;
                delayed_inst_r <= '0';
            else
                state <= next_state;
                delayed_inst_r <= delayed_inst;
            end if;
        end if;
    end process;
end arch;
