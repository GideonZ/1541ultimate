// (C) 2001-2015 Altera Corporation. All rights reserved.
// Your use of Altera Corporation's design tools, logic functions and other 
// software and tools, and its AMPP partner logic functions, and any output 
// files any of the foregoing (including device programming or simulation 
// files), and any associated documentation or information are expressly subject 
// to the terms and conditions of the Altera Program License Subscription 
// Agreement, Altera MegaCore Function License Agreement, or other applicable 
// license agreement, including, without limitation, that your use is for the 
// sole purpose of programming logic devices manufactured by Altera and sold by 
// Altera or its authorized distributors.  Please refer to the applicable 
// agreement for further details.



`timescale 1 ps / 1 ps

(* altera_attribute = "-name IP_TOOL_NAME altera_mem_if_nextgen_ddr2_controller_core; -name IP_TOOL_VERSION 15.1; -name FITTER_ADJUST_HC_SHORT_PATH_GUARDBAND 100" *)
module alt_mem_if_nextgen_ddr2_controller_core (
    afi_clk,
    afi_reset_n,
    afi_half_clk,
    itf_cmd_ready,
    itf_cmd_valid,
    itf_cmd,
    itf_cmd_address,
    itf_cmd_burstlen,
    itf_cmd_id,
    itf_cmd_priority,
    itf_cmd_autopercharge,
    itf_cmd_multicast,
    itf_wr_data_ready,
    itf_wr_data_valid,
    itf_wr_data,
    itf_wr_data_byte_en,
    itf_wr_data_begin,
    itf_wr_data_last,
    itf_wr_data_id,
    itf_rd_data_ready,
    itf_rd_data_valid,
    itf_rd_data,
    itf_rd_data_error,
    itf_rd_data_begin,
    itf_rd_data_last,
    itf_rd_data_id,
    afi_cs_n,
    afi_cke,
    afi_odt,
    afi_addr,
    afi_ba,
    afi_ras_n,
    afi_cas_n,
    afi_we_n,
    afi_dqs_burst,
    afi_wdata_valid,
    afi_wdata,
    afi_dm,
    afi_wlat,
    afi_rdata_en,
    afi_rdata_en_full,
    afi_rdata,
    afi_rdata_valid,
    afi_rlat,
    afi_cal_success,
    afi_cal_fail,
    afi_cal_req,
    afi_init_req,
	afi_mem_clk_disable,
    afi_seq_busy,
    local_refresh_ack,
    local_powerdn_ack,
    local_self_rfsh_ack,
    local_deep_powerdn_ack,
    local_refresh_req,
    local_refresh_chip,
    local_self_rfsh_req,
    local_self_rfsh_chip,
    local_deep_powerdn_req,
    local_deep_powerdn_chip,
    local_multicast,
    local_priority,
    local_init_done,
    local_cal_success,
    local_cal_fail,
    ecc_interrupt,
    csr_read_req,
    csr_write_req,
    csr_addr,
    csr_wdata,
    csr_rdata,
    csr_be,
    csr_rdata_valid,
    csr_waitrequest
);


    parameter   AVL_SIZE_WIDTH                      = 0;
    parameter   AVL_ADDR_WIDTH                      = 0;
    parameter   AVL_DATA_WIDTH                      = 0;
    parameter   LOCAL_ID_WIDTH                      = 0;
    parameter   AVL_BE_WIDTH                        = 0;
    parameter   LOCAL_CS_WIDTH                      = 0;
    parameter   MEM_IF_ADDR_WIDTH                   = 0;
    parameter   MEM_IF_CLK_PAIR_COUNT               = 0;
    parameter   LOCAL_IF_TYPE                       = "AVALON";
    parameter   DWIDTH_RATIO                        = 0;
    parameter   CTL_ODT_ENABLED                     = 0;
    parameter   CTL_OUTPUT_REGD                     = 0;
    parameter   CTL_TBP_NUM                         = 0;
    parameter   WRBUFFER_ADDR_WIDTH                 = 0;
    parameter   RDBUFFER_ADDR_WIDTH                 = 0;
    parameter   MEM_IF_CS_WIDTH                     = 0;
    parameter   MEM_IF_CHIP_BITS                    = 0;
    parameter   MEM_IF_BANKADDR_WIDTH               = 0;
    parameter   MEM_IF_ROW_ADDR_WIDTH               = 0;
    parameter   MEM_IF_COL_ADDR_WIDTH               = 0;
    parameter   MEM_IF_ODT_WIDTH                    = 0;
    parameter   MEM_IF_CLK_EN_WIDTH                 = 0;
    parameter   MEM_IF_DQS_WIDTH                    = 0;
    parameter   MEM_IF_DQ_WIDTH                     = 0;
    parameter   MEM_IF_DM_WIDTH                     = 0;

    parameter   MAX_MEM_IF_CS_WIDTH                 = 30;
    parameter   MAX_MEM_IF_CHIP                     = 4;
    parameter   MAX_MEM_IF_BANKADDR_WIDTH           = 3;
    parameter   MAX_MEM_IF_ROWADDR_WIDTH            = 16;
    parameter   MAX_MEM_IF_COLADDR_WIDTH            = 12;
    parameter   MAX_MEM_IF_ODT_WIDTH                = 1;
    parameter   MAX_MEM_IF_DQS_WIDTH                = 5;
    parameter   MAX_MEM_IF_DQ_WIDTH                 = 40;
    parameter   MAX_MEM_IF_MASK_WIDTH               = 5;
    parameter   MAX_LOCAL_DATA_WIDTH                = 80;

    parameter   CFG_TYPE                            = 0;
    parameter   CFG_INTERFACE_WIDTH                 = 0;
    parameter   CFG_BURST_LENGTH                    = 0;
    parameter   CFG_REORDER_DATA                    = 0;
    parameter   CFG_DATA_REORDERING_TYPE            = "INTER_ROW";
    parameter   CFG_STARVE_LIMIT                    = 0;
    parameter   CFG_ADDR_ORDER                      = 0;

    parameter   MEM_WTCL_INT                        = 0;
    parameter   MEM_ADD_LAT                         = 0;
    parameter   MEM_TCL                             = 0;
    parameter   MEM_TRRD                            = 0;
    parameter   MEM_TFAW                            = 0;
    parameter   MEM_TRFC                            = 0;
    parameter   MEM_TREFI                           = 0;
    parameter   MEM_TRCD                            = 0;
    parameter   MEM_TRP                             = 0;
    parameter   MEM_TWR                             = 0;
    parameter   MEM_TWTR                            = 0;
    parameter   MEM_TRTP                            = 0;
    parameter   MEM_TRAS                            = 0;
    parameter   MEM_TRC                             = 0;
    parameter   CFG_TCCD                            = 2;
    parameter   MEM_AUTO_PD_CYCLES                  = 0;
    parameter   MEM_IF_RD_TO_WR_TURNAROUND_OCT      = 0;
    parameter   MEM_IF_WR_TO_RD_TURNAROUND_OCT      = 0;
    parameter   CTL_RD_TO_PCH_EXTRA_CLK             = 0;
    parameter   CTL_RD_TO_RD_DIFF_CHIP_EXTRA_CLK    = 0;
    parameter   CTL_WR_TO_WR_DIFF_CHIP_EXTRA_CLK    = 0;

    parameter   AFI_ADDR_WIDTH                      = 0;
    parameter   AFI_BANKADDR_WIDTH                  = 0;
    parameter   AFI_CONTROL_WIDTH                   = 0;
    parameter   AFI_CS_WIDTH                        = 0;
    parameter   AFI_CLK_EN_WIDTH                    = 0;
    parameter   AFI_ODT_WIDTH                       = 0;
    parameter   AFI_DM_WIDTH                        = 0;
    parameter   AFI_DQ_WIDTH                        = 0;
    parameter   AFI_WRITE_DQS_WIDTH                 = 0;
    parameter   AFI_RATE_RATIO                      = 0;
    parameter   AFI_WLAT_WIDTH                      = 0;
    parameter   AFI_RLAT_WIDTH                      = 0;
    parameter   AFI_RRANK_WIDTH                     = 0;
    parameter   AFI_WRANK_WIDTH                     = 0;

    parameter   USE_SHADOW_REGS                     = 0;
    parameter   CFG_SELF_RFSH_EXIT_CYCLES           = 0;
    parameter   CFG_PDN_EXIT_CYCLES                 = 0;
    parameter   CFG_POWER_SAVING_EXIT_CYCLES        = 0;
    parameter   CFG_MEM_CLK_ENTRY_CYCLES            = 0;
    parameter   MEM_TMRD_CK                         = 0;
    parameter   CTL_ECC_ENABLED                     = 0;
    parameter   CTL_ECC_AUTO_CORRECTION_ENABLED     = 0;
    parameter   CTL_ECC_MULTIPLES_16_24_40_72       = 1;
    parameter   CTL_ENABLE_BURST_INTERRUPT_INT      = 0;
    parameter   CTL_ENABLE_BURST_TERMINATE_INT      = 0;
    parameter   CTL_ENABLE_WDATA_PATH_LATENCY       = 0;
    parameter   CFG_GEN_SBE                         = 0;
    parameter   CFG_GEN_DBE                         = 0;
    parameter   CFG_ENABLE_INTR                     = 0;
    parameter   CFG_MASK_SBE_INTR                   = 0;
    parameter   CFG_MASK_DBE_INTR                   = 0;
    parameter   CFG_MASK_CORRDROP_INTR              = 0;
    parameter   CFG_CLR_INTR                        = 0;
    parameter   CTL_USR_REFRESH                     = 0;
    parameter   CTL_REGDIMM_ENABLED                 = 0;
    parameter   CFG_WRITE_ODT_CHIP                  = 0;
    parameter   CFG_READ_ODT_CHIP                   = 0;
    parameter   CFG_PORT_WIDTH_WRITE_ODT_CHIP       = 0;
    parameter   CFG_PORT_WIDTH_READ_ODT_CHIP        = 0;
    parameter   MEM_IF_CKE_WIDTH                    = 0;
    parameter   CFG_ENABLE_NO_DM                    = 0;
    parameter   CSR_BE_WIDTH                        = 4;

    parameter CFG_ERRCMD_FIFO_REG                   = 0;
    parameter CFG_ECC_DECODER_REG                   = 0; 	
    parameter   CTL_CSR_ENABLED                     = 0;
    parameter   CSR_ADDR_WIDTH                      = 8;
    parameter   CSR_DATA_WIDTH                      = 32;
    parameter   ENABLE_BURST_MERGE        	    = 0;
    parameter MAX10_CFG                         = 0;
localparam AFI_RDATA_EN_WIDTH                                   = (MEM_IF_DQS_WIDTH * (DWIDTH_RATIO / 2));


input                                                   afi_clk;
input                                                   afi_reset_n;
input                                                   afi_half_clk;

output                                                  itf_cmd_ready;
input                                                   itf_cmd_valid;
input                                                   itf_cmd;
input   [AVL_ADDR_WIDTH                     - 1 : 0]    itf_cmd_address;
input   [AVL_SIZE_WIDTH                     - 1 : 0]    itf_cmd_burstlen;
input   [LOCAL_ID_WIDTH                     - 1 : 0]    itf_cmd_id;
input                                                   itf_cmd_priority;
input                                                   itf_cmd_autopercharge;
input                                                   itf_cmd_multicast;
output                                                  itf_wr_data_ready;
input                                                   itf_wr_data_valid;
input   [AVL_DATA_WIDTH                     - 1 : 0]    itf_wr_data;
input   [AVL_BE_WIDTH                       - 1 : 0]    itf_wr_data_byte_en;
input                                                   itf_wr_data_begin;
input                                                   itf_wr_data_last;
input   [LOCAL_ID_WIDTH                     - 1 : 0]    itf_wr_data_id;
input                                                   itf_rd_data_ready;
output                                                  itf_rd_data_valid;
output  [AVL_DATA_WIDTH                     - 1 : 0]    itf_rd_data;
output                                                  itf_rd_data_error;
output                                                  itf_rd_data_begin;
output                                                  itf_rd_data_last;
output  [LOCAL_ID_WIDTH                     - 1 : 0]    itf_rd_data_id;

output  [AFI_CS_WIDTH                       - 1 : 0]    afi_cs_n;
output  [AFI_CLK_EN_WIDTH                   - 1 : 0]    afi_cke;
output  [AFI_ODT_WIDTH                      - 1 : 0]    afi_odt;
output  [AFI_ADDR_WIDTH                     - 1 : 0]    afi_addr;
output  [AFI_BANKADDR_WIDTH                 - 1 : 0]    afi_ba;
output  [AFI_CONTROL_WIDTH                  - 1 : 0]    afi_ras_n;
output  [AFI_CONTROL_WIDTH                  - 1 : 0]    afi_cas_n;
output  [AFI_CONTROL_WIDTH                  - 1 : 0]    afi_we_n;
output  [AFI_WRITE_DQS_WIDTH                - 1 : 0]    afi_dqs_burst;
output  [AFI_WRITE_DQS_WIDTH                - 1 : 0]    afi_wdata_valid;
output  [AFI_DQ_WIDTH                       - 1 : 0]    afi_wdata;
output  [AFI_DM_WIDTH                       - 1 : 0]    afi_dm;
input   [AFI_WLAT_WIDTH                     - 1 : 0]    afi_wlat;
output  [AFI_RATE_RATIO                     - 1 : 0]    afi_rdata_en;
wire	[AFI_RDATA_EN_WIDTH 			-1 : 0]	int_afi_rdata_en;
output  [AFI_RATE_RATIO                     - 1 : 0]    afi_rdata_en_full;
wire	[AFI_RDATA_EN_WIDTH 			-1 : 0]	int_afi_rdata_en_full;
input   [AFI_DQ_WIDTH                       - 1 : 0]    afi_rdata;
input   [AFI_RATE_RATIO                     - 1 : 0]    afi_rdata_valid;
input   [AFI_RLAT_WIDTH                     - 1 : 0]    afi_rlat;
input                                                   afi_cal_success;
input                                                   afi_cal_fail;
output                                                  afi_cal_req;
output                                                  afi_init_req;
output  [MEM_IF_CLK_PAIR_COUNT              - 1 : 0]    afi_mem_clk_disable;

wire  [(MEM_IF_DQS_WIDTH*MEM_IF_CS_WIDTH) - 1 : 0]    afi_cal_byte_lane_sel_n;
wire  [MEM_IF_CS_WIDTH                    - 1 : 0]    afi_ctl_refresh_done;
input [MEM_IF_CS_WIDTH                    - 1 : 0]    afi_seq_busy;
wire  [MEM_IF_CS_WIDTH                    - 1 : 0]    afi_ctl_long_idle;

output                                                  local_refresh_ack;
output                                                  local_powerdn_ack;
output                                                  local_self_rfsh_ack;
output                                                  local_deep_powerdn_ack;
input                                                   local_refresh_req;
input   [MEM_IF_CS_WIDTH                    - 1 : 0]    local_refresh_chip;
input                                                   local_self_rfsh_req;
input   [MEM_IF_CS_WIDTH                    - 1 : 0]    local_self_rfsh_chip;
input                                                   local_deep_powerdn_req;
input   [MEM_IF_CS_WIDTH                    - 1 : 0]    local_deep_powerdn_chip;
input                                                   local_multicast;
input                                                   local_priority;

output                                                  local_init_done;
output                                                  local_cal_success;
output                                                  local_cal_fail;

output                                                  ecc_interrupt;
input                                                   csr_read_req;
input                                                   csr_write_req;
input   [CSR_ADDR_WIDTH                     - 1 : 0]    csr_addr;
input   [CSR_DATA_WIDTH                     - 1 : 0]    csr_wdata;
output  [CSR_DATA_WIDTH                     - 1 : 0]    csr_rdata;
input   [CSR_BE_WIDTH                       - 1 : 0]    csr_be;
output                                                  csr_rdata_valid;
output                                                  csr_waitrequest;

alt_mem_ddrx_controller_st_top # (
    .LOCAL_SIZE_WIDTH                   ( AVL_SIZE_WIDTH                     ),
    .LOCAL_ADDR_WIDTH                   ( AVL_ADDR_WIDTH                     ),
    .LOCAL_DATA_WIDTH                   ( AVL_DATA_WIDTH                     ),
    .LOCAL_BE_WIDTH                     ( AVL_BE_WIDTH                       ),
    .LOCAL_ID_WIDTH                     ( LOCAL_ID_WIDTH                     ),
    .LOCAL_CS_WIDTH                     ( LOCAL_CS_WIDTH                     ),
    .MEM_IF_ADDR_WIDTH                  ( MEM_IF_ADDR_WIDTH                  ),
    .MEM_IF_CLK_PAIR_COUNT              ( MEM_IF_CLK_PAIR_COUNT              ),
    .LOCAL_IF_TYPE                      ( LOCAL_IF_TYPE                      ),
    .DWIDTH_RATIO                       ( DWIDTH_RATIO                       ),
    .CTL_ODT_ENABLED                    ( CTL_ODT_ENABLED                    ),
    .CTL_OUTPUT_REGD                    ( CTL_OUTPUT_REGD                    ),
    .CTL_TBP_NUM                        ( CTL_TBP_NUM                        ),
    .WRBUFFER_ADDR_WIDTH                ( WRBUFFER_ADDR_WIDTH                ),
    .RDBUFFER_ADDR_WIDTH                ( RDBUFFER_ADDR_WIDTH                ),
    .MEM_IF_CS_WIDTH                    ( MEM_IF_CHIP_BITS                   ),
    .MEM_IF_CHIP                        ( MEM_IF_CS_WIDTH                    ),
    .MEM_IF_BANKADDR_WIDTH              ( MEM_IF_BANKADDR_WIDTH              ),
    .MEM_IF_ROW_WIDTH                   ( MEM_IF_ROW_ADDR_WIDTH              ),
    .MEM_IF_COL_WIDTH                   ( MEM_IF_COL_ADDR_WIDTH              ),
    .MEM_IF_ODT_WIDTH                   ( MEM_IF_ODT_WIDTH                   ),
    .MEM_IF_DQS_WIDTH                   ( MEM_IF_DQS_WIDTH                   ),
    .MEM_IF_DWIDTH                      ( MEM_IF_DQ_WIDTH                    ),
    .MEM_IF_DM_WIDTH                    ( MEM_IF_DM_WIDTH                    ),
    .MAX_MEM_IF_CS_WIDTH                ( MAX_MEM_IF_CS_WIDTH                ),
    .MAX_MEM_IF_CHIP                    ( MAX_MEM_IF_CHIP                    ),
    .MAX_MEM_IF_BANKADDR_WIDTH          ( MAX_MEM_IF_BANKADDR_WIDTH          ),
    .MAX_MEM_IF_ROWADDR_WIDTH           ( MAX_MEM_IF_ROWADDR_WIDTH           ),
    .MAX_MEM_IF_COLADDR_WIDTH           ( MAX_MEM_IF_COLADDR_WIDTH           ),
    .MAX_MEM_IF_ODT_WIDTH               ( MAX_MEM_IF_ODT_WIDTH               ),
    .MAX_MEM_IF_DQS_WIDTH               ( MAX_MEM_IF_DQS_WIDTH               ),
    .MAX_MEM_IF_DQ_WIDTH                ( MAX_MEM_IF_DQ_WIDTH                ),
    .MAX_MEM_IF_MASK_WIDTH              ( MAX_MEM_IF_MASK_WIDTH              ),
    .MAX_LOCAL_DATA_WIDTH               ( MAX_LOCAL_DATA_WIDTH               ),
    .CFG_TYPE                           ( CFG_TYPE                           ),
    .CFG_INTERFACE_WIDTH                ( CFG_INTERFACE_WIDTH                ),
    .CFG_BURST_LENGTH                   ( CFG_BURST_LENGTH                   ),
    .CFG_DEVICE_WIDTH                   ( MEM_IF_DQS_WIDTH                   ),
    .CFG_REORDER_DATA                   ( CFG_REORDER_DATA                   ),
    .CFG_DATA_REORDERING_TYPE           ( CFG_DATA_REORDERING_TYPE           ),
    .CFG_STARVE_LIMIT                   ( CFG_STARVE_LIMIT                   ),
    .CFG_ADDR_ORDER                     ( CFG_ADDR_ORDER                     ),
    .MEM_CAS_WR_LAT                     ( MEM_WTCL_INT                       ),
    .MEM_ADD_LAT                        ( MEM_ADD_LAT                        ),
    .MEM_TCL                            ( MEM_TCL                            ),
    .MEM_TRRD                           ( MEM_TRRD                           ),
    .MEM_TFAW                           ( MEM_TFAW                           ),
    .MEM_TRFC                           ( MEM_TRFC                           ),
    .MEM_TREFI                          ( MEM_TREFI                          ),
    .MEM_TRCD                           ( MEM_TRCD                           ),
    .MEM_TRP                            ( MEM_TRP                            ),
    .MEM_TWR                            ( MEM_TWR                            ),
    .MEM_TWTR                           ( MEM_TWTR                           ),
    .MEM_TRTP                           ( MEM_TRTP                           ),
    .MEM_TRAS                           ( MEM_TRAS                           ),
    .MEM_TRC                            ( MEM_TRC                            ),
    .CFG_TCCD                           ( CFG_TCCD                           ),
    .MEM_AUTO_PD_CYCLES                 ( MEM_AUTO_PD_CYCLES                 ),
    .MEM_IF_RD_TO_WR_TURNAROUND_OCT     ( MEM_IF_RD_TO_WR_TURNAROUND_OCT     ),
    .MEM_IF_WR_TO_RD_TURNAROUND_OCT     ( MEM_IF_WR_TO_RD_TURNAROUND_OCT     ),
    .CTL_RD_TO_PCH_EXTRA_CLK            ( CTL_RD_TO_PCH_EXTRA_CLK            ),
    .CTL_RD_TO_RD_DIFF_CHIP_EXTRA_CLK   ( CTL_RD_TO_RD_DIFF_CHIP_EXTRA_CLK   ),
    .CTL_WR_TO_WR_DIFF_CHIP_EXTRA_CLK   ( CTL_WR_TO_WR_DIFF_CHIP_EXTRA_CLK   ),
    .CFG_SELF_RFSH_EXIT_CYCLES          ( CFG_SELF_RFSH_EXIT_CYCLES          ),
    .CFG_PDN_EXIT_CYCLES                ( CFG_PDN_EXIT_CYCLES                ),
    .CFG_POWER_SAVING_EXIT_CYCLES       ( CFG_POWER_SAVING_EXIT_CYCLES       ),
    .CFG_MEM_CLK_ENTRY_CYCLES           ( CFG_MEM_CLK_ENTRY_CYCLES           ),
    .MEM_TMRD_CK                        ( MEM_TMRD_CK                        ),
    .CTL_ECC_ENABLED                    ( CTL_ECC_ENABLED                    ),
    .CTL_ECC_RMW_ENABLED                ( CTL_ECC_AUTO_CORRECTION_ENABLED    ),
    .CTL_ECC_MULTIPLES_16_24_40_72      ( CTL_ECC_MULTIPLES_16_24_40_72      ),
    .CTL_ENABLE_BURST_INTERRUPT         ( CTL_ENABLE_BURST_INTERRUPT_INT     ),
    .CTL_ENABLE_BURST_TERMINATE         ( CTL_ENABLE_BURST_TERMINATE_INT     ),
    .CTL_ENABLE_WDATA_PATH_LATENCY      ( CTL_ENABLE_WDATA_PATH_LATENCY      ),
    .ENABLE_BURST_MERGE                 ( ENABLE_BURST_MERGE      ),    
    .CFG_GEN_SBE                        ( CFG_GEN_SBE                        ),
    .CFG_GEN_DBE                        ( CFG_GEN_DBE                        ),
    .CFG_ENABLE_INTR                    ( CFG_ENABLE_INTR                    ),
    .CFG_MASK_SBE_INTR                  ( CFG_MASK_SBE_INTR                  ),
    .CFG_MASK_DBE_INTR                  ( CFG_MASK_DBE_INTR                  ),
    .CFG_MASK_CORRDROP_INTR             ( CFG_MASK_CORRDROP_INTR             ),
    .CFG_CLR_INTR                       ( CFG_CLR_INTR                       ),
    .CTL_USR_REFRESH                    ( CTL_USR_REFRESH                    ),
    .CTL_REGDIMM_ENABLED                ( CTL_REGDIMM_ENABLED                ),
    .CFG_WRITE_ODT_CHIP                 ( CFG_WRITE_ODT_CHIP                 ),
    .CFG_READ_ODT_CHIP                  ( CFG_READ_ODT_CHIP                  ),
    .CFG_PORT_WIDTH_WRITE_ODT_CHIP      ( CFG_PORT_WIDTH_WRITE_ODT_CHIP      ),
    .CFG_PORT_WIDTH_READ_ODT_CHIP       ( CFG_PORT_WIDTH_READ_ODT_CHIP       ),
    .MEM_IF_CKE_WIDTH                   ( MEM_IF_CLK_EN_WIDTH                ),
    .CFG_ENABLE_NO_DM                   ( CFG_ENABLE_NO_DM                   ),
    .CTL_CSR_ENABLED                    ( MAX10_CFG ? 0 : CTL_CSR_ENABLED),
    .CSR_ADDR_WIDTH                     ( CSR_ADDR_WIDTH                     ),
    .CSR_DATA_WIDTH                     ( CSR_DATA_WIDTH                     ),
    .CSR_BE_WIDTH                       ( CSR_BE_WIDTH                       ),
    .CFG_WLAT_BUS_WIDTH                 ( AFI_WLAT_WIDTH                     ),
    .CFG_RLAT_BUS_WIDTH                 ( AFI_RLAT_WIDTH                     ),
    .CFG_RRANK_BUS_WIDTH                ( AFI_RRANK_WIDTH                    ),
    .CFG_WRANK_BUS_WIDTH                ( AFI_WRANK_WIDTH                    ),
    .CFG_USE_SHADOW_REGS                ( USE_SHADOW_REGS                    ),
    .CFG_ECC_DECODER_REG		( CFG_ECC_DECODER_REG		     ),
    .CFG_ERRCMD_FIFO_REG		( CFG_ERRCMD_FIFO_REG		     )	

) alt_mem_ddrx_controller_top_inst (
    .clk                                ( afi_clk                            ),
    .half_clk                           ( afi_half_clk                       ),
    .reset_n                            ( afi_reset_n                        ),
    .itf_cmd_ready                      ( itf_cmd_ready                      ),
    .itf_cmd_valid                      ( itf_cmd_valid                      ),
    .itf_cmd                            ( itf_cmd                            ),
    .itf_cmd_address                    ( itf_cmd_address                    ),
    .itf_cmd_burstlen                   ( itf_cmd_burstlen                   ),
    .itf_cmd_id                         ( itf_cmd_id                         ),
    .itf_cmd_priority                   ( itf_cmd_priority                   ),
    .itf_cmd_autopercharge              ( itf_cmd_autopercharge              ), 
    .itf_cmd_multicast                  ( itf_cmd_multicast                  ),
    .itf_wr_data_ready                  ( itf_wr_data_ready                  ),
    .itf_wr_data_valid                  ( itf_wr_data_valid                  ),
    .itf_wr_data                        ( itf_wr_data                        ),
    .itf_wr_data_byte_en                ( itf_wr_data_byte_en                ),
    .itf_wr_data_begin                  ( itf_wr_data_begin                  ),
    .itf_wr_data_last                   ( itf_wr_data_last                   ),
    .itf_wr_data_id                     ( itf_wr_data_id                     ),
    .itf_rd_data_ready                  ( itf_rd_data_ready                  ),
    .itf_rd_data_valid                  ( itf_rd_data_valid                  ),
    .itf_rd_data                        ( itf_rd_data                        ),
    .itf_rd_data_error                  ( itf_rd_data_error                  ),
    .itf_rd_data_begin                  ( itf_rd_data_begin                  ),
    .itf_rd_data_last                   ( itf_rd_data_last                   ),
    .itf_rd_data_id                     ( itf_rd_data_id                     ),
    .afi_rst_n                          (                                    ), 
    .afi_cs_n                           ( afi_cs_n                           ),
    .afi_cke                            ( afi_cke                            ),
    .afi_odt                            ( afi_odt                            ),
    .afi_addr                           ( afi_addr                           ),
    .afi_ba                             ( afi_ba                             ),
    .afi_ras_n                          ( afi_ras_n                          ),
    .afi_cas_n                          ( afi_cas_n                          ),
    .afi_we_n                           ( afi_we_n                           ),
    .afi_dqs_burst                      ( afi_dqs_burst                      ),
    .afi_wdata_valid                    ( afi_wdata_valid                    ),
    .afi_wdata                          ( afi_wdata                          ),
    .afi_dm                             ( afi_dm                             ),
    .afi_wlat                           ( afi_wlat                           ),
    .afi_rdata_en                       ( int_afi_rdata_en                   ),
    .afi_rdata_en_full                  ( int_afi_rdata_en_full              ),
    .afi_rrank                          (                                    ),
    .afi_wrank                          (                                    ),
    .afi_rdata                          ( afi_rdata                          ),
    .afi_rdata_valid                    ( afi_rdata_valid                    ),
    .afi_rlat                           ( afi_rlat                           ),
    .afi_cal_success                    ( afi_cal_success                    ),
    .afi_cal_fail                       ( afi_cal_fail                       ),
    .afi_cal_req                        ( afi_cal_req                        ),
    .afi_init_req                       ( afi_init_req                       ),
    .afi_mem_clk_disable                ( afi_mem_clk_disable                ),
    .afi_cal_byte_lane_sel_n            ( afi_cal_byte_lane_sel_n            ),
    .afi_ctl_refresh_done               ( afi_ctl_refresh_done               ),
    .afi_seq_busy                       ( afi_seq_busy                       ),
    .afi_ctl_long_idle                  ( afi_ctl_long_idle                  ),
    .local_init_done                    ( local_init_done                    ),
    .local_refresh_ack                  ( local_refresh_ack                  ),
    .local_powerdn_ack                  ( local_powerdn_ack                  ),
    .local_self_rfsh_ack                ( local_self_rfsh_ack                ),
    .local_deep_powerdn_ack             ( local_deep_powerdn_ack             ),
    .local_refresh_req                  ( local_refresh_req                  ),
    .local_refresh_chip                 ( local_refresh_chip                 ),
    .local_powerdn_req                  (                                    ),
    .local_self_rfsh_req                ( local_self_rfsh_req                ),
    .local_self_rfsh_chip               ( local_self_rfsh_chip               ),
    .local_deep_powerdn_req             ( local_deep_powerdn_req             ),
    .local_deep_powerdn_chip            ( local_deep_powerdn_chip            ),
    .local_zqcal_req                    ( 1'b0                               ),
    .local_zqcal_chip                   ( 1'b0                               ),
    .local_multicast                    ( local_multicast                    ),
    .local_priority                     ( local_priority                     ),
    .ecc_interrupt                      ( ecc_interrupt                      ),
    .csr_read_req                       ( csr_read_req                       ),
    .csr_write_req                      ( csr_write_req                      ),
    .csr_burst_count                    ( 1'b1                               ),
    .csr_beginbursttransfer             (                                    ),
    .csr_addr                           ( csr_addr                           ),
    .csr_wdata                          ( csr_wdata                          ),
    .csr_rdata                          ( csr_rdata                          ),
    .csr_be                             ( csr_be                             ),
    .csr_rdata_valid                    ( csr_rdata_valid                    ),
    .csr_waitrequest                    ( csr_waitrequest                    )
);

assign afi_rdata_en = int_afi_rdata_en[AFI_RATE_RATIO-1:0];
assign afi_rdata_en_full = int_afi_rdata_en_full[AFI_RATE_RATIO-1:0];
assign local_cal_success = afi_cal_success;
assign local_cal_fail = afi_cal_fail;


endmodule
