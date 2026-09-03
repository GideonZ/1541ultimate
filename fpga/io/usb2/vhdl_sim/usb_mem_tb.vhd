--------------------------------------------------------------------------------
-- Gideon's Logic Architectures - Copyright 2014
-- Entity: usb_mem_tb
-- Date:2026-09-01
-- Author: Gideon     
-- Description: Quick test bench for non-aligned reads
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.mem_bus_pkg.all;
use work.tl_flat_memory_model_pkg.all;

entity usb_mem_tb is

end entity;

architecture arch of usb_mem_tb is
    signal clocks_stopped: boolean := false;
    signal clock        : std_logic := '0';
    signal reset        : std_logic;

    signal cmd_addr     : std_logic_vector(3 downto 0) := X"0";
    signal cmd_valid    : std_logic := '0';
    signal cmd_write    : std_logic := '0';
    signal cmd_wdata    : std_logic_vector(15 downto 0) := X"0000";
    signal cmd_ack      : std_logic := '0';
    signal cmd_done     : std_logic := '0';
    signal cmd_ready    : std_logic := '0';

    -- BRAM interface
    signal ram_addr     : std_logic_vector(10 downto 2);
    signal ram_en       : std_logic;
    signal ram_we       : std_logic_vector(3 downto 0);
    signal ram_wdata    : std_logic_vector(31 downto 0);
    signal ram_rdata    : std_logic_vector(31 downto 0) := X"DEADBABE";
    
    -- memory interface
    signal mem_req      : t_mem_req_32 := c_mem_req_32_init;
    signal mem_resp     : t_mem_resp_32;

begin
    clock <= not clock after 10 ns when not clocks_stopped;
    reset <= '1', '0' after 250 ns;

    i_memctrl: entity work.usb_memory_ctrl
    generic map (
        g_big_endian => false,
        g_tag        => X"14" )
    
    port map (
        clock       => clock,
        reset       => reset,
        
        -- cmd interface
        cmd_addr    => cmd_addr,
        cmd_valid   => cmd_valid,
        cmd_write   => cmd_write,
        cmd_wdata   => cmd_wdata,
        cmd_ack     => cmd_ack,
        cmd_ready   => cmd_ready,
    
        -- BRAM interface
        ram_addr    => ram_addr,
        ram_en      => ram_en,
        ram_we      => ram_we,
        ram_wdata   => ram_wdata,
        ram_rdata   => ram_rdata,
        
        -- memory interface
        mem_req     => mem_req,
        mem_resp    => mem_resp );
        
    i_memory: entity work.mem_bus_32_slave_bfm
    generic map (
        g_name    => "memory",
        g_latency => 3
    )
    port map (
        clock     => clock,
        req       => mem_req,
        resp      => mem_resp
    );

    process
        variable mem : h_mem_object;
    begin
        wait until reset = '0';
        bind_mem_model("memory", mem);

        for k in 508 to 512 loop
            for i in 0 to 511 loop
                write_memory_8(mem, std_logic_vector(to_unsigned(k + i, 32)), std_logic_vector(to_unsigned(i, 8)));
            end loop;
            wait until clock = '1';
            cmd_addr  <= X"0";
            cmd_wdata <= std_logic_vector(to_unsigned(k, 16));
            cmd_valid <= '1';
            cmd_write <= '1';
            wait until clock = '1';
            cmd_addr  <= X"3";
            cmd_wdata <= std_logic_vector(to_unsigned(512 + 3 + (k mod 4), 16)); -- mod 4 = and 3
            cmd_valid <= '1';
            cmd_write <= '1';
            wait until clock = '1';
            cmd_valid <= '0';
            cmd_write <= '0';
            wait until clock = '1';
            wait until cmd_ready = '1';
        end loop;

        -- since we also added byte enables to the mem_write, let's observe the behavior of different lengths
        for k in 1 to 10 loop
            wait until clock = '1';
            cmd_addr  <= X"0";
            cmd_wdata <= std_logic_vector(to_unsigned(k * 4096, 16));
            cmd_valid <= '1';
            cmd_write <= '1';
            wait until clock = '1';
            cmd_addr  <= X"2";
            cmd_wdata <= std_logic_vector(to_unsigned(k + 3, 16));
            cmd_valid <= '1';
            cmd_write <= '1';
            wait until clock = '1';
            cmd_valid <= '0';
            cmd_write <= '0';
            wait until clock = '1';
            wait until cmd_ready = '1';
        end loop;

        for i in 1 to 30 loop
            wait until clock = '1';
        end loop;
        clocks_stopped <= true;
    end process;

    test: process(clock)
        variable expected : std_logic_vector(31 downto 0);
    begin
        if rising_edge(clock) then
            if ram_we = X"F" then
                expected := std_logic_vector(to_unsigned(3 + 4 * to_integer(unsigned(ram_addr)), 8)) &
                            std_logic_vector(to_unsigned(2 + 4 * to_integer(unsigned(ram_addr)), 8)) &
                            std_logic_vector(to_unsigned(1 + 4 * to_integer(unsigned(ram_addr)), 8)) &
                            std_logic_vector(to_unsigned(0 + 4 * to_integer(unsigned(ram_addr)), 8));
                assert ram_wdata = expected
                    report "RAM write data incorrect"
                    severity failure;
            else
                assert ram_we = X"0"
                    report "Ram write enable not full word"
                    severity error;
            end if;
        end if;
    end process;

end arch;
