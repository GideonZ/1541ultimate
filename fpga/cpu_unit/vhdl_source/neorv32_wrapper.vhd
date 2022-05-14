--------------------------------------------------------------------------------
-- Gideon's Logic B.V. - Copyright 2022
-- Entity: neorv32_wrapper
-- Author: Gideon     
-- Description: NeoRV CPU Core wrapper for Ultimate
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;
    
library neorv32;

entity neorv32_wrapper is
generic (
    g_jtag_debug    : boolean := true;
    g_frequency     : natural := 50_000_000;
    g_tag           : std_logic_vector(7 downto 0) := X"20" );
port (
    clock           : in    std_logic;
    reset           : in    std_logic;
    cpu_reset       : in    std_logic;
    
    -- JTAG on-chip debugger interface (available if ON_CHIP_DEBUGGER_EN = true) --
    jtag_trst_i     : in    std_ulogic := '0'; -- low-active TAP reset (optional)
    jtag_tck_i      : in    std_ulogic := '0'; -- serial clock
    jtag_tdi_i      : in    std_ulogic := '1'; -- serial data input
    jtag_tdo_o      : out   std_ulogic;        -- serial data output
    jtag_tms_i      : in    std_ulogic := '1'; -- mode select

    irq_i           : in    std_logic := '0';
    irq_o           : out   std_logic := '0';
    
    io_req          : out   t_io_req;
    io_resp         : in    t_io_resp;
    io_busy         : out   std_logic;
    
    mem_req         : out   t_mem_req_32;
    mem_resp        : in    t_mem_resp_32 );

end entity;

architecture arch of neorv32_wrapper is

    signal reset_i     : std_logic;
    signal reset_n     : std_logic;

    signal wb_tag_o       : std_ulogic_vector(02 downto 0); -- request tag
    signal wb_adr_o       : std_ulogic_vector(31 downto 0); -- address
    signal wb_dat_i       : std_ulogic_vector(31 downto 0) := (others => 'U'); -- read data
    signal wb_dat_o       : std_ulogic_vector(31 downto 0); -- write data
    signal wb_we_o        : std_ulogic; -- read/write
    signal wb_sel_o       : std_ulogic_vector(03 downto 0); -- byte enable
    signal wb_stb_o       : std_ulogic; -- strobe
    signal wb_cyc_o       : std_ulogic; -- valid cycle
    signal wb_lock_o      : std_ulogic; -- exclusive access request
    signal wb_ack_i       : std_ulogic := 'L'; -- transfer acknowledge
    signal wb_err_i       : std_ulogic := 'L'; -- transfer error

begin
    reset_i <= cpu_reset or reset when rising_edge(clock);
    reset_n <= not reset_i;
    
    i_cpu: entity neorv32.neorv32_top
    generic map(
        CLOCK_FREQUENCY              => g_frequency,
        HW_THREAD_ID                 => 0,
        INT_BOOTLOADER_EN            => true,
        ON_CHIP_DEBUGGER_EN          => g_jtag_debug,
        CPU_EXTENSION_RISCV_B        => false,
        CPU_EXTENSION_RISCV_C        => false,
        CPU_EXTENSION_RISCV_E        => false,
        CPU_EXTENSION_RISCV_M        => false,
        CPU_EXTENSION_RISCV_U        => false,
        CPU_EXTENSION_RISCV_Zfinx    => false,
        CPU_EXTENSION_RISCV_Zicsr    => true, -- for Interrupts
        CPU_EXTENSION_RISCV_Zicntr   => false,
        CPU_EXTENSION_RISCV_Zihpm    => false,
        CPU_EXTENSION_RISCV_Zifencei => g_jtag_debug,
        CPU_EXTENSION_RISCV_Zmmul    => false,
        CPU_EXTENSION_RISCV_Zxcfu    => false,
        FAST_MUL_EN                  => false,
        FAST_SHIFT_EN                => false,
        CPU_CNT_WIDTH                => 32,
        CPU_IPB_ENTRIES              => 2,
        PMP_NUM_REGIONS              => 0,
        HPM_NUM_CNTS                 => 0,
        MEM_INT_IMEM_EN              => false,
        MEM_INT_DMEM_EN              => true,   -- implement processor-internal data memory
        MEM_INT_DMEM_SIZE            => 2*1024, -- size of processor-internal data memory in bytes
        ICACHE_EN                    => true,
        ICACHE_NUM_BLOCKS            => 256,
        ICACHE_BLOCK_SIZE            => 8,
        ICACHE_ASSOCIATIVITY         => 1,
        MEM_EXT_EN                   => true,
        MEM_EXT_TIMEOUT              => 255,
        MEM_EXT_PIPE_MODE            => true,
        MEM_EXT_BIG_ENDIAN           => false,
        MEM_EXT_ASYNC_RX             => false,
        SLINK_NUM_TX                 => 0,
        SLINK_NUM_RX                 => 0,
        SLINK_TX_FIFO                => 0,
        SLINK_RX_FIFO                => 0,
        XIRQ_NUM_CH                  => 0,
        IO_GPIO_EN                   => false,
        IO_MTIME_EN                  => false,
        IO_UART0_EN                  => false,
        IO_UART1_EN                  => false,
        IO_SPI_EN                    => false,
        IO_TWI_EN                    => false,
        IO_PWM_NUM_CH                => 0,
        IO_WDT_EN                    => false,
        IO_TRNG_EN                   => false,
        IO_CFS_EN                    => false,
        IO_NEOLED_EN                 => false,
        IO_GPTMR_EN                  => false,
        IO_XIP_EN                    => false
    )
    port map (
        clk_i                        => clock,
        rstn_i                       => reset_n,

        jtag_trst_i                  => jtag_trst_i,
        jtag_tck_i                   => jtag_tck_i,
        jtag_tdi_i                   => jtag_tdi_i,
        jtag_tdo_o                   => jtag_tdo_o,
        jtag_tms_i                   => jtag_tms_i,

        wb_tag_o                     => wb_tag_o,
        wb_adr_o                     => wb_adr_o,
        wb_dat_i                     => wb_dat_i,
        wb_dat_o                     => wb_dat_o,
        wb_we_o                      => wb_we_o,
        wb_sel_o                     => wb_sel_o,
        wb_stb_o                     => wb_stb_o,
        wb_cyc_o                     => wb_cyc_o,
        wb_ack_i                     => wb_ack_i,
        wb_err_i                     => wb_err_i,

        msw_irq_i                    => '0',
        mext_irq_i                   => irq_i
    );
    
    i_wb_to_mem_and_io: entity work.wishbone2memio
    generic map (
        g_tag     => g_tag
    )
    port map (
        clock     => clock,
        reset     => reset,
        wb_tag_o  => wb_tag_o,
        wb_adr_o  => wb_adr_o,
        wb_dat_i  => wb_dat_i,
        wb_dat_o  => wb_dat_o,
        wb_we_o   => wb_we_o,
        wb_sel_o  => wb_sel_o,
        wb_stb_o  => wb_stb_o,
        wb_cyc_o  => wb_cyc_o,
        wb_ack_i  => wb_ack_i,
        wb_err_i  => wb_err_i,
        io_busy   => io_busy,
        io_req    => io_req,
        io_resp   => io_resp,
        mem_req   => mem_req,
        mem_resp  => mem_resp
    );
    
end architecture;
