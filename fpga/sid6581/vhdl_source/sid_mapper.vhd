-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2011 Gideon's Logic Architectures'
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
use work.io_bus_pkg.all;
use work.slot_bus_pkg.all;
use work.sid_io_regs_pkg.all;

entity sid_mapper is
port (
    clock         : in  std_logic;
    reset         : in  std_logic;
                  
    slot_req      : in  t_slot_req;
    slot_resp     : out t_slot_resp;

    control       : in  t_sid_control;
    
    sid_addr      : out unsigned(7 downto 0);
    sid_wren      : out std_logic;
    sid_wdata     : out std_logic_vector(7 downto 0);
    sid_rdata     : in  std_logic_vector(7 downto 0) );

end sid_mapper;


architecture mapping of sid_mapper is
    signal sid_wren_l   : std_logic;
    signal sid_wren_r   : std_logic;
    signal sid_wren_d   : std_logic;
    signal sid_addr_l   : unsigned(7 downto 0);
    signal sid_addr_r   : unsigned(7 downto 0);
    signal sid_addr_d   : unsigned(7 downto 0);
    signal sid_wdata_l  : std_logic_vector(7 downto 0);
    signal sid_wdata_d  : std_logic_vector(7 downto 0);
begin
    slot_resp.data <= sid_rdata;

    sid_wren  <= sid_wren_l or sid_wren_d;
    sid_addr  <= sid_addr_d when sid_wren_d='1' else sid_addr_l;
    sid_wdata <= sid_wdata_l; -- should work, but it's not neat!
    
    process(clock)
    begin
        if rising_edge(clock) then
            sid_wren_l <= '0';
            sid_wren_r <= '0';
            sid_wren_d <= sid_wren_r;
            sid_addr_d <= sid_addr_r;
            sid_wdata_l <= slot_req.data;
            sid_wdata_d <= sid_wdata_l;

            if slot_req.io_write='1' then
                sid_addr_l <= slot_req.io_address(7 downto 0);
                sid_addr_r <= slot_req.io_address(7 downto 0);
            else
                sid_addr_l <= slot_req.bus_address(7 downto 0);
                sid_addr_r <= slot_req.bus_address(7 downto 0);
            end if;
                        
            -- check for left channel access
            if control.enable_left='1' then
                if slot_req.bus_write='1' then
                    if control.snoop_left='1' and slot_req.bus_address(15 downto 12)=X"D" then
                        if control.extend_left='1' then
                            if slot_req.bus_address(11 downto 7)=control.base_left(11 downto 7) then
                                sid_addr_l(7) <= '0';
                                sid_wren_l <= '1';
                            end if;
                        else -- just 3 voices
                            if slot_req.bus_address(11 downto 5)=control.base_left(11 downto 5) then
                                sid_wren_l <= '1';
                                sid_addr_l(7 downto 5) <= "000"; -- truncated address
                            end if;
                        end if;
                    end if;
                elsif slot_req.io_write='1' then
                    if control.snoop_left='0' then
                        if control.extend_left='1' and slot_req.io_address(8 downto 7)=control.base_left(8 downto 7) then
                            sid_addr_l(7) <= '0';
                            sid_wren_l <= '1';
                        elsif control.extend_left='0' and slot_req.io_address(8 downto 5)=control.base_left(8 downto 5) then
                            sid_addr_l(7 downto 5) <= "000";
                            sid_wren_l <= '1';
                        end if;
                    end if;
                end if;
            end if;
                                        
            -- check for right channel access
            if control.enable_right='1' then
                if slot_req.bus_write='1' then
                    if control.snoop_right='1' and slot_req.bus_address(15 downto 12)=X"D" then
                        if control.extend_right='1' then
                            if slot_req.bus_address(11 downto 7)=control.base_right(11 downto 7) then
                                sid_addr_r(7) <= '1';
                                sid_wren_r <= '1';
                            end if;
                        else -- just 3 voices
                            if slot_req.bus_address(11 downto 5)=control.base_right(11 downto 5) then
                                sid_wren_r <= '1';
                                sid_addr_r(7 downto 5) <= "100"; -- truncated address
                            end if;
                        end if;
                    end if;
                elsif slot_req.io_write='1' then
                    if control.snoop_right='0' then
                        if control.extend_right='1' and slot_req.io_address(8 downto 7)=control.base_right(8 downto 7) then
                            sid_addr_r(7) <= '1';
                            sid_wren_r <= '1';
                        elsif control.extend_right='0' and slot_req.io_address(8 downto 5)=control.base_right(8 downto 5) then
                            sid_addr_r(7 downto 5) <= "100";
                            sid_wren_r <= '1';
                        end if;
                    end if;
                end if;
            end if;
        end if;
    end process;

    slot_resp.irq        <= '0';
    slot_resp.reg_output <= '0';

end mapping;

        
    -- Mapping options are as follows:
    -- STD $D400-$D41F: Snoop='1' Base=$40. Extend='0' (bit 11...5 are significant)
    -- STD $D500-$D51F: Snoop='1' Base=$50. Extend='0'
    -- STD $DE00-$DE1F: Snoop='0' Base=$E0. Extend='0' (bit 8...5 are significant)
    -- STD $DF00-$DF1F: Snoop='0' Base=$F0. Extend='0'
    -- EXT $DF80-$DFFF: Snoop='0' Base=$F8. Extend='1' (bit 8...7 are significant)
    -- EXT $D600-$D67F: Snoop='1' Base=$60. Extend='1' (bit 11..7 are significant)
    -- .. etc
