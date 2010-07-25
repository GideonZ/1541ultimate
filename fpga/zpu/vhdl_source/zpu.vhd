------------------------------------------------------------------------------
----                                                                      ----
----  ZPU 8-bit version, wrapper wirh memory                              ----
----                                                                      ----
----  http://www.opencores.org/                                           ----
----                                                                      ----
----  Description:                                                        ----
----  ZPU is a 32 bits small stack cpu. This is a modified version of     ----
----  the zpu_small implementation. This one has only one 8-bit external  ----
----  memory port, which is used for I/O, instruction fetch and data      ----
----  accesses. It is intended to interface with existing 8-bit systems,  ----
----  while maintaining the large addressing range and 32-bit programming ----
----  model. The 32-bit stack remains "internal" in the ZPU.              ----
----                                                                      ----
----  This version is about the same size as zpu_small from zealot,       ----
----  but performs 25% better at the same clock speed, given that the     ----
----  external memory bus can operate with 0 wait states. The performance ----
----  increase is due to the fact that most instructions only require 3   ----
----  clock cycles instead of 4.                                          ----
----                                                                      ----
----  Author:                                                             ----
----    - Øyvind Harboe, oyvind.harboe zylin.com [zpu concept]            ----
----    - Salvador E. Tropea, salvador inti.gob.ar [zealot]               ----
----    - Gideon Zweijtzer, gideon.zweijtzer technolution.eu [this]       ----
----                                                                      ----
------------------------------------------------------------------------------
----                                                                      ----
---- Copyright (c) 2008 Øyvind Harboe <oyvind.harboe zylin.com>           ----
---- Copyright (c) 2008 Salvador E. Tropea <salvador inti.gob.ar>         ----
---- Copyright (c) 2008 Instituto Nacional de Tecnología Industrial       ----
---- Copyright (c) 2009 Gideon N. Zweijtzer <Technolution.NL>             ----
----                                                                      ----
---- Distributed under the BSD license                                    ----
----                                                                      ----
------------------------------------------------------------------------------
----                                                                      ----
---- Design unit:      zpu (Behave) (Entity and architecture)             ----
---- File name:        zpu.vhd                                            ----
---- Note:             None                                               ----
---- Limitations:      None known                                         ----
---- Errors:           None known                                         ----
---- Library:          work                                               ----
---- Dependencies:     ieee.std_logic_1164                                ----
----                   ieee.numeric_std                                   ----
----                   work.zpupkg                                        ----
---- Target FPGA:      Spartan 3E (XC3S500E-4-PQG208)                     ----
---- Language:         VHDL                                               ----
---- Wishbone:         No                                                 ----
---- Synthesis tools:  Xilinx Release 10.1.03i - xst K.39                 ----
---- Simulation tools: Modelsim                                           ----
---- Text editor:      UltraEdit 11.00a+                                  ----
----                                                                      ----
------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.zpupkg.all;

entity zpu is
    generic(
        g_addr_size  : integer := 16;
        g_stack_size : integer := 12;  -- Memory (stack+data) width
        g_prog_size  : integer := 14;  -- Program size
        g_dont_care  : std_logic := '-'); -- Value used to fill the unused bits, can be '-' or '0'
    port(
        clock        : in  std_logic;
        reset        : in  std_logic;
        interrupt_i  : in  std_logic;
        break_o      : out std_logic;

        mem_address  : out std_logic_vector(g_addr_size-1 downto 0);
        mem_size     : out std_logic_vector(1 downto 0);
        mem_instr    : out std_logic;
        mem_req      : out std_logic;
        mem_write    : out std_logic;
        mem_rack     : in  std_logic;
        mem_dack     : in  std_logic;
        mem_wdata    : out std_logic_vector(7 downto 0);
        mem_rdata    : in  std_logic_vector(7 downto 0) );
end zpu;

architecture gideon of zpu is
    signal a_we         : std_logic; -- BRAM A port Write Enable
    signal a_en         : std_logic; -- BRAM A port Enable
    signal a_addr       : unsigned(g_stack_size-1 downto 2):=(others => '0'); -- BRAM A Address
    signal a_wdata      : unsigned(31 downto 0):=(others => '0'); -- Data to BRAM A port
    signal a_rdata      : unsigned(31 downto 0); -- Data from BRAM A port
    signal a_wdata_slv  : std_logic_vector(31 downto 0):=(others => '0'); -- Data to BRAM A port
    signal a_rdata_slv  : std_logic_vector(31 downto 0); -- Data from BRAM A port

    signal b_we         : std_logic; -- BRAM B port Write Enable
    signal b_en         : std_logic; -- BRAM B port Enable
    signal b_addr       : unsigned(g_stack_size-1 downto 2):=(others => '0'); -- BRAM B Address
    signal b_wdata      : unsigned(31 downto 0):=(others => '0'); -- Data to BRAM B port
    signal b_rdata      : unsigned(31 downto 0); -- Data from BRAM B port
    signal b_wdata_slv  : std_logic_vector(31 downto 0):=(others => '0'); -- Data to BRAM B port
    signal b_rdata_slv  : std_logic_vector(31 downto 0); -- Data from BRAM B port

    signal c_addr       : unsigned(g_addr_size-1 downto 0);

begin

    cpu: entity work.zpu_8bit_loadb
    generic map (
        g_addr_size  => g_addr_size, 
        g_stack_size => g_stack_size,
        g_prog_size  => g_prog_size, 
        g_dont_care  => g_dont_care )
    port map (
        clk_i        => clock,      
        reset_i      => reset,      
        interrupt_i  => interrupt_i,
        break_o      => break_o,    
-- synthesis translate_off
        dbg_o        => open,       
-- synthesis translate_on
        -- BRAM (stack ONLY)
        a_we_o       => a_we,
        a_en_o       => a_en,
        a_addr_o     => a_addr,
        a_o          => a_wdata,
        a_i          => a_rdata,

        b_we_o       => b_we,
        b_en_o       => b_en,
        b_addr_o     => b_addr,
        b_o          => b_wdata,
        b_i          => b_rdata,

        -- memory port for text, bss, data
        c_addr_o     => c_addr,
        c_size_o     => mem_size,
        c_inst_o     => mem_instr,
        c_req_o      => mem_req,
        c_rack_i     => mem_rack,
        c_dack_i     => mem_dack,
        c_we_o       => mem_write,
        c_data_o     => mem_wdata,
        c_data_i     => mem_rdata );

    mem_address <= std_logic_vector(c_addr);

    a_wdata_slv <= std_logic_vector(a_wdata);
    b_wdata_slv <= std_logic_vector(b_wdata);
    a_rdata     <= unsigned(a_rdata_slv);
    b_rdata     <= unsigned(b_rdata_slv);
        
    i_stack_ram: entity work.dpram
    generic map (
        g_width_bits    => 32,
        g_depth_bits    => g_stack_size-2,
        g_read_first_a  => false,
        g_read_first_b  => false,
        g_storage       => "block" )
    port map (
        a_clock         => clock,
        a_address       => a_addr,
        a_rdata         => a_rdata_slv,
        a_wdata         => a_wdata_slv,
        a_en            => a_en,
        a_we            => a_we,

        b_clock         => clock,
        b_address       => b_addr,
        b_rdata         => b_rdata_slv,
        b_wdata         => b_wdata_slv,
        b_en            => b_en,
        b_we            => b_we );

end gideon;
