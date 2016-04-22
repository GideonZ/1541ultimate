--Legal Notice: (C)2015 Altera Corporation. All rights reserved.  Your
--use of Altera Corporation's design tools, logic functions and other
--software and tools, and its AMPP partner logic functions, and any
--output files any of the foregoing (including device programming or
--simulation files), and any associated documentation or information are
--expressly subject to the terms and conditions of the Altera Program
--License Subscription Agreement or other applicable license agreement,
--including, without limitation, that your use is for the sole purpose
--of programming logic devices manufactured by Altera and sold by Altera
--or its authorized distributors.  Please refer to the applicable
--agreement for further details.


-- turn off superfluous VHDL processor warnings 
-- altera message_level Level1 
-- altera message_off 10034 10035 10036 10037 10230 10240 10030 

library altera;
use altera.altera_europa_support_lib.all;

library altera_mf;
use altera_mf.altera_mf_components.all;

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity ddr2_controller_phy is 
        port (
              -- inputs:
                 signal dqs_delay_ctrl_import : IN STD_LOGIC_VECTOR (5 DOWNTO 0);
                 signal dqs_offset_delay_ctrl : IN STD_LOGIC_VECTOR (5 DOWNTO 0);
                 signal global_reset_n : IN STD_LOGIC;
                 signal hc_scan_ck : IN STD_LOGIC;
                 signal hc_scan_din : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal hc_scan_enable_access : IN STD_LOGIC;
                 signal hc_scan_enable_dm : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal hc_scan_enable_dq : IN STD_LOGIC_VECTOR (7 DOWNTO 0);
                 signal hc_scan_enable_dqs : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal hc_scan_enable_dqs_config : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal hc_scan_update : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal local_address : IN STD_LOGIC_VECTOR (24 DOWNTO 0);
                 signal local_autopch_req : IN STD_LOGIC;
                 signal local_be : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
                 signal local_burstbegin : IN STD_LOGIC;
                 signal local_multicast_req : IN STD_LOGIC;
                 signal local_read_req : IN STD_LOGIC;
                 signal local_refresh_chip : IN STD_LOGIC;
                 signal local_refresh_req : IN STD_LOGIC;
                 signal local_self_rfsh_chip : IN STD_LOGIC;
                 signal local_self_rfsh_req : IN STD_LOGIC;
                 signal local_size : IN STD_LOGIC_VECTOR (2 DOWNTO 0);
                 signal local_wdata : IN STD_LOGIC_VECTOR (15 DOWNTO 0);
                 signal local_write_req : IN STD_LOGIC;
                 signal oct_ctl_rs_value : IN STD_LOGIC_VECTOR (13 DOWNTO 0);
                 signal oct_ctl_rt_value : IN STD_LOGIC_VECTOR (13 DOWNTO 0);
                 signal pll_phasecounterselect : IN STD_LOGIC_VECTOR (3 DOWNTO 0);
                 signal pll_phasestep : IN STD_LOGIC;
                 signal pll_phaseupdown : IN STD_LOGIC;
                 signal pll_reconfig : IN STD_LOGIC;
                 signal pll_reconfig_counter_param : IN STD_LOGIC_VECTOR (2 DOWNTO 0);
                 signal pll_reconfig_counter_type : IN STD_LOGIC_VECTOR (3 DOWNTO 0);
                 signal pll_reconfig_data_in : IN STD_LOGIC_VECTOR (8 DOWNTO 0);
                 signal pll_reconfig_enable : IN STD_LOGIC;
                 signal pll_reconfig_read_param : IN STD_LOGIC;
                 signal pll_reconfig_soft_reset_en_n : IN STD_LOGIC;
                 signal pll_reconfig_write_param : IN STD_LOGIC;
                 signal pll_ref_clk : IN STD_LOGIC;
                 signal soft_reset_n : IN STD_LOGIC;

              -- outputs:
                 signal aux_full_rate_clk : OUT STD_LOGIC;
                 signal aux_half_rate_clk : OUT STD_LOGIC;
                 signal aux_scan_clk : OUT STD_LOGIC;
                 signal aux_scan_clk_reset_n : OUT STD_LOGIC;
                 signal dll_reference_clk : OUT STD_LOGIC;
                 signal dqs_delay_ctrl_export : OUT STD_LOGIC_VECTOR (5 DOWNTO 0);
                 signal ecc_interrupt : OUT STD_LOGIC;
                 signal hc_scan_dout : OUT STD_LOGIC_VECTOR (7 DOWNTO 0);
                 signal local_init_done : OUT STD_LOGIC;
                 signal local_power_down_ack : OUT STD_LOGIC;
                 signal local_rdata : OUT STD_LOGIC_VECTOR (15 DOWNTO 0);
                 signal local_rdata_valid : OUT STD_LOGIC;
                 signal local_ready : OUT STD_LOGIC;
                 signal local_refresh_ack : OUT STD_LOGIC;
                 signal local_self_rfsh_ack : OUT STD_LOGIC;
                 signal mem_addr : OUT STD_LOGIC_VECTOR (13 DOWNTO 0);
                 signal mem_ba : OUT STD_LOGIC_VECTOR (1 DOWNTO 0);
                 signal mem_cas_n : OUT STD_LOGIC;
                 signal mem_cke : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal mem_clk : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal mem_clk_n : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal mem_cs_n : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal mem_dm : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal mem_dq : INOUT STD_LOGIC_VECTOR (7 DOWNTO 0);
                 signal mem_dqs : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal mem_dqsn : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal mem_odt : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
                 signal mem_ras_n : OUT STD_LOGIC;
                 signal mem_reset_n : OUT STD_LOGIC;
                 signal mem_we_n : OUT STD_LOGIC;
                 signal phy_clk : OUT STD_LOGIC;
                 signal pll_phase_done : OUT STD_LOGIC;
                 signal pll_reconfig_busy : OUT STD_LOGIC;
                 signal pll_reconfig_clk : OUT STD_LOGIC;
                 signal pll_reconfig_data_out : OUT STD_LOGIC_VECTOR (8 DOWNTO 0);
                 signal pll_reconfig_reset : OUT STD_LOGIC;
                 signal reset_phy_clk_n : OUT STD_LOGIC;
                 signal reset_request_n : OUT STD_LOGIC
              );
end entity ddr2_controller_phy;


architecture europa of ddr2_controller_phy is
  component ddr2_alt_mem_ddrx_controller_top is
PORT (
    signal afi_we_n : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal local_init_done : OUT STD_LOGIC;
        signal afi_ctl_long_idle : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal afi_ba : OUT STD_LOGIC_VECTOR (1 DOWNTO 0);
        signal afi_cas_n : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ecc_interrupt : OUT STD_LOGIC;
        signal afi_ctl_refresh_done : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal csr_waitrequest : OUT STD_LOGIC;
        signal afi_rdata_en_full : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal afi_cal_byte_lane_sel_n : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal csr_rdata : OUT STD_LOGIC_VECTOR (31 DOWNTO 0);
        signal afi_ras_n : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal afi_cke : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal afi_mem_clk_disable : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal afi_dm : OUT STD_LOGIC_VECTOR (1 DOWNTO 0);
        signal csr_rdata_valid : OUT STD_LOGIC;
        signal afi_addr : OUT STD_LOGIC_VECTOR (13 DOWNTO 0);
        signal afi_odt : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal afi_rst_n : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal afi_rdata_en : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal local_ready : OUT STD_LOGIC;
        signal afi_dqs_burst : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal local_readdata : OUT STD_LOGIC_VECTOR (15 DOWNTO 0);
        signal afi_wdata_valid : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal afi_cal_req : OUT STD_LOGIC;
        signal local_powerdn_ack : OUT STD_LOGIC;
        signal local_self_rfsh_ack : OUT STD_LOGIC;
        signal local_refresh_ack : OUT STD_LOGIC;
        signal afi_wdata : OUT STD_LOGIC_VECTOR (15 DOWNTO 0);
        signal afi_cs_n : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal local_readdatavalid : OUT STD_LOGIC;
        signal local_write : IN STD_LOGIC;
        signal local_autopch_req : IN STD_LOGIC;
        signal afi_cal_fail : IN STD_LOGIC;
        signal local_beginbursttransfer : IN STD_LOGIC;
        signal local_refresh_req : IN STD_LOGIC;
        signal reset_n : IN STD_LOGIC;
        signal csr_be : IN STD_LOGIC_VECTOR (3 DOWNTO 0);
        signal local_multicast : IN STD_LOGIC;
        signal half_clk : IN STD_LOGIC;
        signal local_priority : IN STD_LOGIC;
        signal local_self_rfsh_chip : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal afi_rdata : IN STD_LOGIC_VECTOR (15 DOWNTO 0);
        signal afi_cal_success : IN STD_LOGIC;
        signal csr_addr : IN STD_LOGIC_VECTOR (15 DOWNTO 0);
        signal local_read : IN STD_LOGIC;
        signal local_address : IN STD_LOGIC_VECTOR (24 DOWNTO 0);
        signal afi_seq_busy : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal local_byteenable : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        signal local_self_rfsh_req : IN STD_LOGIC;
        signal local_refresh_chip : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal local_powerdn_req : IN STD_LOGIC;
        signal csr_read_req : IN STD_LOGIC;
        signal csr_beginbursttransfer : IN STD_LOGIC;
        signal local_burstcount : IN STD_LOGIC_VECTOR (2 DOWNTO 0);
        signal local_writedata : IN STD_LOGIC_VECTOR (15 DOWNTO 0);
        signal afi_wlat : IN STD_LOGIC_VECTOR (4 DOWNTO 0);
        signal csr_wdata : IN STD_LOGIC_VECTOR (31 DOWNTO 0);
        signal afi_rdata_valid : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal clk : IN STD_LOGIC;
        signal csr_write_req : IN STD_LOGIC;
        signal csr_burst_count : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal afi_rlat : IN STD_LOGIC_VECTOR (4 DOWNTO 0)
      );
  end component ddr2_alt_mem_ddrx_controller_top;
  component ddr2_phy is
PORT (
    signal mem_cke : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_cal_fail : OUT STD_LOGIC;
        signal mem_addr : OUT STD_LOGIC_VECTOR (13 DOWNTO 0);
        signal mem_dm : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal aux_full_rate_clk : OUT STD_LOGIC;
        signal mem_dq : INOUT STD_LOGIC_VECTOR (7 DOWNTO 0);
        signal mem_ras_n : OUT STD_LOGIC;
        signal dbg_rd_data : OUT STD_LOGIC_VECTOR (31 DOWNTO 0);
        signal mem_cs_n : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal mem_ba : OUT STD_LOGIC_VECTOR (1 DOWNTO 0);
        signal mem_reset_n : OUT STD_LOGIC;
        signal dbg_waitrequest : OUT STD_LOGIC;
        signal ctl_rdata : OUT STD_LOGIC_VECTOR (15 DOWNTO 0);
        signal ctl_wlat : OUT STD_LOGIC_VECTOR (4 DOWNTO 0);
        signal ctl_rdata_valid : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_clk : OUT STD_LOGIC;
        signal aux_half_rate_clk : OUT STD_LOGIC;
        signal ctl_cal_success : OUT STD_LOGIC;
        signal mem_dqs_n : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal mem_we_n : OUT STD_LOGIC;
        signal mem_odt : OUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal mem_cas_n : OUT STD_LOGIC;
        signal ctl_rlat : OUT STD_LOGIC_VECTOR (4 DOWNTO 0);
        signal mem_clk : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_reset_n : OUT STD_LOGIC;
        signal reset_request_n : OUT STD_LOGIC;
        signal mem_dqs : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal mem_clk_n : INOUT STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_doing_rd : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_dm : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        signal soft_reset_n : IN STD_LOGIC;
        signal dbg_clk : IN STD_LOGIC;
        signal ctl_cal_req : IN STD_LOGIC;
        signal global_reset_n : IN STD_LOGIC;
        signal dbg_cs : IN STD_LOGIC;
        signal ctl_mem_clk_disable : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_rst_n : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_we_n : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_addr : IN STD_LOGIC_VECTOR (13 DOWNTO 0);
        signal ctl_odt : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_ba : IN STD_LOGIC_VECTOR (1 DOWNTO 0);
        signal dbg_addr : IN STD_LOGIC_VECTOR (12 DOWNTO 0);
        signal ctl_wdata : IN STD_LOGIC_VECTOR (15 DOWNTO 0);
        signal dbg_wr : IN STD_LOGIC;
        signal ctl_cke : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_dqs_burst : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal pll_ref_clk : IN STD_LOGIC;
        signal ctl_cs_n : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_cas_n : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal ctl_cal_byte_lane_sel_n : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal dbg_rd : IN STD_LOGIC;
        signal ctl_ras_n : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal dbg_wr_data : IN STD_LOGIC_VECTOR (31 DOWNTO 0);
        signal ctl_wdata_valid : IN STD_LOGIC_VECTOR (0 DOWNTO 0);
        signal dbg_reset_n : IN STD_LOGIC
      );
  end component ddr2_phy;
                signal afi_addr :  STD_LOGIC_VECTOR (13 DOWNTO 0);
                signal afi_ba :  STD_LOGIC_VECTOR (1 DOWNTO 0);
                signal afi_cas_n :  STD_LOGIC;
                signal afi_cke :  STD_LOGIC;
                signal afi_cs_n :  STD_LOGIC;
                signal afi_ctl_long_idle :  STD_LOGIC;
                signal afi_ctl_refresh_done :  STD_LOGIC;
                signal afi_dm :  STD_LOGIC_VECTOR (1 DOWNTO 0);
                signal afi_dqs_burst :  STD_LOGIC;
                signal afi_odt :  STD_LOGIC;
                signal afi_ras_n :  STD_LOGIC;
                signal afi_rdata :  STD_LOGIC_VECTOR (15 DOWNTO 0);
                signal afi_rdata_en :  STD_LOGIC;
                signal afi_rdata_en_full :  STD_LOGIC;
                signal afi_rdata_valid :  STD_LOGIC;
                signal afi_rst_n :  STD_LOGIC;
                signal afi_wdata :  STD_LOGIC_VECTOR (15 DOWNTO 0);
                signal afi_wdata_valid :  STD_LOGIC;
                signal afi_we_n :  STD_LOGIC;
                signal afi_wlat :  STD_LOGIC_VECTOR (4 DOWNTO 0);
                signal csr_rdata_sig :  STD_LOGIC_VECTOR (31 DOWNTO 0);
                signal csr_rdata_valid_sig :  STD_LOGIC;
                signal csr_waitrequest_sig :  STD_LOGIC;
                signal ctl_cal_byte_lane_sel_n :  STD_LOGIC;
                signal ctl_cal_fail :  STD_LOGIC;
                signal ctl_cal_req :  STD_LOGIC;
                signal ctl_cal_success :  STD_LOGIC;
                signal ctl_clk :  STD_LOGIC;
                signal ctl_mem_clk_disable :  STD_LOGIC;
                signal ctl_rlat :  STD_LOGIC_VECTOR (4 DOWNTO 0);
                signal dbg_rd_data_sig :  STD_LOGIC_VECTOR (31 DOWNTO 0);
                signal dbg_waitrequest_sig :  STD_LOGIC;
                signal internal_aux_full_rate_clk :  STD_LOGIC;
                signal internal_aux_half_rate_clk :  STD_LOGIC;
                signal internal_ecc_interrupt :  STD_LOGIC;
                signal internal_local_init_done :  STD_LOGIC;
                signal internal_local_power_down_ack :  STD_LOGIC;
                signal internal_local_rdata :  STD_LOGIC_VECTOR (15 DOWNTO 0);
                signal internal_local_rdata_valid :  STD_LOGIC;
                signal internal_local_ready :  STD_LOGIC;
                signal internal_local_refresh_ack :  STD_LOGIC;
                signal internal_local_self_rfsh_ack :  STD_LOGIC;
                signal internal_mem_addr :  STD_LOGIC_VECTOR (13 DOWNTO 0);
                signal internal_mem_ba :  STD_LOGIC_VECTOR (1 DOWNTO 0);
                signal internal_mem_cas_n :  STD_LOGIC;
                signal internal_mem_cke :  STD_LOGIC_VECTOR (0 DOWNTO 0);
                signal internal_mem_cs_n :  STD_LOGIC_VECTOR (0 DOWNTO 0);
                signal internal_mem_dm :  STD_LOGIC_VECTOR (0 DOWNTO 0);
                signal internal_mem_odt :  STD_LOGIC_VECTOR (0 DOWNTO 0);
                signal internal_mem_ras_n :  STD_LOGIC;
                signal internal_mem_reset_n :  STD_LOGIC;
                signal internal_mem_we_n :  STD_LOGIC;
                signal internal_reset_request_n :  STD_LOGIC;
                signal reset_ctl_clk_n :  STD_LOGIC;

begin

  phy_clk <= ctl_clk;
  reset_phy_clk_n <= reset_ctl_clk_n;
  ddr2_alt_mem_ddrx_controller_top_inst : ddr2_alt_mem_ddrx_controller_top
    port map(
            afi_addr => afi_addr,
            afi_ba => afi_ba,
            afi_cal_byte_lane_sel_n(0) => ctl_cal_byte_lane_sel_n,
            afi_cal_fail => ctl_cal_fail,
            afi_cal_req => ctl_cal_req,
            afi_cal_success => ctl_cal_success,
            afi_cas_n(0) => afi_cas_n,
            afi_cke(0) => afi_cke,
            afi_cs_n(0) => afi_cs_n,
            afi_ctl_long_idle(0) => afi_ctl_long_idle,
            afi_ctl_refresh_done(0) => afi_ctl_refresh_done,
            afi_dm => afi_dm,
            afi_dqs_burst(0) => afi_dqs_burst,
            afi_mem_clk_disable(0) => ctl_mem_clk_disable,
            afi_odt(0) => afi_odt,
            afi_ras_n(0) => afi_ras_n,
            afi_rdata => afi_rdata,
            afi_rdata_en(0) => afi_rdata_en,
            afi_rdata_en_full(0) => afi_rdata_en_full,
            afi_rdata_valid => A_TOSTDLOGICVECTOR(afi_rdata_valid),
            afi_rlat => ctl_rlat,
            afi_rst_n(0) => afi_rst_n,
            afi_seq_busy => A_TOSTDLOGICVECTOR(std_logic'('0')),
            afi_wdata => afi_wdata,
            afi_wdata_valid(0) => afi_wdata_valid,
            afi_we_n(0) => afi_we_n,
            afi_wlat => afi_wlat,
            clk => ctl_clk,
            csr_addr => std_logic_vector'("0000000000000000"),
            csr_be => std_logic_vector'("0000"),
            csr_beginbursttransfer => std_logic'('0'),
            csr_burst_count => A_TOSTDLOGICVECTOR(std_logic'('0')),
            csr_rdata => csr_rdata_sig,
            csr_rdata_valid => csr_rdata_valid_sig,
            csr_read_req => std_logic'('0'),
            csr_waitrequest => csr_waitrequest_sig,
            csr_wdata => std_logic_vector'("00000000000000000000000000000000"),
            csr_write_req => std_logic'('0'),
            ecc_interrupt => internal_ecc_interrupt,
            half_clk => internal_aux_half_rate_clk,
            local_address => local_address,
            local_autopch_req => local_autopch_req,
            local_beginbursttransfer => local_burstbegin,
            local_burstcount => local_size,
            local_byteenable => local_be,
            local_init_done => internal_local_init_done,
            local_multicast => local_multicast_req,
            local_powerdn_ack => internal_local_power_down_ack,
            local_powerdn_req => std_logic'('0'),
            local_priority => std_logic'('1'),
            local_read => local_read_req,
            local_readdata => internal_local_rdata,
            local_readdatavalid => internal_local_rdata_valid,
            local_ready => internal_local_ready,
            local_refresh_ack => internal_local_refresh_ack,
            local_refresh_chip => A_TOSTDLOGICVECTOR(local_refresh_chip),
            local_refresh_req => local_refresh_req,
            local_self_rfsh_ack => internal_local_self_rfsh_ack,
            local_self_rfsh_chip => A_TOSTDLOGICVECTOR(local_self_rfsh_chip),
            local_self_rfsh_req => local_self_rfsh_req,
            local_write => local_write_req,
            local_writedata => local_wdata,
            reset_n => reset_ctl_clk_n
    );

  ddr2_phy_inst : ddr2_phy
    port map(
            aux_full_rate_clk => internal_aux_full_rate_clk,
            aux_half_rate_clk => internal_aux_half_rate_clk,
            ctl_addr => afi_addr,
            ctl_ba => afi_ba,
            ctl_cal_byte_lane_sel_n => A_TOSTDLOGICVECTOR(ctl_cal_byte_lane_sel_n),
            ctl_cal_fail => ctl_cal_fail,
            ctl_cal_req => ctl_cal_req,
            ctl_cal_success => ctl_cal_success,
            ctl_cas_n => A_TOSTDLOGICVECTOR(afi_cas_n),
            ctl_cke => A_TOSTDLOGICVECTOR(afi_cke),
            ctl_clk => ctl_clk,
            ctl_cs_n => A_TOSTDLOGICVECTOR(afi_cs_n),
            ctl_dm => afi_dm,
            ctl_doing_rd => A_TOSTDLOGICVECTOR(afi_rdata_en),
            ctl_dqs_burst => A_TOSTDLOGICVECTOR(afi_dqs_burst),
            ctl_mem_clk_disable => A_TOSTDLOGICVECTOR(ctl_mem_clk_disable),
            ctl_odt => A_TOSTDLOGICVECTOR(afi_odt),
            ctl_ras_n => A_TOSTDLOGICVECTOR(afi_ras_n),
            ctl_rdata => afi_rdata,
            ctl_rdata_valid(0) => afi_rdata_valid,
            ctl_reset_n => reset_ctl_clk_n,
            ctl_rlat => ctl_rlat,
            ctl_rst_n => A_TOSTDLOGICVECTOR(afi_rst_n),
            ctl_wdata => afi_wdata,
            ctl_wdata_valid => A_TOSTDLOGICVECTOR(afi_wdata_valid),
            ctl_we_n => A_TOSTDLOGICVECTOR(afi_we_n),
            ctl_wlat => afi_wlat,
            dbg_addr => std_logic_vector'("0000000000000"),
            dbg_clk => ctl_clk,
            dbg_cs => std_logic'('0'),
            dbg_rd => std_logic'('0'),
            dbg_rd_data => dbg_rd_data_sig,
            dbg_reset_n => reset_ctl_clk_n,
            dbg_waitrequest => dbg_waitrequest_sig,
            dbg_wr => std_logic'('0'),
            dbg_wr_data => std_logic_vector'("00000000000000000000000000000000"),
            global_reset_n => global_reset_n,
            mem_addr => internal_mem_addr,
            mem_ba => internal_mem_ba,
            mem_cas_n => internal_mem_cas_n,
            mem_cke(0) => internal_mem_cke(0),
            mem_clk(0) => mem_clk(0),
            mem_clk_n(0) => mem_clk_n(0),
            mem_cs_n(0) => internal_mem_cs_n(0),
            mem_dm(0) => internal_mem_dm(0),
            mem_dq => mem_dq,
            mem_dqs(0) => mem_dqs(0),
            mem_dqs_n(0) => mem_dqsn(0),
            mem_odt(0) => internal_mem_odt(0),
            mem_ras_n => internal_mem_ras_n,
            mem_reset_n => internal_mem_reset_n,
            mem_we_n => internal_mem_we_n,
            pll_ref_clk => pll_ref_clk,
            reset_request_n => internal_reset_request_n,
            soft_reset_n => soft_reset_n
    );

  --<< start europa
  --vhdl renameroo for output signals
  aux_full_rate_clk <= internal_aux_full_rate_clk;
  --vhdl renameroo for output signals
  aux_half_rate_clk <= internal_aux_half_rate_clk;
  --vhdl renameroo for output signals
  ecc_interrupt <= internal_ecc_interrupt;
  --vhdl renameroo for output signals
  local_init_done <= internal_local_init_done;
  --vhdl renameroo for output signals
  local_power_down_ack <= internal_local_power_down_ack;
  --vhdl renameroo for output signals
  local_rdata <= internal_local_rdata;
  --vhdl renameroo for output signals
  local_rdata_valid <= internal_local_rdata_valid;
  --vhdl renameroo for output signals
  local_ready <= internal_local_ready;
  --vhdl renameroo for output signals
  local_refresh_ack <= internal_local_refresh_ack;
  --vhdl renameroo for output signals
  local_self_rfsh_ack <= internal_local_self_rfsh_ack;
  --vhdl renameroo for output signals
  mem_addr <= internal_mem_addr;
  --vhdl renameroo for output signals
  mem_ba <= internal_mem_ba;
  --vhdl renameroo for output signals
  mem_cas_n <= internal_mem_cas_n;
  --vhdl renameroo for output signals
  mem_cke <= internal_mem_cke;
  --vhdl renameroo for output signals
  mem_cs_n <= internal_mem_cs_n;
  --vhdl renameroo for output signals
  mem_dm <= internal_mem_dm;
  --vhdl renameroo for output signals
  mem_odt <= internal_mem_odt;
  --vhdl renameroo for output signals
  mem_ras_n <= internal_mem_ras_n;
  --vhdl renameroo for output signals
  mem_reset_n <= internal_mem_reset_n;
  --vhdl renameroo for output signals
  mem_we_n <= internal_mem_we_n;
  --vhdl renameroo for output signals
  reset_request_n <= internal_reset_request_n;

end europa;

