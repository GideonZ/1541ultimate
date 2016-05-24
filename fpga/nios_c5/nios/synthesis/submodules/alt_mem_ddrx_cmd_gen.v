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



//altera message_off 10230 10036

`include "alt_mem_ddrx_define.iv"

`timescale 1 ps / 1 ps
module alt_mem_ddrx_cmd_gen
    # (parameter
        // cmd_gen settings
        CFG_LOCAL_ADDR_WIDTH            = 33,
        CFG_LOCAL_SIZE_WIDTH            = 3,
        CFG_LOCAL_ID_WIDTH              = 8,
        CFG_INT_SIZE_WIDTH              = 4,
        CFG_PORT_WIDTH_COL_ADDR_WIDTH   = 4,
        CFG_PORT_WIDTH_ROW_ADDR_WIDTH   = 5,
        CFG_PORT_WIDTH_BANK_ADDR_WIDTH  = 2,
        CFG_PORT_WIDTH_CS_ADDR_WIDTH    = 2,
        CFG_PORT_WIDTH_BURST_LENGTH     = 5,
        CFG_PORT_WIDTH_ADDR_ORDER       = 2,
        CFG_DWIDTH_RATIO                = 2, // 2-FR,4-HR,8-QR
        CFG_CTL_QUEUE_DEPTH             = 8,
        CFG_MEM_IF_CHIP                 = 1, // one hot
        CFG_MEM_IF_CS_WIDTH             = 1, // binary coded
        CFG_MEM_IF_BA_WIDTH             = 3,
        CFG_MEM_IF_ROW_WIDTH            = 13,
        CFG_MEM_IF_COL_WIDTH            = 10,
        CFG_DATA_ID_WIDTH               = 10,
        CFG_ENABLE_QUEUE                = 1,
        CFG_ENABLE_BURST_MERGE          = 1,
        CFG_CMD_GEN_OUTPUT_REG          = 0,
        CFG_CTL_TBP_NUM                 = 4,
        CFG_CTL_SHADOW_TBP_NUM          = 4,
        MIN_COL                         = 8,
        MIN_ROW                         = 12,
        MIN_BANK                        = 2,
        MIN_CS                          = 1
    )
    (
        ctl_clk,
        ctl_reset_n,
        
        // tbp interface
        tbp_full,
        tbp_load,
        tbp_read,
        tbp_write,
        tbp_chipsel,
        tbp_bank,
        tbp_row,
        tbp_col,
        tbp_shadow_chipsel,
        tbp_shadow_bank,
        tbp_shadow_row,
        cmd_gen_load,
        cmd_gen_waiting_to_load,
        cmd_gen_chipsel,
        cmd_gen_bank,
        cmd_gen_row,
        cmd_gen_col,
        cmd_gen_write,
        cmd_gen_read,
        cmd_gen_multicast,
        cmd_gen_size,
        cmd_gen_localid,
        cmd_gen_dataid,
        cmd_gen_priority,
        cmd_gen_rmw_correct,
        cmd_gen_rmw_partial,
        cmd_gen_autopch,
        cmd_gen_complete,
        cmd_gen_same_chipsel_addr,
        cmd_gen_same_bank_addr,
        cmd_gen_same_row_addr,
        cmd_gen_same_col_addr,
        cmd_gen_same_read_cmd,
        cmd_gen_same_write_cmd,
        cmd_gen_same_shadow_chipsel_addr,
        cmd_gen_same_shadow_bank_addr,
        cmd_gen_same_shadow_row_addr,
	cmd_gen_busy,
        
        // input interface
        cmd_gen_full,
        cmd_valid,
        cmd_address,
        cmd_write,
        cmd_read,
        cmd_id,
        cmd_multicast,
        cmd_size,
        cmd_priority,
        cmd_autoprecharge,
        
        // datapath interface
        proc_busy,
        proc_load,
        proc_load_dataid,
        proc_write,
        proc_read,
        proc_size,
        proc_localid,
        wdatap_free_id_valid, // from wdata path
        wdatap_free_id_dataid, // from wdata path
        rdatap_free_id_valid, // from rdata path
        rdatap_free_id_dataid, // from rdata path
        tbp_load_index,
        data_complete,
        data_rmw_complete,
        
        // nodm and ecc signal
        errcmd_ready,
        errcmd_valid,
        errcmd_chipsel,
        errcmd_bank,
        errcmd_row,
        errcmd_column,
        errcmd_size,
        errcmd_localid,
        data_partial_be,
        
        // configuration ports
        cfg_enable_cmd_split,
        cfg_burst_length,
        cfg_addr_order,
        cfg_enable_ecc,
        cfg_enable_no_dm,
        cfg_col_addr_width,
        cfg_row_addr_width,
        cfg_bank_addr_width,
        cfg_cs_addr_width
    );
    
    localparam MAX_COL                        = CFG_MEM_IF_COL_WIDTH;
    localparam MAX_ROW                        = CFG_MEM_IF_ROW_WIDTH;
    localparam MAX_BANK                       = CFG_MEM_IF_BA_WIDTH;
    localparam MAX_CS                         = CFG_MEM_IF_CS_WIDTH;
    localparam BUFFER_WIDTH                   = 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + CFG_DATA_ID_WIDTH + CFG_LOCAL_ID_WIDTH + CFG_INT_SIZE_WIDTH + CFG_MEM_IF_CS_WIDTH + CFG_MEM_IF_BA_WIDTH + CFG_MEM_IF_ROW_WIDTH + CFG_MEM_IF_COL_WIDTH;
    localparam CFG_LOCAL_ADDR_BITSELECT_WIDTH = log2(CFG_LOCAL_ADDR_WIDTH);
    localparam INT_LOCAL_ADDR_WIDTH           = 2**CFG_LOCAL_ADDR_BITSELECT_WIDTH;
    
    localparam CFG_CMD_GEN_SPLIT_REGISTERED   = 1;
    localparam CFG_LOCAL_BURST_MERGE_IDCMP    = 0;   //Enable/Disable Local ID compare for burst merge

    //ECC State Machine
    localparam IDLE         = 4'h0;
    localparam CORRECT_RD   = 4'h1;
    localparam PARWR_WR     = 4'h2;
    localparam FULL_WR      = 4'h3;
    localparam PARWR_RD     = 4'h4;
    localparam FULL_RD      = 4'h6;
    localparam CORRECT_WR   = 4'h8;

    input   ctl_clk;
    input   ctl_reset_n;
    
    input                                                       tbp_full;
    input   [CFG_CTL_TBP_NUM-1:0]                               tbp_load;
    input   [CFG_CTL_TBP_NUM-1:0]                               tbp_read;
    input   [CFG_CTL_TBP_NUM-1:0]                               tbp_write;
    input   [(CFG_CTL_TBP_NUM*CFG_MEM_IF_CS_WIDTH)-1:0]         tbp_chipsel;
    input   [(CFG_CTL_TBP_NUM*CFG_MEM_IF_BA_WIDTH)-1:0]         tbp_bank;
    input   [(CFG_CTL_TBP_NUM*CFG_MEM_IF_ROW_WIDTH)-1:0]        tbp_row;
    input   [(CFG_CTL_TBP_NUM*CFG_MEM_IF_COL_WIDTH)-1:0]        tbp_col;
    input   [(CFG_CTL_SHADOW_TBP_NUM*CFG_MEM_IF_CS_WIDTH)-1:0]  tbp_shadow_chipsel;
    input   [(CFG_CTL_SHADOW_TBP_NUM*CFG_MEM_IF_BA_WIDTH)-1:0]  tbp_shadow_bank;
    input   [(CFG_CTL_SHADOW_TBP_NUM*CFG_MEM_IF_ROW_WIDTH)-1:0] tbp_shadow_row;
    output                                                      cmd_gen_load;
    output                                                      cmd_gen_waiting_to_load;
    output  [CFG_MEM_IF_CS_WIDTH-1:0]                           cmd_gen_chipsel;
    output  [CFG_MEM_IF_BA_WIDTH-1:0]                           cmd_gen_bank;
    output  [CFG_MEM_IF_ROW_WIDTH-1:0]                          cmd_gen_row;
    output  [CFG_MEM_IF_COL_WIDTH-1:0]                          cmd_gen_col;
    output                                                      cmd_gen_write;
    output                                                      cmd_gen_read;
    output                                                      cmd_gen_multicast;
    output  [CFG_INT_SIZE_WIDTH-1:0]                            cmd_gen_size;
    output  [CFG_LOCAL_ID_WIDTH-1:0]                            cmd_gen_localid;
    output  [CFG_DATA_ID_WIDTH-1:0]                             cmd_gen_dataid;
    output                                                      cmd_gen_priority;
    output                                                      cmd_gen_rmw_correct;
    output                                                      cmd_gen_rmw_partial;
    output                                                      cmd_gen_autopch;
    output                                                      cmd_gen_complete;
    output  [CFG_CTL_TBP_NUM-1:0]                               cmd_gen_same_chipsel_addr;
    output  [CFG_CTL_TBP_NUM-1:0]                               cmd_gen_same_bank_addr;
    output  [CFG_CTL_TBP_NUM-1:0]                               cmd_gen_same_row_addr;
    output  [CFG_CTL_TBP_NUM-1:0]                               cmd_gen_same_col_addr;
    output  [CFG_CTL_TBP_NUM-1:0]                               cmd_gen_same_read_cmd;
    output  [CFG_CTL_TBP_NUM-1:0]                               cmd_gen_same_write_cmd;
    output  [CFG_CTL_SHADOW_TBP_NUM-1:0]                        cmd_gen_same_shadow_chipsel_addr;
    output  [CFG_CTL_SHADOW_TBP_NUM-1:0]                        cmd_gen_same_shadow_bank_addr;
    output  [CFG_CTL_SHADOW_TBP_NUM-1:0]                        cmd_gen_same_shadow_row_addr;
    output							cmd_gen_busy;
    
    output                                       cmd_gen_full;
    input                                        cmd_valid;
    input   [CFG_LOCAL_ADDR_WIDTH-1:0]           cmd_address;
    input                                        cmd_write;
    input                                        cmd_read;
    input   [CFG_LOCAL_ID_WIDTH-1:0]             cmd_id;
    input                                        cmd_multicast;
    input   [CFG_LOCAL_SIZE_WIDTH-1:0]           cmd_size;
    input                                        cmd_priority;
    input                                        cmd_autoprecharge;
    
    output                                       proc_busy;
    output                                       proc_load;
    output                                       proc_load_dataid;
    output                                       proc_write;
    output                                       proc_read;
    output  [CFG_INT_SIZE_WIDTH-1:0]             proc_size;
    output  [CFG_LOCAL_ID_WIDTH-1:0]             proc_localid;
    input                                        wdatap_free_id_valid;
    input   [CFG_DATA_ID_WIDTH-1:0]              wdatap_free_id_dataid;
    input                                        rdatap_free_id_valid;
    input   [CFG_DATA_ID_WIDTH-1:0]              rdatap_free_id_dataid;
    output  [CFG_CTL_TBP_NUM-1:0]                tbp_load_index;
    input   [CFG_CTL_TBP_NUM-1:0]                data_complete;
    input                                        data_rmw_complete;
    
    output                                       errcmd_ready; // high means cmd_gen accepts command
    input                                        errcmd_valid;
    input   [CFG_MEM_IF_CS_WIDTH-1:0]            errcmd_chipsel;
    input   [CFG_MEM_IF_BA_WIDTH-1:0]            errcmd_bank;
    input   [CFG_MEM_IF_ROW_WIDTH-1:0]           errcmd_row;
    input   [CFG_MEM_IF_COL_WIDTH-1:0]           errcmd_column;
    input   [CFG_INT_SIZE_WIDTH-1:0]             errcmd_size;
    input   [CFG_LOCAL_ID_WIDTH   - 1 : 0]       errcmd_localid;
    input                                        data_partial_be;
    
    input                                        cfg_enable_cmd_split;
    input   [CFG_PORT_WIDTH_BURST_LENGTH-1:0]    cfg_burst_length;  // this contains immediate BL value, max is 31
    input   [CFG_PORT_WIDTH_ADDR_ORDER-1:0]      cfg_addr_order;    // 0 is chiprowbankcol , 1 is chipbankrowcol , 2 is rowchipbankcol
    input                                        cfg_enable_ecc;
    input                                        cfg_enable_no_dm;
    input   [CFG_PORT_WIDTH_COL_ADDR_WIDTH-1:0]  cfg_col_addr_width;
    input   [CFG_PORT_WIDTH_ROW_ADDR_WIDTH-1:0]  cfg_row_addr_width;
    input   [CFG_PORT_WIDTH_BANK_ADDR_WIDTH-1:0] cfg_bank_addr_width;
    input   [CFG_PORT_WIDTH_CS_ADDR_WIDTH-1:0]   cfg_cs_addr_width;
    
    // === address mapping
    
    integer n;
    integer j;
    integer k;
    integer m;
    
    wire                                   cfg_enable_rmw;
    wire			           take_from_ecc_correct;
    wire				   ecc_queue_load;
    wire				   split_queue_load;
    wire				   split_queue_load_for_avl;
    wire				   split_queue_load_init;	
    wire				   split_queue_load_ecc_load;
    reg					   split_queue_load_open;
    wire				   split_queue_load_open_final;
    wire				   split_queue_fast_unload;
    reg     [3:0]			   ecc_state_sm;
    
    wire    [INT_LOCAL_ADDR_WIDTH-1:0]     int_cmd_address;
    
    reg     [CFG_MEM_IF_CS_WIDTH-1:0]      int_cs_addr;
    reg     [CFG_MEM_IF_BA_WIDTH-1:0]      int_bank_addr;
    reg     [CFG_MEM_IF_ROW_WIDTH-1:0]     int_row_addr;
    reg     [CFG_MEM_IF_COL_WIDTH-1:0]     int_col_addr;
    
    // === command splitting block
    
    reg     [CFG_MEM_IF_CS_WIDTH-1:0]      int_split_cs_addr;
    reg     [CFG_MEM_IF_BA_WIDTH-1:0]      int_split_bank_addr;
    reg     [CFG_MEM_IF_ROW_WIDTH-1:0]     int_split_row_addr;
    reg     [CFG_MEM_IF_COL_WIDTH-1:0]     int_split_col_addr;
    reg                                    int_split_read;
    wire				   int_split_read_final;
    reg                                    int_split_write;
    wire				   int_split_write_final; 
    reg     [CFG_INT_SIZE_WIDTH-1:0]       int_split_size;
    reg                                    int_split_autopch;
    reg                                    int_split_multicast;
    reg                                    int_split_priority;
    reg     [CFG_LOCAL_ID_WIDTH-1:0]       int_split_localid;
    
    reg     [CFG_MEM_IF_CS_WIDTH-1:0]      split_cs_addr;
    reg     [CFG_MEM_IF_BA_WIDTH-1:0]      split_bank_addr;
    reg     [CFG_MEM_IF_ROW_WIDTH-1:0]     split_row_addr;
    reg     [CFG_MEM_IF_COL_WIDTH-1:0]     split_col_addr;
    reg                                    split_read;
    reg                                    split_write;
    reg     [CFG_INT_SIZE_WIDTH-1:0]       split_size;
    reg                                    split_autopch;
    reg                                    split_multicast;
    reg                                    split_priority;
    reg     [CFG_LOCAL_ID_WIDTH-1:0]       split_localid;
    
    reg     [CFG_MEM_IF_CS_WIDTH-1:0]      buf_cs_addr;
    reg     [CFG_MEM_IF_BA_WIDTH-1:0]      buf_bank_addr;
    reg     [CFG_MEM_IF_ROW_WIDTH-1:0]     buf_row_addr;
    reg     [CFG_MEM_IF_COL_WIDTH-1:0]     buf_col_addr;
    reg                                    buf_read_req;
    reg                                    buf_write_req;
    reg                                    buf_autopch_req;
    reg                                    buf_multicast;
    reg                                    buf_priority;
    reg     [CFG_LOCAL_ID_WIDTH-1:0]       buf_localid;
    reg     [CFG_LOCAL_SIZE_WIDTH:0]       buf_size;
    
    reg                                    buf_chip_addr_reach_max;
    reg                                    buf_bank_addr_reach_max;
    reg                                    buf_row_addr_reach_max;
    reg                                    buf_col_addr_reach_max;
    
    reg     [3 : 0]                        int_buf_row_addr_reach_max;
    
    reg     [CFG_INT_SIZE_WIDTH-1:0]       decrmntd_size;
    reg     [CFG_MEM_IF_CS_WIDTH-1:0]      incrmntd_cs_addr;
    reg     [CFG_MEM_IF_BA_WIDTH-1:0]      incrmntd_bank_addr;
    reg     [CFG_MEM_IF_ROW_WIDTH-1:0]     incrmntd_row_addr;
    reg     [CFG_MEM_IF_COL_WIDTH-1:0]     incrmntd_col_addr;
    
    reg     [CFG_MEM_IF_CS_WIDTH-1:0]      max_chip_from_csr;
    reg     [CFG_MEM_IF_BA_WIDTH-1:0]      max_bank_from_csr;
    reg     [CFG_MEM_IF_ROW_WIDTH-1:0]     max_row_from_csr;
    reg     [CFG_MEM_IF_COL_WIDTH-1:0]     max_col_from_csr;
    
    wire                                   copy;
    reg     [2:0]                          unaligned_burst;  // because planned max native size is 8, unaligned burst can be a max of 7
    reg     [3:0]                          native_size;      // support native size up to 15, bl16 FR have native size of 8
    reg     [4:0]                          native_size_x2;
    wire                                   require_gen;
    reg                                    deassert_ready;
    reg                                    registered;
    reg                                    generating;
    
    // === ecc mux
    
    reg     [CFG_MEM_IF_CS_WIDTH-1:0]      ecc_cs_addr;
    reg     [CFG_MEM_IF_BA_WIDTH-1:0]      ecc_bank_addr;
    reg     [CFG_MEM_IF_ROW_WIDTH-1:0]     ecc_row_addr;
    reg     [CFG_MEM_IF_COL_WIDTH-1:0]     ecc_col_addr;
    reg                                    ecc_read;
    reg                                    ecc_write;
    reg     [CFG_INT_SIZE_WIDTH-1:0]       ecc_size;
    reg                                    ecc_autopch;
    reg                                    ecc_multicast;
    reg                                    ecc_priority;
    reg     [CFG_LOCAL_ID_WIDTH-1:0]       ecc_localid;
    reg     [CFG_DATA_ID_WIDTH-1:0]        ecc_dataid;
    reg                                    ecc_correct;
    reg                                    ecc_partial;
    
    reg                                    ecc_rmw_read;
    reg                                    ecc_rmw_write;
    reg                                    errcmd_ready;
    reg                                    correct;
    reg                                    partial_opr;

    wire                                   mux_busy;
    wire                                   mux_busy_non_ecc; 
    wire			           mux_busy_ecc;
    
    wire    [CFG_MEM_IF_CS_WIDTH-1:0]      muxed_cs_addr;
    wire    [CFG_MEM_IF_BA_WIDTH-1:0]      muxed_bank_addr;
    wire    [CFG_MEM_IF_ROW_WIDTH-1:0]     muxed_row_addr;
    wire    [CFG_MEM_IF_COL_WIDTH-1:0]     muxed_col_addr;
    wire                                   muxed_read;
    wire                                   muxed_write;
    wire    [CFG_INT_SIZE_WIDTH-1:0]       muxed_size;
    wire                                   muxed_autopch;
    wire                                   muxed_multicast;
    wire                                   muxed_priority;
    wire    [CFG_LOCAL_ID_WIDTH-1:0]       muxed_localid;
    wire    [CFG_DATA_ID_WIDTH-1:0]        muxed_dataid;
    wire                                   muxed_complete;
    wire                                   muxed_correct;
    wire                                   muxed_partial;
    
    wire                                   proc_busy;
    wire                                   proc_load;
    wire                                   proc_load_dataid;
    wire                                   proc_write;
    wire                                   proc_read;
    wire    [CFG_INT_SIZE_WIDTH-1:0]       proc_size;
    wire    [CFG_LOCAL_ID_WIDTH-1:0]       proc_localid;
    
    reg                                    ecc_proc_busy;
    reg					   ecc_proc_busy_parwr_read_queue_load;
    reg                                    ecc_proc_load;
    reg                                    ecc_proc_load_dataid;
    reg                                    ecc_proc_write;
    reg                                    ecc_proc_read;
    reg     [CFG_INT_SIZE_WIDTH-1:0]       ecc_proc_size;
    reg     [CFG_LOCAL_ID_WIDTH-1:0]       ecc_proc_localid;
    
    reg                                    waiting_to_load;
    
    wire    [CFG_CTL_TBP_NUM-1:0]          tbp_load_index;
    
    reg [CFG_LOCAL_ADDR_BITSELECT_WIDTH-1:0] cfg_addr_bitsel_chipsel;
    reg [CFG_LOCAL_ADDR_BITSELECT_WIDTH-1:0] cfg_addr_bitsel_bank;
    reg [CFG_LOCAL_ADDR_BITSELECT_WIDTH-1:0] cfg_addr_bitsel_row;
    
    // === queue
    
    reg     [BUFFER_WIDTH-1:0]               pipe[CFG_CTL_QUEUE_DEPTH-1:0];
    reg                                      pipefull[CFG_CTL_QUEUE_DEPTH-1:0];
    
    wire                                     fetch;
    wire    [BUFFER_WIDTH-1:0]               buffer_input;
    wire                                     write_to_queue;
    wire                                     queue_full;
    
    wire                                     cmd_gen_load;
    wire                                     cmd_gen_waiting_to_load;
    wire    [CFG_MEM_IF_CS_WIDTH-1:0]        cmd_gen_chipsel;
    wire    [CFG_MEM_IF_BA_WIDTH-1:0]        cmd_gen_bank;
    wire    [CFG_MEM_IF_ROW_WIDTH-1:0]       cmd_gen_row;
    wire    [CFG_MEM_IF_COL_WIDTH-1:0]       cmd_gen_col;
    wire                                     cmd_gen_write;
    wire                                     cmd_gen_read;
    wire                                     cmd_gen_multicast;
    wire    [CFG_INT_SIZE_WIDTH-1:0]         cmd_gen_size;
    wire    [CFG_LOCAL_ID_WIDTH-1:0]         cmd_gen_localid;
    wire    [CFG_DATA_ID_WIDTH-1:0]          cmd_gen_dataid;
    wire                                     cmd_gen_priority;
    wire                                     cmd_gen_rmw_correct;
    wire                                     cmd_gen_rmw_partial;
    wire                                     cmd_gen_autopch;
    wire                                     cmd_gen_complete;
    wire    [CFG_CTL_TBP_NUM-1:0]            cmd_gen_same_chipsel_addr;
    wire    [CFG_CTL_TBP_NUM-1:0]            cmd_gen_same_bank_addr;
    wire    [CFG_CTL_TBP_NUM-1:0]            cmd_gen_same_row_addr;
    wire    [CFG_CTL_TBP_NUM-1:0]            cmd_gen_same_col_addr;
    wire    [CFG_CTL_TBP_NUM-1:0]            cmd_gen_same_read_cmd;
    wire    [CFG_CTL_TBP_NUM-1:0]            cmd_gen_same_write_cmd;
    wire    [CFG_CTL_SHADOW_TBP_NUM-1:0]     cmd_gen_same_shadow_chipsel_addr;
    wire    [CFG_CTL_SHADOW_TBP_NUM-1:0]     cmd_gen_same_shadow_bank_addr;
    wire    [CFG_CTL_SHADOW_TBP_NUM-1:0]     cmd_gen_same_shadow_row_addr;
    
    reg     [CFG_CTL_TBP_NUM-1:0]            same_chipsel_addr;
    reg     [CFG_CTL_TBP_NUM-1:0]            same_bank_addr;
    reg     [CFG_CTL_TBP_NUM-1:0]            same_row_addr;
    reg     [CFG_CTL_TBP_NUM-1:0]            same_col_addr;
    reg     [CFG_CTL_TBP_NUM-1:0]            same_read_cmd;
    reg     [CFG_CTL_TBP_NUM-1:0]            same_write_cmd;
    reg     [CFG_CTL_SHADOW_TBP_NUM-1:0]     same_shadow_chipsel_addr;
    reg     [CFG_CTL_SHADOW_TBP_NUM-1:0]     same_shadow_bank_addr;
    reg     [CFG_CTL_SHADOW_TBP_NUM-1:0]     same_shadow_row_addr;
    
    reg                                      read           [CFG_CTL_TBP_NUM-1:0];
    reg                                      write          [CFG_CTL_TBP_NUM-1:0];
    reg     [CFG_MEM_IF_CS_WIDTH-1:0]        chipsel        [CFG_CTL_TBP_NUM-1:0];
    reg     [CFG_MEM_IF_BA_WIDTH-1:0]        bank           [CFG_CTL_TBP_NUM-1:0];
    reg     [CFG_MEM_IF_ROW_WIDTH-1:0]       row            [CFG_CTL_TBP_NUM-1:0];
    reg     [CFG_MEM_IF_COL_WIDTH-1:0]       col            [CFG_CTL_TBP_NUM-1:0];
    
    wire    [CFG_MEM_IF_CS_WIDTH-1:0]        shadow_chipsel [CFG_CTL_SHADOW_TBP_NUM-1:0];
    wire    [CFG_MEM_IF_BA_WIDTH-1:0]        shadow_bank    [CFG_CTL_SHADOW_TBP_NUM-1:0];
    wire    [CFG_MEM_IF_ROW_WIDTH-1:0]       shadow_row     [CFG_CTL_SHADOW_TBP_NUM-1:0];
    
    reg     [CFG_MEM_IF_CS_WIDTH-1:0]      int_ecc_cs_addr;
    reg     [CFG_MEM_IF_BA_WIDTH-1:0]      int_ecc_bank_addr;
    reg     [CFG_MEM_IF_ROW_WIDTH-1:0]     int_ecc_row_addr;
    reg     [CFG_MEM_IF_COL_WIDTH-1:0]     int_ecc_col_addr;
    reg                                    int_ecc_read;
    wire                                   int_ecc_read_final;
    reg                                    int_ecc_write;
    wire				   int_ecc_write_final;
    reg     [CFG_INT_SIZE_WIDTH-1:0]       int_ecc_size;
    reg                                    int_ecc_autopch;
    reg                                    int_ecc_multicast;
    reg                                    int_ecc_priority;
    reg     [CFG_LOCAL_ID_WIDTH-1:0]       int_ecc_localid;
    reg     [CFG_DATA_ID_WIDTH-1:0]        int_ecc_dataid;
    reg					   int_ecc_data_complete;
    reg                                    int_ecc_partial_be;
    wire				   int_ecc_data_complete_final;
    wire                                   int_ecc_partial_be_final;

    //Burst merge
    wire                                        cmd_gen_full_postq;
    wire                                        cmd_valid_postq;
    wire                                        cmd_write_postq;
    wire                                        cmd_read_postq;
    wire     [CFG_LOCAL_ID_WIDTH-1:0]           cmd_id_postq;
    wire                                        cmd_multicast_postq;
    wire     [CFG_LOCAL_SIZE_WIDTH-1:0]         cmd_size_postq;
    wire     [CFG_LOCAL_SIZE_WIDTH:0]           cmd_size_plus_unaligned_size;
    wire                                        cmd_priority_postq;
    wire                                        cmd_autoprecharge_postq;
    wire     [CFG_MEM_IF_CS_WIDTH-1:0]          int_cs_addr_postq;
    wire     [CFG_MEM_IF_BA_WIDTH-1:0]          int_bank_addr_postq;
    wire     [CFG_MEM_IF_ROW_WIDTH-1:0]         int_row_addr_postq;
    wire     [CFG_MEM_IF_COL_WIDTH-1:0]         int_col_addr_postq; 

    wire    one;
    
    //======================================================================================
    //
    //  [START] General
    //
    //======================================================================================
        assign cfg_enable_rmw = cfg_enable_ecc | cfg_enable_no_dm;
        
        assign one            = 1'b1;
        
    //======================================================================================
    //
    //  [END] General
    //
    //======================================================================================
    
    //======================================================================================
    //
    //  [START] TBP Info
    //
    //======================================================================================
        generate
            genvar p;
            for (p=0; p<CFG_CTL_TBP_NUM; p=p+1)
                begin : info_per_tbp
                    always @ (*)
                        begin
                            read   [p] = tbp_read   [p];
                            write  [p] = tbp_write  [p];
                            chipsel[p] = tbp_chipsel[(p+1)*CFG_MEM_IF_CS_WIDTH-1:p*CFG_MEM_IF_CS_WIDTH];
                            bank   [p] = tbp_bank   [(p+1)*CFG_MEM_IF_BA_WIDTH-1:p*CFG_MEM_IF_BA_WIDTH];
                            row    [p] = tbp_row    [(p+1)*CFG_MEM_IF_ROW_WIDTH-1:p*CFG_MEM_IF_ROW_WIDTH];
                            col    [p] = tbp_col    [(p+1)*CFG_MEM_IF_COL_WIDTH-1:p*CFG_MEM_IF_COL_WIDTH];
                        end
                end
            
            for (p=0; p<CFG_CTL_SHADOW_TBP_NUM; p=p+1)
                begin : info_per_shadow_tbp
                    assign shadow_chipsel[p] = tbp_shadow_chipsel[(p+1)*CFG_MEM_IF_CS_WIDTH-1:p*CFG_MEM_IF_CS_WIDTH];
                    assign shadow_bank   [p] = tbp_shadow_bank   [(p+1)*CFG_MEM_IF_BA_WIDTH-1:p*CFG_MEM_IF_BA_WIDTH];
                    assign shadow_row    [p] = tbp_shadow_row    [(p+1)*CFG_MEM_IF_ROW_WIDTH-1:p*CFG_MEM_IF_ROW_WIDTH];
                end
        endgenerate
    //======================================================================================
    //
    //  [END] TBP Info
    //
    //======================================================================================
    
    //======================================================================================
    //
    //  [START] Address re-mapping
    //
    //======================================================================================
        // Pre-calculate int_*_addr chipsel, bank, row, col bit select offsets
        always @ (*)
            begin
                // Row width info
                if (cfg_addr_order == `MMR_ADDR_ORDER_ROW_CS_BA_COL)
                    begin
                        cfg_addr_bitsel_row = cfg_cs_addr_width + cfg_bank_addr_width + cfg_col_addr_width - log2(CFG_DWIDTH_RATIO);
                    end
                else if (cfg_addr_order == `MMR_ADDR_ORDER_CS_BA_ROW_COL)
                    begin
                        cfg_addr_bitsel_row = cfg_col_addr_width - log2(CFG_DWIDTH_RATIO);
                    end
                else // cfg_addr_order == `MMR_ADDR_ORDER_CS_ROW_BA_COL
                    begin
                        cfg_addr_bitsel_row = cfg_bank_addr_width + cfg_col_addr_width - log2(CFG_DWIDTH_RATIO);
                    end
                
                // Bank width info
                if (cfg_addr_order == `MMR_ADDR_ORDER_CS_BA_ROW_COL)
                    begin
                        cfg_addr_bitsel_bank = cfg_row_addr_width + cfg_col_addr_width - log2(CFG_DWIDTH_RATIO);
                    end
                else // cfg_addr_order == `MMR_ADDR_ORDER_ROW_CS_BA_COL || `MMR_ADDR_ORDER_CS_ROW_BA_COL
                    begin
                        cfg_addr_bitsel_bank = cfg_col_addr_width - log2(CFG_DWIDTH_RATIO);
                    end
                
                // Chipsel width info
                if (cfg_addr_order == `MMR_ADDR_ORDER_ROW_CS_BA_COL)
                    begin
                        cfg_addr_bitsel_chipsel = cfg_bank_addr_width + cfg_col_addr_width - log2(CFG_DWIDTH_RATIO);
                    end
                else // cfg_addr_order == `MMR_ADDR_ORDER_CS_BA_ROW_COL || `MMR_ADDR_ORDER_CS_ROW_BA_COL
                    begin
                        cfg_addr_bitsel_chipsel = cfg_bank_addr_width + cfg_row_addr_width + cfg_col_addr_width - log2(CFG_DWIDTH_RATIO);
                    end
            end
        
        assign  int_cmd_address =   cmd_address;
        
        // Supported addr order
        // 0 - chip-row-bank-col
        // 1 - chip-bank-row-col
        // 2 - row-chip-bank-col
        
        // Derive column address from address
        always @(*)
            begin : Col_addr_loop
                int_col_addr[MIN_COL - log2(CFG_DWIDTH_RATIO) - 1 : 0] = int_cmd_address[MIN_COL - log2(CFG_DWIDTH_RATIO) - 1 : 0];
                
                for (n = MIN_COL - log2(CFG_DWIDTH_RATIO);n < MAX_COL;n = n + 1'b1)
                    begin
                        if (n < (cfg_col_addr_width - log2(CFG_DWIDTH_RATIO))) // Bit of col_addr can be configured in CSR using cfg_col_addr_width
                            begin
                                int_col_addr[n] = int_cmd_address[n];
                            end
                        else
                            begin
                                int_col_addr[n] = 1'b0;
                            end
                    end
                
                int_col_addr = int_col_addr << log2(CFG_DWIDTH_RATIO);
            end
        
        // Derive row address from address
        reg [CFG_LOCAL_ADDR_BITSELECT_WIDTH-1:0] row_addr_loop_1;
        reg [CFG_LOCAL_ADDR_BITSELECT_WIDTH-1:0] row_addr_loop_2;
        
        always @(*)
            begin : Row_addr_loop
                for (j = 0;j < MIN_ROW;j = j + 1'b1) // The purpose of using this for-loop is to get rid of "if (j < cfg_row_addr_width) begin" which causes multiplexers
                    begin
                        row_addr_loop_1 = j + cfg_addr_bitsel_row;
                        int_row_addr[j] = int_cmd_address[row_addr_loop_1];
                    end
                
                for (j = MIN_ROW;j < MAX_ROW;j = j + 1'b1)
                    begin
                        row_addr_loop_2 = j + cfg_addr_bitsel_row;
                        
                        if(j < cfg_row_addr_width) // Bit of row_addr can be configured in CSR using cfg_row_addr_width
                            begin
                                int_row_addr[j] = int_cmd_address[row_addr_loop_2];
                            end
                        else
                            begin
                                int_row_addr[j] = 1'b0;
                            end
                    end
            end
        
        // Derive bank address from address
        reg [CFG_LOCAL_ADDR_BITSELECT_WIDTH-1:0] bank_addr_loop_1;
        reg [CFG_LOCAL_ADDR_BITSELECT_WIDTH-1:0] bank_addr_loop_2;
        
        always @(*)
            begin : Bank_addr_loop
                for (k = 0;k < MIN_BANK;k = k + 1'b1) // The purpose of using this for-loop is to get rid of "if (k < cfg_bank_addr_width) begin" which causes multiplexers
                    begin
                        bank_addr_loop_1 = k + cfg_addr_bitsel_bank;
                        int_bank_addr[k] = int_cmd_address[bank_addr_loop_1];
                    end
                
                for (k = MIN_BANK;k < MAX_BANK;k = k + 1'b1)
                    begin
                        bank_addr_loop_2 = k + cfg_addr_bitsel_bank;
                        
                        if (k < cfg_bank_addr_width) // Bit of bank_addr can be configured in CSR using cfg_bank_addr_width
                            begin
                                int_bank_addr[k] = int_cmd_address[bank_addr_loop_2];
                            end
                        else
                            begin
                                int_bank_addr[k] = 1'b0;
                            end
                    end
            end
        
        // Derive chipsel address from address
        always @(*)
            begin
                m = 0;
                
                if (cfg_cs_addr_width > 1'b0) // If cfg_cs_addr_width =< 1'b1, address doesn't have cs_addr bit
                    begin
                        for (m=0; m<MIN_CS; m=m+1'b1) // The purpose of using this for-loop is to get rid of "if (m < cfg_cs_addr_width) begin" which causes multiplexers
                            begin
                                int_cs_addr[m] = int_cmd_address[m + cfg_addr_bitsel_chipsel];
                            end
                        for (m=MIN_CS; m<MAX_CS; m=m+1'b1)
                            begin
                                if (m < cfg_cs_addr_width) // Bit of cs_addr can be configured in CSR using cfg_cs_addr_width
                                    begin
                                        int_cs_addr[m] = int_cmd_address[m + cfg_addr_bitsel_chipsel];
                                    end
                                else
                                    begin
                                        int_cs_addr[m] = 1'b0;
                                    end
                            end
                    end
                else  // If CFG_MEM_IF_CS_WIDTH = 1, then set cs_addr to 0 (one chip, one rank)
                    begin
                        int_cs_addr = {CFG_MEM_IF_CS_WIDTH{1'b0}};
                    end
            end
    //======================================================================================
    //
    //  [END] Address re-mapping
    //
    //======================================================================================
    
    //======================================================================================
    //
    //  [START] Burst Merge
    //
    //======================================================================================

generate
	if (CFG_ENABLE_BURST_MERGE == 1)
    		begin
	    	reg                                        cmd_write_str[1:0];
    		wire                                       cmd_read_str[1:0];
    		reg     [CFG_LOCAL_ID_WIDTH-1:0]           cmd_id_str[1:0];
    		reg                                        cmd_multicast_str[1:0];
    		reg     [CFG_LOCAL_SIZE_WIDTH-1:0]         cmd_size_str[1:0];
    		reg                                        cmd_priority_str[1:0];
    		reg                                        cmd_autoprecharge_str[1:0];
    		reg     [CFG_MEM_IF_CS_WIDTH-1:0]          int_cs_addr_str[1:0];
    		reg     [CFG_MEM_IF_BA_WIDTH-1:0]          int_bank_addr_str[1:0];
    		reg     [CFG_MEM_IF_ROW_WIDTH-1:0]         int_row_addr_str[1:0];
    		reg     [CFG_MEM_IF_COL_WIDTH-1:0]         int_col_addr_str[1:0];

    		reg     [1:0]			       bm_write_ptr;
    		reg     [1:0]			       bm_read_ptr;

    		wire		                   bm_cs_can_merge;
    		wire				   bm_bank_can_merge;
    		wire				   bm_row_can_merge;
    		wire				   bm_read_can_merge;
    		wire				   bm_write_can_merge;
    		wire				   bm_localid_can_merge;

    		wire				   bm_valid_can_merge;
    		wire				   bm_seqcol_can_merge;
    		wire    [CFG_MEM_IF_COL_WIDTH:0]   bm_nextcol_can_merge;
    		wire    [CFG_MEM_IF_COL_WIDTH-1:0] bm_base_col_addr;
    		wire    [CFG_LOCAL_SIZE_WIDTH-1:0] bm_base_size; 
    		wire    [CFG_MEM_IF_COL_WIDTH-1:0] bm_nextbase_col_addr;
    		wire    [CFG_LOCAL_SIZE_WIDTH:0]   bm_total_merge_size;
    		wire                               bm_oversize;
		wire				   bm_cross_row;

    		wire				   bm_can_merge;

    		assign cmd_gen_full = (bm_write_ptr[1] != bm_read_ptr[1]) && (bm_write_ptr[0] == bm_read_ptr[0]);
    		assign cmd_valid_postq = (bm_write_ptr != bm_read_ptr); 
	
    		assign cmd_read_str[0] = !cmd_write_str[0];
    		assign cmd_read_str[1] = !cmd_write_str[1];

    		assign int_col_addr_postq 		= bm_read_ptr[0] ? int_col_addr_str[1] 		: int_col_addr_str[0];
    		assign int_row_addr_postq 		= bm_read_ptr[0] ? int_row_addr_str[1] 		: int_row_addr_str[0];
    		assign int_bank_addr_postq 		= bm_read_ptr[0] ? int_bank_addr_str[1] 	: int_bank_addr_str[0];
    		assign int_cs_addr_postq 		= bm_read_ptr[0] ? int_cs_addr_str[1] 		: int_cs_addr_str[0];
    		assign cmd_write_postq 		= bm_read_ptr[0] ? cmd_write_str[1] 		: cmd_write_str[0];
    		assign cmd_read_postq 		= bm_read_ptr[0] ? cmd_read_str[1] 		: cmd_read_str[0];
    		assign cmd_id_postq 		= bm_read_ptr[0] ? cmd_id_str[1] 		: cmd_id_str[0];
    		assign cmd_multicast_postq 		= bm_read_ptr[0] ? cmd_multicast_str[1] 	: cmd_multicast_str[0];
    		assign cmd_size_postq 		= bm_read_ptr[0] ? cmd_size_str[1] 		: cmd_size_str[0];
    		assign cmd_priority_postq 		= bm_read_ptr[0] ? cmd_priority_str[1] 		: cmd_priority_str[0];
    		assign cmd_autoprecharge_postq 	= bm_read_ptr[0] ? cmd_autoprecharge_str[1] 	: cmd_autoprecharge_str[0];

    		assign bm_cs_can_merge 		= (int_cs_addr_str[1] == int_cs_addr_str[0]);
    		assign bm_bank_can_merge 		= (int_bank_addr_str[1] == int_bank_addr_str[0]);
    		assign bm_row_can_merge 		= (int_row_addr_str[1] == int_row_addr_str[0]);
    		assign bm_read_can_merge 		= (cmd_read_str[1] == cmd_read_str[0]);
    		assign bm_write_can_merge 		= (cmd_write_str[1] == cmd_write_str[0]);
    		assign bm_localid_can_merge 	= (CFG_LOCAL_BURST_MERGE_IDCMP == 1) ? (cmd_id_str[1] == cmd_id_str[0]) : 1'b1;
    		assign bm_valid_can_merge 		= cmd_gen_full;

    		assign bm_nextcol_can_merge         = bm_base_col_addr + (bm_base_size * CFG_DWIDTH_RATIO);
    		assign bm_base_col_addr		    = int_col_addr_postq;
    		assign bm_base_size		    = cmd_size_postq; 
    		assign bm_nextbase_col_addr         = bm_read_ptr[0] ? int_col_addr_str[0] 		: int_col_addr_str[1];  //reverse ordr
    		assign bm_seqcol_can_merge          = (bm_nextcol_can_merge == bm_nextbase_col_addr);
    		assign bm_total_merge_size          = cmd_size_str[1] + cmd_size_str[0];
    		assign bm_oversize                  = (bm_total_merge_size[CFG_LOCAL_SIZE_WIDTH] == 1);
		assign bm_cross_row		    = (bm_nextcol_can_merge[CFG_MEM_IF_COL_WIDTH] == 1);

		assign bm_can_merge = (CFG_ENABLE_BURST_MERGE == 1) ? (bm_valid_can_merge && bm_read_can_merge && bm_write_can_merge && bm_localid_can_merge && bm_bank_can_merge && bm_row_can_merge && bm_cs_can_merge && bm_seqcol_can_merge && !bm_oversize && !bm_cross_row && cmd_gen_full_postq) : 0;

		//2 deep queue
        	always @(posedge ctl_clk, negedge ctl_reset_n)
            		begin
                	if (!ctl_reset_n)
                    		begin
	            		int_col_addr_str[0] <= 0;
    				int_row_addr_str[0] <= 0;
    				int_bank_addr_str[0] <= 0;
    				int_cs_addr_str[0] <= 0;
    				cmd_write_str[0] <= 0;
    				cmd_id_str[0] <= 0;
    				cmd_multicast_str[0] <= 0;
    				cmd_size_str[0] <= 0;
    				cmd_priority_str[0] <= 0;
    				cmd_autoprecharge_str[0] <= 0;
                    		end
                	else
                    		begin
                        	if (cmd_valid && !cmd_gen_full && (bm_write_ptr[0] == 0))
                            		begin
					int_col_addr_str[0] <= int_col_addr;
    					int_row_addr_str[0] <= int_row_addr;
    					int_bank_addr_str[0] <= int_bank_addr;
    					int_cs_addr_str[0] <= int_cs_addr;
    					cmd_write_str[0] <= cmd_write;
    					cmd_id_str[0] <= cmd_id;
    					cmd_multicast_str[0] <= cmd_multicast;
    					cmd_size_str[0] <= cmd_size;
    					cmd_priority_str[0] <= cmd_priority;
    					cmd_autoprecharge_str[0] <= cmd_autoprecharge;
                            		end
				else if (bm_can_merge)
					begin
					if ((bm_read_ptr[0] == 1))
						begin
						int_col_addr_str[0] 	 <= int_col_addr_str[1];
						end
                                	cmd_size_str[0] 	 <= cmd_size_str[0] + cmd_size_str[1];
			 		cmd_priority_str[0] 	 <= cmd_priority_str[0] || cmd_priority_str[1];
					cmd_autoprecharge_str[0] <= cmd_autoprecharge_str[0] || cmd_autoprecharge_str[1];
					cmd_multicast_str[0]	 <= cmd_multicast_str[0] || cmd_multicast_str[1];
					end
                    		end
            		end

		//2 deep queue
       		always @(posedge ctl_clk, negedge ctl_reset_n)
            		begin
                	if (!ctl_reset_n)
                    		begin
				int_col_addr_str[1] <= 0;
    				int_row_addr_str[1] <= 0;
    				int_bank_addr_str[1] <= 0;
    				int_cs_addr_str[1] <= 0;
    				cmd_write_str[1] <= 0;
    				cmd_id_str[1] <= 0;
    				cmd_multicast_str[1] <= 0;
    				cmd_size_str[1] <= 0;
    				cmd_priority_str[1] <= 0;
    				cmd_autoprecharge_str[1] <= 0;
                    		end
                	else
                    		begin
				if (cmd_valid && !cmd_gen_full && (bm_write_ptr[0] == 1))
                            		begin
					int_col_addr_str[1] <= int_col_addr;
    					int_row_addr_str[1] <= int_row_addr;
    					int_bank_addr_str[1] <= int_bank_addr;
    					int_cs_addr_str[1] <= int_cs_addr;
    					cmd_write_str[1] <= cmd_write;
    					cmd_id_str[1] <= cmd_id;
    					cmd_multicast_str[1] <= cmd_multicast;
    					cmd_size_str[1] <= cmd_size;
    					cmd_priority_str[1] <= cmd_priority;
    					cmd_autoprecharge_str[1] <= cmd_autoprecharge;
                            		end
                    		end
            		end

		//write pointer
        	always @(posedge ctl_clk, negedge ctl_reset_n)
            		begin
                	if (!ctl_reset_n)
                    		begin
				bm_write_ptr        <= 0;
                    		end
                	else
                    		begin
                        	if (cmd_valid && !cmd_gen_full)
                            		begin
					bm_write_ptr      <= bm_write_ptr + 1;
                            		end
				else if (bm_can_merge)
					begin
					bm_write_ptr      <= 1;
					end
                   		end
            		end

    		//read pointer
        	always @(posedge ctl_clk, negedge ctl_reset_n)
            		begin
                	if (!ctl_reset_n)
                    		begin
				bm_read_ptr        <= 0;
                    		end
                	else
                    		begin
				if (cmd_valid_postq && !cmd_gen_full_postq)
			    		begin
					bm_read_ptr      <= bm_read_ptr + 1;
			    		end	    
				else if (bm_can_merge)
					begin
					bm_read_ptr      <= 0;
					end
                    		end
            		end
		end
	else
		begin
		assign cmd_gen_full		= cmd_gen_full_postq;
                assign cmd_valid_postq		= cmd_valid;
                assign cmd_write_postq 		= cmd_write;
                assign cmd_read_postq 		= cmd_read;
                assign cmd_id_postq 		= cmd_id;
                assign cmd_multicast_postq 	= cmd_multicast;
                assign cmd_size_postq 		= cmd_size;
                assign cmd_priority_postq 	= cmd_priority;
                assign cmd_autoprecharge_postq 	= cmd_autoprecharge;
                assign int_cs_addr_postq 	= int_cs_addr;
                assign int_bank_addr_postq 	= int_bank_addr;
                assign int_row_addr_postq 	= int_row_addr;
                assign int_col_addr_postq 	= int_col_addr;
		end
endgenerate

    //======================================================================================
    //
    //  [END] Burst Merge
    //
    //======================================================================================

    //======================================================================================
    //
    //  [START] Burst splitting
    //
    //======================================================================================
	assign 	cmd_gen_busy = cmd_gen_load || cmd_valid;
        assign  cmd_gen_full_postq = mux_busy | deassert_ready;
        assign  copy               = ~cmd_gen_full_postq & cmd_valid_postq; // Copy current input command info into a register
	assign  cmd_size_plus_unaligned_size = unaligned_burst + cmd_size_postq;
        assign  require_gen        = (cmd_size_postq > native_size | cmd_size_plus_unaligned_size > native_size) & cfg_enable_cmd_split; // Indicate that current input command require splitting
        
        // CSR address calculation
        always @ (*)
            begin
                max_chip_from_csr = (2**cfg_cs_addr_width)   - 1'b1;
                max_bank_from_csr = (2**cfg_bank_addr_width) - 1'b1;
                max_row_from_csr  = (2**cfg_row_addr_width)  - 1'b1;
                max_col_from_csr  = (2**cfg_col_addr_width)  - 1'b1;
            end
        
        // Calculate native size for selected burstlength and controller rate
        always @ (*)
            begin
                native_size    = 1 * (cfg_burst_length / CFG_DWIDTH_RATIO); // 1 for bl2 FR, 2 for bl8 HR, ...
                native_size_x2 = 2 * (cfg_burst_length / CFG_DWIDTH_RATIO); // 2X of original native size
            end
        
        always @(*)
            begin
                if (native_size == 1)
                    begin
                        unaligned_burst = 0;
                    end
                else if (native_size == 2)
                    begin
                        unaligned_burst = {2'd0,int_col_addr_postq[log2(CFG_DWIDTH_RATIO)]};
                    end
                else if (native_size == 4)
                    begin
                        unaligned_burst = {1'd0,int_col_addr_postq[(log2(CFG_DWIDTH_RATIO)+1):log2(CFG_DWIDTH_RATIO)]};
                    end
                else // native_size == 8
                    begin
                        unaligned_burst = int_col_addr_postq[(log2(CFG_DWIDTH_RATIO)+2):log2(CFG_DWIDTH_RATIO)];
                    end
            end
        
        // Deassert local_ready signal because need to split local command into multiple memory commands
        always @(posedge ctl_clk, negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                    begin
                        deassert_ready <= 0;
                    end
                else
                    begin
                        if (copy && require_gen)
                            begin
                                deassert_ready <= 1;
                            end
                        else if ((buf_size > native_size_x2) && cfg_enable_cmd_split)
                            begin
                                deassert_ready <= 1;
                            end
                        else if (generating && ~mux_busy)
                            begin
                                deassert_ready <= 0;
                            end
                    end
            end
        
        // Assert register signal so that we will pass split command into TBP
        always @ (*)
        begin
            if (copy && require_gen)
                begin
                    registered = 1;
                end
            else
                begin
                    registered = 0;
                end
        end
        
        // Generating signal will notify that current command in under splitting process
        // Signal stays high until the last memory burst aligned command is generated
        always @(posedge ctl_clk, negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                    begin
                        generating <= 0;
                    end
                else
                    begin
                        if (registered)
                            begin
                                generating <= 1;
                            end
                        else if ((generating && buf_size > native_size_x2) && cfg_enable_cmd_split)
                            begin
                                generating <= 1;
                            end
                        else if (~mux_busy)
                            begin
                                generating <= 0;
                            end
                    end
            end
        
        // Determine the correct size
        always @(*)
            begin
                if (!generating)
                    begin
                        if ((cmd_size_plus_unaligned_size < native_size) || !cfg_enable_cmd_split) //(local_size > 1 && !unaligned_burst)
                            begin
                                int_split_size = cmd_size_postq;
                            end
                        else
                            begin
                                int_split_size = native_size - unaligned_burst;
                            end
                    end
                else
                    begin
                        int_split_size = decrmntd_size;
                    end
            end
        
        // MUX logic to determine where to take the command info from
        always @(*)
            begin
                if (!generating) // not generating so take direct input from avalon if
                    begin
                        int_split_read      = cmd_read_postq  & cmd_valid_postq;
                        int_split_write     = cmd_write_postq & cmd_valid_postq;
                        int_split_autopch   = cmd_autoprecharge_postq;
                        int_split_multicast = cmd_multicast_postq;
                        int_split_priority  = cmd_priority_postq;
                        int_split_localid   = cmd_id_postq;
                        int_split_cs_addr   = int_cs_addr_postq;
                        int_split_bank_addr = int_bank_addr_postq;
                        int_split_row_addr  = int_row_addr_postq;
                        int_split_col_addr  = int_col_addr_postq;
                    end
                else // generating cmd so process buffer content
                    begin
                        int_split_read      = buf_read_req;
                        int_split_write     = buf_write_req;
                        int_split_autopch   = buf_autopch_req;
                        int_split_multicast = buf_multicast;
                        int_split_priority  = buf_priority;
                        int_split_localid   = buf_localid;
                        int_split_cs_addr   = incrmntd_cs_addr;
                        int_split_bank_addr = incrmntd_bank_addr;
                        int_split_row_addr  = incrmntd_row_addr;
                        
                        if (cfg_burst_length == 2)
                            begin
                                int_split_col_addr = {incrmntd_col_addr[CFG_MEM_IF_COL_WIDTH-1:1], 1'b0};
                            end
                        else if (cfg_burst_length == 4)
                            begin
                                int_split_col_addr = {incrmntd_col_addr[CFG_MEM_IF_COL_WIDTH-1:2], 2'b00};
                            end
                        else if (cfg_burst_length == 8)
                            begin
                                int_split_col_addr = {incrmntd_col_addr[CFG_MEM_IF_COL_WIDTH-1:3], 3'b000};
                            end
                        else // if (cfg_burst_length == 16)
                            begin
                                int_split_col_addr = {incrmntd_col_addr[CFG_MEM_IF_COL_WIDTH-1:4], 4'b0000};
                            end
                    end
            end
            
            // Split command information
            generate
                if (CFG_CMD_GEN_SPLIT_REGISTERED)
                    begin
                        always @ (posedge ctl_clk or negedge ctl_reset_n)
                            begin
                                if (!ctl_reset_n)
                                    begin
                                        split_cs_addr   <= 0;
                                        split_bank_addr <= 0;
                                        split_row_addr  <= 0;
                                        split_col_addr  <= 0;
                                        split_read      <= 0;
                                        split_write     <= 0;
                                        split_size      <= 0;
                                        split_autopch   <= 0;
                                        split_multicast <= 0;
                                        split_priority  <= 0;
                                        split_localid   <= 0;
                                    end
                                else
                                    begin
                                        if (!queue_full)
                                            begin
                                                split_cs_addr   <= int_split_cs_addr;
                                                split_bank_addr <= int_split_bank_addr;
                                                split_row_addr  <= int_split_row_addr;
                                                split_col_addr  <= int_split_col_addr;
                                                split_read      <= int_split_read;
                                                split_write     <= int_split_write;
                                                split_size      <= int_split_size;
                                                split_autopch   <= int_split_autopch;
                                                split_multicast <= int_split_multicast;
                                                split_priority  <= int_split_priority;
                                                split_localid   <= int_split_localid;
                                            end
                                    end
                            end
                    end
                else
                    begin
                        always @ (*)
                            begin
                                split_cs_addr   = int_split_cs_addr;
                                split_bank_addr = int_split_bank_addr;
                                split_row_addr  = int_split_row_addr;
                                split_col_addr  = int_split_col_addr;
                                split_read      = int_split_read;
                                split_write     = int_split_write;
                                split_size      = int_split_size;
                                split_autopch   = int_split_autopch;
                                split_multicast = int_split_multicast;
                                split_priority  = int_split_priority;
                                split_localid   = int_split_localid;
                            end
                    end
            endgenerate
            
    //======================================================================================
    //
    //  [END] Burst splitting
    //
    //======================================================================================
    
    //======================================================================================
    //
    //  [START] Command buffer
    //
    //======================================================================================
        // Keep track of command size during a split process
        // will keep decreasing when a split command was sent to TBP
        always @(posedge ctl_clk, negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                    begin
                        buf_size      <= 0;
                        decrmntd_size <= 0;
                    end
                else
                    begin
                        if (copy)
                            begin
                                buf_size <= cmd_size_plus_unaligned_size;
                                
                                if ((cmd_size_plus_unaligned_size) > native_size_x2)
                                    begin
                                        decrmntd_size <= native_size;
                                    end
                                else
                                    begin
                                        decrmntd_size <= cmd_size_plus_unaligned_size - native_size;
                                    end
                            end
                        else if (!registered && (buf_size > native_size) && ~mux_busy)
                            begin
                                buf_size <= buf_size - native_size;
                                
                                if ((buf_size - native_size) > native_size_x2)
                                    begin
                                        decrmntd_size <= native_size;
                                    end
                                else
                                    begin
                                        decrmntd_size <= buf_size - native_size_x2;
                                    end
                            end
                    end
            end
        
        // Address max logic, assert '1' if address reaches boundary
        always @ (posedge ctl_clk or negedge ctl_reset_n)
        begin
            if (!ctl_reset_n)
            begin
                buf_chip_addr_reach_max <= 1'b0;
                buf_bank_addr_reach_max <= 1'b0;
                buf_col_addr_reach_max  <= 1'b0;
                
                int_buf_row_addr_reach_max <= 0;
            end
            else
            begin
                if (copy)
                begin
                    buf_chip_addr_reach_max <= (int_cs_addr_postq   == max_chip_from_csr) ? 1'b1 : 1'b0;
                    buf_bank_addr_reach_max <= (int_bank_addr_postq == max_bank_from_csr) ? 1'b1 : 1'b0;
                    
                    int_buf_row_addr_reach_max [3] <= (int_row_addr_postq [     CFG_MEM_IF_ROW_WIDTH      - 1 : 3 * (CFG_MEM_IF_ROW_WIDTH / 4)]  == max_row_from_csr [     CFG_MEM_IF_ROW_WIDTH      - 1 : 3 * (CFG_MEM_IF_ROW_WIDTH / 4)]) ? 1'b1 : 1'b0;
                    int_buf_row_addr_reach_max [2] <= (int_row_addr_postq [3 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 2 * (CFG_MEM_IF_ROW_WIDTH / 4)]  == max_row_from_csr [3 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 2 * (CFG_MEM_IF_ROW_WIDTH / 4)]) ? 1'b1 : 1'b0;
                    int_buf_row_addr_reach_max [1] <= (int_row_addr_postq [2 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 1 * (CFG_MEM_IF_ROW_WIDTH / 4)]  == max_row_from_csr [2 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 1 * (CFG_MEM_IF_ROW_WIDTH / 4)]) ? 1'b1 : 1'b0;
                    int_buf_row_addr_reach_max [0] <= (int_row_addr_postq [1 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 0 * (CFG_MEM_IF_ROW_WIDTH / 4)]  == max_row_from_csr [1 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 0 * (CFG_MEM_IF_ROW_WIDTH / 4)]) ? 1'b1 : 1'b0;
                    
                    if ((cfg_burst_length == 16 && int_col_addr_postq[CFG_MEM_IF_COL_WIDTH-1:4] == max_col_from_csr[CFG_MEM_IF_COL_WIDTH-1:4])
                        ||
                        (cfg_burst_length ==  8 && int_col_addr_postq[CFG_MEM_IF_COL_WIDTH-1:3] == max_col_from_csr[CFG_MEM_IF_COL_WIDTH-1:3])
                        ||
                        (cfg_burst_length ==  4 && int_col_addr_postq[CFG_MEM_IF_COL_WIDTH-1:2] == max_col_from_csr[CFG_MEM_IF_COL_WIDTH-1:2])
                        ||
                        (cfg_burst_length ==  2 && int_col_addr_postq[CFG_MEM_IF_COL_WIDTH-1:1] == max_col_from_csr[CFG_MEM_IF_COL_WIDTH-1:1])
                        )
                    begin
                        buf_col_addr_reach_max <= 1'b1;
                    end
                    else
                    begin
                        buf_col_addr_reach_max <= 1'b0;
                    end
                end
                else if (generating && ~mux_busy)
                begin
                    buf_chip_addr_reach_max <= (buf_cs_addr   == max_chip_from_csr) ? 1'b1 : 1'b0;
                    buf_bank_addr_reach_max <= (buf_bank_addr == max_bank_from_csr) ? 1'b1 : 1'b0;
                    
                    int_buf_row_addr_reach_max [3] <= (buf_row_addr [     CFG_MEM_IF_ROW_WIDTH      - 1 : 3 * (CFG_MEM_IF_ROW_WIDTH / 4)]  == max_row_from_csr [     CFG_MEM_IF_ROW_WIDTH      - 1 : 3 * (CFG_MEM_IF_ROW_WIDTH / 4)]) ? 1'b1 : 1'b0;
                    int_buf_row_addr_reach_max [2] <= (buf_row_addr [3 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 2 * (CFG_MEM_IF_ROW_WIDTH / 4)]  == max_row_from_csr [3 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 2 * (CFG_MEM_IF_ROW_WIDTH / 4)]) ? 1'b1 : 1'b0;
                    int_buf_row_addr_reach_max [1] <= (buf_row_addr [2 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 1 * (CFG_MEM_IF_ROW_WIDTH / 4)]  == max_row_from_csr [2 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 1 * (CFG_MEM_IF_ROW_WIDTH / 4)]) ? 1'b1 : 1'b0;
                    int_buf_row_addr_reach_max [0] <= (buf_row_addr [1 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 0 * (CFG_MEM_IF_ROW_WIDTH / 4)]  == max_row_from_csr [1 * (CFG_MEM_IF_ROW_WIDTH / 4) - 1 : 0 * (CFG_MEM_IF_ROW_WIDTH / 4)]) ? 1'b1 : 1'b0;
                    
                    if ((cfg_burst_length == 16 && (buf_col_addr[CFG_MEM_IF_COL_WIDTH-1:4] + 1'b1) == max_col_from_csr[CFG_MEM_IF_COL_WIDTH-1:4])
                        ||
                        (cfg_burst_length ==  8 && (buf_col_addr[CFG_MEM_IF_COL_WIDTH-1:3] + 1'b1) == max_col_from_csr[CFG_MEM_IF_COL_WIDTH-1:3])
                        ||
                        (cfg_burst_length ==  4 && (buf_col_addr[CFG_MEM_IF_COL_WIDTH-1:2] + 1'b1) == max_col_from_csr[CFG_MEM_IF_COL_WIDTH-1:2])
                        ||
                        (cfg_burst_length ==  2 && (buf_col_addr[CFG_MEM_IF_COL_WIDTH-1:1] + 1'b1) == max_col_from_csr[CFG_MEM_IF_COL_WIDTH-1:1])
                        )
                    begin
                        buf_col_addr_reach_max <= 1'b1;
                    end
                    else
                    begin
                        buf_col_addr_reach_max <= 1'b0;
                    end
                end
            end
        end
        
        always @ (*)
        begin
            buf_row_addr_reach_max = &int_buf_row_addr_reach_max;
        end
        
        // Buffered command info, to be used in split process
        always @(posedge ctl_clk, negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                    begin
                        buf_read_req    <= 1'b0;
                        buf_write_req   <= 1'b0;
                        buf_autopch_req <= 1'b0;
                        buf_multicast   <= 1'b0;
                        buf_priority    <= 1'b0;
                        buf_localid     <= 0;
                    end
                else
                    begin
                        if (copy)
                            begin
                                buf_read_req    <= cmd_read_postq;
                                buf_write_req   <= cmd_write_postq;
                                buf_autopch_req <= cmd_autoprecharge_postq;
                                buf_multicast   <= cmd_multicast_postq;
                                buf_priority    <= cmd_priority_postq;
                                buf_localid     <= cmd_id_postq;
                            end
                    end
            end
        
        // Keep track of command address during a split process
        // will keep increasing when a split command was sent to TBP
        // also takes into account address order
        always @(posedge ctl_clk, negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                    begin
                        buf_cs_addr   <= 0;
                        buf_bank_addr <= 0;
                        buf_row_addr  <= 0;
                        buf_col_addr  <= 0;
                    end
                else
                    if (copy)
                        begin
                            buf_cs_addr   <= int_cs_addr_postq;
                            buf_bank_addr <= int_bank_addr_postq;
                            buf_row_addr  <= int_row_addr_postq;
                            buf_col_addr  <= int_col_addr_postq;
                        end
                    else if (generating && ~mux_busy)
                        begin
                            buf_cs_addr   <= incrmntd_cs_addr;
                            buf_bank_addr <= incrmntd_bank_addr;
                            buf_row_addr  <= incrmntd_row_addr;
                            buf_col_addr  <= incrmntd_col_addr;
                        end
            end
        
        always @ (*)
            begin
                if (buf_col_addr_reach_max)
                    begin
                        incrmntd_cs_addr   = buf_cs_addr;
                        incrmntd_bank_addr = buf_bank_addr;
                        incrmntd_row_addr  = buf_row_addr;
                        incrmntd_col_addr  = buf_col_addr;
                        
                        if (cfg_burst_length == 16)
                            begin
                                incrmntd_col_addr[CFG_MEM_IF_COL_WIDTH-1:4] = 0;
                            end
                        else if (cfg_burst_length == 8)
                            begin
                                incrmntd_col_addr[CFG_MEM_IF_COL_WIDTH-1:3] = 0;
                            end
                        else if (cfg_burst_length == 4)
                            begin
                                incrmntd_col_addr[CFG_MEM_IF_COL_WIDTH-1:2] = 0;
                            end
                        else //  if (cfg_burst_length == 2)
                            begin
                                incrmntd_col_addr[CFG_MEM_IF_COL_WIDTH-1:1] = 0;
                            end
                        
                        if (cfg_addr_order == `MMR_ADDR_ORDER_ROW_CS_BA_COL) // 2 is rowchipbankcol
                            begin
                                if (buf_bank_addr_reach_max)
                                    begin
                                        incrmntd_bank_addr = 0;
                                        if (buf_chip_addr_reach_max)
                                            begin
                                                incrmntd_cs_addr = 0;
                                                if (buf_row_addr_reach_max)
                                                    begin
                                                        incrmntd_row_addr = 0;
                                                    end
                                                else
                                                    begin
                                                        incrmntd_row_addr = buf_row_addr + 1'b1;
                                                    end
                                            end
                                        else
                                            begin
                                                incrmntd_cs_addr = buf_cs_addr + 1'b1;
                                            end
                                    end
                                else
                                    begin
                                        incrmntd_bank_addr = buf_bank_addr + 1'b1;
                                    end
                            end
                        else if (cfg_addr_order == `MMR_ADDR_ORDER_CS_BA_ROW_COL) // 1 is chipbankrowcol
                            begin
                                if (buf_row_addr_reach_max)
                                    begin
                                        incrmntd_row_addr = 0;
                                        if (buf_bank_addr_reach_max)
                                            begin
                                                incrmntd_bank_addr = 0;
                                                if (buf_chip_addr_reach_max)
                                                    begin
                                                        incrmntd_cs_addr = 0;
                                                    end
                                                else
                                                    begin
                                                        incrmntd_cs_addr = buf_cs_addr + 1'b1;
                                                    end
                                            end
                                        else
                                            begin
                                                incrmntd_bank_addr = buf_bank_addr + 1'b1;
                                            end
                                    end
                                else
                                    begin
                                        incrmntd_row_addr = buf_row_addr + 1'b1;
                                    end
                            end
                        else // 0 is chiprowbankcol
                            begin
                                if (buf_bank_addr_reach_max)
                                    begin
                                        incrmntd_bank_addr = 0;
                                        if (buf_row_addr_reach_max)
                                            begin
                                                incrmntd_row_addr = 0;
                                                if (buf_chip_addr_reach_max)
                                                    begin
                                                        incrmntd_cs_addr = 0;
                                                    end
                                                else
                                                    begin
                                                        incrmntd_cs_addr = buf_cs_addr + 1'b1;
                                                    end
                                            end
                                        else
                                            begin
                                                incrmntd_row_addr = buf_row_addr + 1'b1;
                                            end
                                    end
                                else
                                    begin
                                        incrmntd_bank_addr = buf_bank_addr + 1'b1;
                                    end
                            end
                    end
                else
                    begin
                        incrmntd_cs_addr   = buf_cs_addr;
                        incrmntd_bank_addr = buf_bank_addr;
                        incrmntd_row_addr  = buf_row_addr;
                        incrmntd_col_addr  = buf_col_addr + cfg_burst_length;
                    end
            end
    //======================================================================================
    //
    //  [END] Command buffer
    //
    //======================================================================================
    
    //======================================================================================
    //
    //  [START] Data ID related logic
    //
    //======================================================================================
        // proc signals to datapath
        assign proc_busy        = (cfg_enable_rmw) ? ecc_proc_busy        : tbp_full;
        assign proc_load        = (cfg_enable_rmw) ? ecc_proc_load        : cmd_gen_load;
        assign proc_load_dataid = (cfg_enable_rmw) ? ecc_proc_load_dataid : cmd_gen_load;
        assign proc_write       = (cfg_enable_rmw) ? ecc_proc_write       : cmd_gen_write;
        assign proc_read        = (cfg_enable_rmw) ? ecc_proc_read        : cmd_gen_read;
        assign proc_size        = (cfg_enable_rmw) ? ecc_proc_size        : cmd_gen_size;
        assign proc_localid     = (cfg_enable_rmw) ? ecc_proc_localid     : cmd_gen_localid;
        assign tbp_load_index   = (cfg_enable_rmw) ? 1                    : tbp_load; // set to '1' in RMW mode, because we need data_complete[0] to toggle indicating wdata is complete
        
        always @ (*)
            begin
		ecc_proc_busy      = !ecc_proc_busy_parwr_read_queue_load && !split_queue_load_for_avl; 
		ecc_proc_busy_parwr_read_queue_load = !queue_full && ((ecc_state_sm == 4'h4) || (ecc_state_sm == 4'h1)); 
		ecc_proc_load        = (ecc_proc_write | ecc_proc_read);
                ecc_proc_load_dataid = !ecc_rmw_read;                                               // De-assert when we're loading an RMW read
            end
        
        // Seperate into 2 process because ecc_proc read/write needs to be assigned seperately, due to blocking assignments
        always @ (*)
            begin
                ecc_proc_write       = int_split_write;  
                ecc_proc_read        = int_split_read | ecc_rmw_read;                               // Assert when we're loading an RMW read
                ecc_proc_size        = int_split_size;
                ecc_proc_localid     = int_split_localid;
            end
        
    //======================================================================================
    //
    //  [END] Data ID related logic
    //
    //======================================================================================
    
    //======================================================================================
    //
    //  [START] ECC related logic
    //
    //======================================================================================
			
	//State Machine
        always @ (posedge ctl_clk or negedge ctl_reset_n)
        begin
        	if (!ctl_reset_n)
                begin
                    ecc_state_sm  <= IDLE;
                    ecc_read      <= 1'b0;
                    ecc_write     <= 1'b0;
                    ecc_rmw_read  <= 1'b0;
                    ecc_rmw_write <= 1'b0;
                    correct       <= 1'b0;
                    errcmd_ready  <= 1'b0;
                    partial_opr   <= 1'b0;
                end
                else
                begin
                    case (ecc_state_sm)
		    	IDLE :
                        begin
                       		if (cfg_enable_ecc && errcmd_valid)     
                        	begin
                        		ecc_state_sm  <= CORRECT_RD;
					ecc_read      <= 1'b1;
					ecc_write     <= 1'b0;
                			ecc_rmw_read  <= 1'b1;
                			ecc_rmw_write <= 1'b0;
					correct       <= 1'b1;
					errcmd_ready  <= 1'b1;
                			partial_opr   <= 1'b0;
                        	end
				else if (cfg_enable_rmw && int_ecc_write_final && int_ecc_data_complete_final && !int_ecc_partial_be_final)
				begin
					ecc_state_sm  <= FULL_WR;	//4'h3
					ecc_read      <= 1'b0;
                			ecc_write     <= 1'b1;
					ecc_rmw_read  <= 1'b0;
					ecc_rmw_write <= 1'b0;
					correct       <= 1'b0;
					errcmd_ready  <= 1'b0;
					partial_opr   <= 1'b0;
				end
				else if (cfg_enable_rmw && int_ecc_write_final && int_ecc_data_complete_final && int_ecc_partial_be_final)
				begin
					ecc_state_sm  <= PARWR_RD;
               	 			ecc_read      <= 1'b1;
                			ecc_write     <= 1'b0;
                			ecc_rmw_read  <= 1'b1;
                			ecc_rmw_write <= 1'b0;
					correct       <= 1'b0;
					errcmd_ready  <= 1'b0;
                			partial_opr   <= 1'b1;
				end
				else if (cfg_enable_rmw && int_ecc_read_final)
				begin
					ecc_state_sm  <= FULL_RD;	//4'h6
					ecc_read      <= 1'b1;
					ecc_write     <= 1'b0;
					ecc_rmw_read  <= 1'b0;
					ecc_rmw_write <= 1'b0;
					correct       <= 1'b0;
					errcmd_ready  <= 1'b0;
					partial_opr   <= 1'b0;
				end
				else 
				begin
					ecc_state_sm  <= IDLE;
					ecc_read      <= 1'b0;
					ecc_write     <= 1'b0;
                			ecc_rmw_read  <= 1'b0;
                			ecc_rmw_write <= 1'b0;
					correct       <= 1'b0;
					errcmd_ready  <= 1'b0;
                			partial_opr   <= 1'b0;
				end
			end
			CORRECT_RD :
                        begin
				errcmd_ready <= 1'b0;
				if (!queue_full)
				begin
					ecc_state_sm  <= CORRECT_WR;
					ecc_read      <= 1'b0;
					ecc_write     <= 1'b1;
                			ecc_rmw_read  <= 1'b0;
               	 			ecc_rmw_write <= 1'b1;
					correct       <= 1'b1;
					errcmd_ready  <= 1'b0;
                			partial_opr   <= 1'b0;
				end
			end
			CORRECT_WR :
                        begin
				if (!queue_full)
				begin
					if (cfg_enable_ecc && errcmd_valid)     
                        		begin
                        			ecc_state_sm  <= CORRECT_RD;
						ecc_read      <= 1'b1;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b1;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b1;
						errcmd_ready  <= 1'b1;
                				partial_opr   <= 1'b0;
                        		end
					else if (cfg_enable_rmw && int_ecc_write_final && int_ecc_data_complete_final && !int_ecc_partial_be_final)
					begin
						ecc_state_sm  <= FULL_WR;
						ecc_read      <= 1'b0;
						ecc_write     <= 1'b1;
						ecc_rmw_read  <= 1'b0;
						ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
						partial_opr   <= 1'b0;
					end
					else if (cfg_enable_rmw && int_ecc_write_final && int_ecc_data_complete_final && int_ecc_partial_be_final)
					begin
						ecc_state_sm  <= PARWR_RD;
               	 				ecc_read      <= 1'b1;
                				ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b1;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b1;
					end
					else if (cfg_enable_rmw && int_ecc_read_final)
					begin
						ecc_state_sm  <= FULL_RD;	//4'h6
						ecc_read      <= 1'b1;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b0;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;
					end
					else 
					begin
						ecc_state_sm  <= IDLE;
						ecc_read      <= 1'b0;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b0;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;
					end
				end
			end
			PARWR_RD :
                        begin
				if (!queue_full)	     
                        	begin
                        		ecc_state_sm  <= PARWR_WR;	//4'h2
					ecc_read      <= 1'b0;
					ecc_write     <= 1'b1;
                			ecc_rmw_read  <= 1'b0;
                			ecc_rmw_write <= 1'b1;
					correct       <= 1'b0;
					errcmd_ready  <= 1'b0;
                			partial_opr   <= 1'b1;
				end
			end
			PARWR_WR :	//4'h2
                        begin
				if (!queue_full)
				begin
					if (cfg_enable_ecc && errcmd_valid)     
                        		begin
                        			ecc_state_sm  <= CORRECT_RD;
						ecc_read      <= 1'b1;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b1;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b1;
						errcmd_ready  <= 1'b1;
                				partial_opr   <= 1'b0;
                        		end	
					else if (cfg_enable_rmw && int_ecc_write_final && int_ecc_data_complete_final && !int_ecc_partial_be_final) 
					begin
						ecc_state_sm  <= FULL_WR;
						ecc_read      <= 1'b0;
						ecc_write     <= 1'b1;
                				ecc_rmw_read  <= 1'b0;
               	 				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;
					end
					else if (cfg_enable_rmw && int_ecc_write_final && int_ecc_data_complete_final && int_ecc_partial_be_final)
					begin
						ecc_state_sm  <= PARWR_RD;
               	 				ecc_read      <= 1'b1;
                				ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b1;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b1;
					end
					else if (cfg_enable_rmw && int_ecc_read_final)
					begin
						ecc_state_sm  <= FULL_RD;	//4'h6
						ecc_read      <= 1'b1;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b0;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;
					end
					else 
					begin
						ecc_state_sm  <= IDLE;
						ecc_read      <= 1'b0;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b0;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;
					end
				end
			end
			FULL_WR :	//4'h3
                        begin
				if (!queue_full)
				begin
					if (cfg_enable_ecc && errcmd_valid)     
                        		begin
                        			ecc_state_sm  <= CORRECT_RD;
						ecc_read      <= 1'b1;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b1;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b1;
						errcmd_ready  <= 1'b1;
                				partial_opr   <= 1'b0;
                        		end
					else if (cfg_enable_rmw && int_ecc_write_final && int_ecc_data_complete_final && int_ecc_partial_be_final) 
                        		begin
						ecc_state_sm  <= PARWR_RD;
               	 				ecc_read      <= 1'b1;
                				ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b1;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b1;
                        		end
					else if (cfg_enable_rmw && int_ecc_write_final && int_ecc_data_complete_final && !int_ecc_partial_be_final) 
                        		begin
						ecc_state_sm  <= FULL_WR;
						ecc_read      <= 1'b0;
						ecc_write     <= 1'b1;
                				ecc_rmw_read  <= 1'b0;
               	 				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;

                        		end
					else if (cfg_enable_rmw && int_ecc_read_final)
					begin
						ecc_state_sm  <= FULL_RD;	//4'h6
						ecc_read      <= 1'b1;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b0;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;
					end
					else 
					begin
						ecc_state_sm  <= IDLE;
						ecc_read      <= 1'b0;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b0;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;
					end
				end
			end
			FULL_RD :	//4'h6
                        begin
				if (!queue_full)
				begin
					if (cfg_enable_ecc && errcmd_valid)     
                        		begin
                        			ecc_state_sm  <= CORRECT_RD;
						ecc_read      <= 1'b1;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b1;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b1;
						errcmd_ready  <= 1'b1;
                				partial_opr   <= 1'b0;
                        		end
					else if (cfg_enable_rmw && int_ecc_write_final && int_ecc_data_complete_final && !int_ecc_partial_be_final)
					begin
						ecc_state_sm  <= FULL_WR;
						ecc_read      <= 1'b0;
						ecc_write     <= 1'b1;
                				ecc_rmw_read  <= 1'b0;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;
					end
					else if (cfg_enable_rmw && int_ecc_write_final && int_ecc_data_complete_final && int_ecc_partial_be_final)
					begin
						ecc_state_sm  <= PARWR_RD;
               	 				ecc_read      <= 1'b1;
                				ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b1;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b1;
                        		end
					else if (cfg_enable_rmw && int_ecc_read_final)
					begin
						ecc_state_sm  <= FULL_RD;	//4'h6
						ecc_read      <= 1'b1;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b0;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;
					end
					else 
					begin
						ecc_state_sm  <= IDLE;
						ecc_read      <= 1'b0;
						ecc_write     <= 1'b0;
                				ecc_rmw_read  <= 1'b0;
                				ecc_rmw_write <= 1'b0;
						correct       <= 1'b0;
						errcmd_ready  <= 1'b0;
                				partial_opr   <= 1'b0;
					end
				end
			end
			default :
                        begin
                                ecc_state_sm  <= IDLE;
				ecc_read      <= 1'b0;
				ecc_write     <= 1'b0;
                		ecc_rmw_read  <= 1'b0;
                		ecc_rmw_write <= 1'b0;
				correct       <= 1'b0;
				errcmd_ready  <= 1'b0;
                		partial_opr   <= 1'b0;
                        end
		   endcase
		end
        end		

        // for ECC usage only
        always @ (posedge ctl_clk or negedge ctl_reset_n)
        begin
            if (!ctl_reset_n)
            begin
		split_queue_load_open <= 1'b0;
            end
            else
            begin
		if (split_queue_load_for_avl) 
		begin
			split_queue_load_open <= int_split_write;	//set flag to indicate waiting for data_complete
		end
		else if (data_complete[0])
		begin
			split_queue_load_open <= 1'b0;	//clear flag to indicate data_complete has arrived
		end
            end
        end
        
        // Delay cmd_gen command by one cycle
        always @ (posedge ctl_clk or negedge ctl_reset_n)
        begin
            if (!ctl_reset_n)
            begin
                int_ecc_cs_addr   <= 0;
                int_ecc_bank_addr <= 0;
                int_ecc_row_addr  <= 0;
                int_ecc_col_addr  <= 0;
                int_ecc_read      <= 0;
                int_ecc_write     <= 0;
                int_ecc_size      <= 0;
                int_ecc_autopch   <= 0;
                int_ecc_multicast <= 0;
                int_ecc_priority  <= 0;
                int_ecc_localid   <= 0;
                int_ecc_dataid    <= 0;
            end
            else if (split_queue_load) // hold off the next command when ECC logic is busy
	    begin
                int_ecc_cs_addr   <= int_split_cs_addr;
                int_ecc_bank_addr <= int_split_bank_addr;
                int_ecc_row_addr  <= int_split_row_addr;
                int_ecc_col_addr  <= int_split_col_addr;
               	int_ecc_read      <= int_split_read_final;	//When data_id not ready, suppress load	
		int_ecc_write     <= int_split_write_final;	//When data_id not ready, suppress load
                int_ecc_size      <= int_split_size;
                int_ecc_autopch   <= int_split_autopch;
                int_ecc_multicast <= int_split_multicast;
                int_ecc_priority  <= int_split_priority;
                int_ecc_localid   <= int_split_localid;
                
                if (int_split_write)
                begin
                    int_ecc_dataid <= wdatap_free_id_dataid;
                end
                else
                begin
                    int_ecc_dataid <= rdatap_free_id_dataid;
                end
            end
        end

        always @ (posedge ctl_clk or negedge ctl_reset_n)
        begin
            if (!ctl_reset_n)
            begin
		int_ecc_data_complete <= 0;
		int_ecc_partial_be    <= 0;
            end
            else 
	    begin
		if (data_complete[0])
		begin
			int_ecc_data_complete <= data_complete[0];
			int_ecc_partial_be    <= data_partial_be;
		end
		else if (split_queue_load_for_avl)
		begin
			int_ecc_data_complete <= 0;
			int_ecc_partial_be    <= 0;
		end
            end
        end
       


        // Buffer for ECC command information
        always @(posedge ctl_clk, negedge ctl_reset_n)
        begin
            if (!ctl_reset_n)
            begin
                ecc_cs_addr   <= 0;
                ecc_bank_addr <= 0;
                ecc_row_addr  <= 0;
                ecc_col_addr  <= 0;
                ecc_size      <= 0;
                ecc_autopch   <= 0;
                ecc_multicast <= 0;
                ecc_dataid    <= 0;
                ecc_localid   <= 0;
                ecc_priority  <= 0;
            end
            else
            begin
                if (ecc_queue_load) //when queue take, load in new command
						             //except when doing correction
                begin
                    ecc_cs_addr   <= take_from_ecc_correct ? errcmd_chipsel : int_ecc_cs_addr;
                    ecc_bank_addr <= take_from_ecc_correct ? errcmd_bank    : int_ecc_bank_addr;
                    ecc_row_addr  <= take_from_ecc_correct ? errcmd_row     : int_ecc_row_addr;
                    ecc_col_addr  <= take_from_ecc_correct ? errcmd_column  : int_ecc_col_addr;
                    ecc_size      <= take_from_ecc_correct ? errcmd_size    : int_ecc_size;
                    ecc_autopch   <= take_from_ecc_correct ? 1'b0 	    : int_ecc_autopch;
                    ecc_multicast <= take_from_ecc_correct ? 1'b0 	    : int_ecc_multicast;
                    ecc_localid   <= take_from_ecc_correct ? 0 		    : int_ecc_localid;
                    ecc_priority  <= take_from_ecc_correct ? errcmd_localid : int_ecc_priority;
                    ecc_dataid    <= take_from_ecc_correct ? 1'b0 	    : int_ecc_dataid;
                end
		else	//on hold
		begin
                    ecc_cs_addr   <= ecc_cs_addr;
                    ecc_bank_addr <= ecc_bank_addr;
                    ecc_row_addr  <= ecc_row_addr;
                    ecc_col_addr  <= ecc_col_addr;
                    ecc_size      <= ecc_size;
                    ecc_autopch   <= ecc_autopch;
                    ecc_multicast <= ecc_multicast;
                    ecc_localid   <= ecc_localid;
                    ecc_priority  <= ecc_priority;
                    ecc_dataid    <= ecc_dataid;
                end
            end
        end

	assign ecc_queue_load	        = (((ecc_state_sm == 4'b0000) && (int_ecc_read_final || int_ecc_write_final || errcmd_valid)) ||
					   ((ecc_state_sm == 4'b0011) && (int_ecc_read_final || int_ecc_write_final || errcmd_valid) && !queue_full) || 
				   	   ((ecc_state_sm == 4'b0110) && (int_ecc_read_final || int_ecc_write_final || errcmd_valid) && !queue_full) ||
					   ((ecc_state_sm == 4'b1000) && (int_ecc_read_final || int_ecc_write_final || errcmd_valid) && !queue_full) ||
					   ((ecc_state_sm == 4'b0010) && (int_ecc_read_final || int_ecc_write_final || errcmd_valid) && !queue_full)); 
	
	assign split_queue_load          = (split_queue_load_init || split_queue_load_ecc_load); 
	assign split_queue_load_init     = (!int_ecc_read && !int_ecc_write && (int_split_read_final || int_split_write_final) && (ecc_state_sm != 4'h4) && (ecc_state_sm != 4'h1)); 
	assign split_queue_load_ecc_load = (ecc_queue_load && (!take_from_ecc_correct));
	assign split_queue_load_for_avl  = split_queue_load && (int_split_read_final || int_split_write_final);
	assign int_split_read_final      = (int_split_read && rdatap_free_id_valid);
	assign int_split_write_final     = (int_split_write && wdatap_free_id_valid);

	assign split_queue_fast_unload   = data_complete[0] && split_queue_load_ecc_load;

        assign int_ecc_data_complete_final = split_queue_fast_unload ? data_complete[0] : int_ecc_data_complete;
	assign int_ecc_partial_be_final    = split_queue_fast_unload ? data_partial_be  : int_ecc_partial_be;

	assign split_queue_load_open_final = split_queue_load_open && !data_complete[0];
	assign int_ecc_read_final          = int_ecc_read  && !split_queue_load_open_final;
	assign int_ecc_write_final         = int_ecc_write && !split_queue_load_open_final;

        assign take_from_ecc_correct    = cfg_enable_ecc && errcmd_valid &&
					  ((ecc_state_sm == 4'h0) ||
 					   ((ecc_state_sm == 4'h8) && !queue_full) ||
 					   ((ecc_state_sm == 4'h2) && !queue_full) ||
 					   ((ecc_state_sm == 4'h3) && !queue_full) ||
 					   ((ecc_state_sm == 4'h6) && !queue_full));

        // ECC partial and correct information
        always @ (*)
            begin
                ecc_correct = correct;
                ecc_partial = partial_opr;
            end
        
        // Waiting to load signal, indicate to TBP that we're about to load TBP but cmd_gen_load signal is not high
        // this signal is required so that TBP will flush old contents in no-DM or ECC mode, else it'll cause in-efficiency
        // in no-DM or ECC mode, we need to wait for write data to arrive before asserting read (partial write) or write (full write) request to TBP
        // therefore TBP won't flush existing content in TBP because it didn't know command generator is about to load new command
        always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                    begin
                        waiting_to_load <= 1'b0;
                    end
                else
                    begin
                        if (tbp_full) // need to load when TBP is full, case:93272, avl_ready will stuck low if we keep all tbp slots with open page, this will free up at least one slot
                            begin
                                waiting_to_load <= 1'b1;
                            end
                        else
                            begin
                                waiting_to_load <= 1'b0;
                            end
                    end
            end
    //======================================================================================
    //
    //  [END] ECC related logic
    //
    //======================================================================================
    
    //======================================================================================
    //
    //  [START] Mux Logic
    //
    //======================================================================================
        assign mux_busy         = cfg_enable_rmw ? mux_busy_ecc : mux_busy_non_ecc;
        assign mux_busy_non_ecc = queue_full;	
	assign mux_busy_ecc     = (!split_queue_load_for_avl); 

        assign  muxed_cs_addr           = (cfg_enable_rmw) ?  ecc_cs_addr           :  split_cs_addr;
        assign  muxed_bank_addr         = (cfg_enable_rmw) ?  ecc_bank_addr         :  split_bank_addr;
        assign  muxed_row_addr          = (cfg_enable_rmw) ?  ecc_row_addr          :  split_row_addr;
        assign  muxed_col_addr          = (cfg_enable_rmw) ?  ecc_col_addr          :  split_col_addr;
        assign  muxed_read              = (cfg_enable_rmw) ?  ecc_read              :  split_read;
        assign  muxed_write             = (cfg_enable_rmw) ?  ecc_write             :  split_write;
        assign  muxed_size              = (cfg_enable_rmw) ?  ecc_size              :  split_size;
        assign  muxed_autopch           = (cfg_enable_rmw) ?  ecc_autopch           :  split_autopch;
        assign  muxed_multicast         = (cfg_enable_rmw) ?  ecc_multicast         :  split_multicast;
        assign  muxed_localid           = (cfg_enable_rmw) ?  ecc_localid           :  split_localid;
        assign  muxed_priority          = (cfg_enable_rmw) ?  ecc_priority          :  split_priority;
        assign  muxed_dataid            = (cfg_enable_rmw) ?  ecc_dataid            : (muxed_read ? rdatap_free_id_dataid : wdatap_free_id_dataid);
        assign  muxed_complete          = (cfg_enable_rmw) ? !ecc_rmw_write         :  split_read; // Data is always complete for read command, therefore set it to split_read
        assign  muxed_correct           = (cfg_enable_rmw) ?  ecc_correct           :  1'b0;
        assign  muxed_partial           = (cfg_enable_rmw) ?  ecc_partial           :  1'b0;
    //======================================================================================
    //
    //  [END] Mux Logic
    //
    //======================================================================================
    
    //======================================================================================
    //
    //  [START] Queue Logic
    //
    //======================================================================================
        // mapping of buffer_input
        assign buffer_input = {muxed_read,muxed_write,muxed_multicast,muxed_autopch,muxed_priority,muxed_complete,muxed_correct,muxed_partial,muxed_dataid,muxed_localid,muxed_size,muxed_cs_addr,muxed_row_addr,muxed_bank_addr,muxed_col_addr};
        
        // avalon_write_req & avalon_read_req is AND with internal_ready in alt_ddrx_avalon_if.v
        assign write_to_queue = (muxed_read | muxed_write) & ~queue_full;
        assign fetch = cmd_gen_load & ~tbp_full;
        
        //pipefull and pipe register chain
        //feed 0 to pipefull entry that is empty
        always @(posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                    begin
                        for(j=0; j<CFG_CTL_QUEUE_DEPTH; j=j+1)
                            begin
                                pipefull[j] <= 1'b0;
                                pipe    [j] <= 0;
                            end
                    end
                else
                    begin
                        if (fetch) // fetch and write
                            begin
                                        for(j=0; j<CFG_CTL_QUEUE_DEPTH-1; j=j+1)
                                            begin
                                                if(pipefull[j] == 1'b1 & pipefull[j+1] == 1'b0)
                                                    begin
                                                        pipefull[j] <= write_to_queue;
                                                        pipe    [j] <= buffer_input;
                                                    end
                                                else
                                                    begin
                                                        pipefull[j] <= pipefull[j+1];
                                                        pipe    [j] <= pipe    [j+1];
                                                    end
                                            end
                                        
                                        pipefull[CFG_CTL_QUEUE_DEPTH-1] <= pipefull[CFG_CTL_QUEUE_DEPTH-1] & write_to_queue;
                                        pipe    [CFG_CTL_QUEUE_DEPTH-1] <= pipe    [CFG_CTL_QUEUE_DEPTH-1] & buffer_input;
                            end
                        else if (write_to_queue) // write only
                            begin
                                        for(j=1; j<CFG_CTL_QUEUE_DEPTH; j=j+1)
                                            begin
                                                if(pipefull[j-1] == 1'b1 & pipefull[j] == 1'b0)
                                                    begin
                                                        pipefull[j] <= 1'b1;
                                                        pipe    [j] <= buffer_input;
                                                    end
                                            end
                                        
                                        if(pipefull[0] == 1'b0)
                                            begin
                                                pipefull[0] <= 1'b1;
                                                pipe    [0] <= buffer_input;
                                            end
                            end
                    end
            end
    //======================================================================================
    //
    //  [END] Queue Logic
    //
    //======================================================================================
    
    //======================================================================================
    //
    //  [START] Output Logic
    //
    //======================================================================================
        generate
                begin
                    wire                                     int_queue_full;
                    
                    reg     [CFG_CTL_TBP_NUM-1:0]            int_same_chipsel_addr;
                    reg     [CFG_CTL_TBP_NUM-1:0]            int_same_bank_addr;
                    reg     [CFG_CTL_TBP_NUM-1:0]            int_same_row_addr_0;
                    reg     [CFG_CTL_TBP_NUM-1:0]            int_same_row_addr_1;
                    reg     [CFG_CTL_TBP_NUM-1:0]            int_same_row_addr_2;
                    reg     [CFG_CTL_TBP_NUM-1:0]            int_same_row_addr_3;
                    reg     [CFG_CTL_TBP_NUM-1:0]            int_same_col_addr;
                    reg     [CFG_CTL_TBP_NUM-1:0]            int_same_read_cmd;
                    reg     [CFG_CTL_TBP_NUM-1:0]            int_same_write_cmd;
                    
                    reg     [CFG_CTL_SHADOW_TBP_NUM-1:0]     int_same_shadow_chipsel_addr;
                    reg     [CFG_CTL_SHADOW_TBP_NUM-1:0]     int_same_shadow_bank_addr;
                    reg     [CFG_CTL_SHADOW_TBP_NUM-1:0]     int_same_shadow_row_addr;
                    
                    reg                                      int_register_valid;
                    
                    reg     [CFG_MEM_IF_CS_WIDTH-1:0]        int_cmd_gen_chipsel;
                    reg     [CFG_MEM_IF_BA_WIDTH-1:0]        int_cmd_gen_bank;
                    reg     [CFG_MEM_IF_ROW_WIDTH-1:0]       int_cmd_gen_row;
                    reg     [CFG_MEM_IF_COL_WIDTH-1:0]       int_cmd_gen_col;
                    reg                                      int_cmd_gen_write;
                    reg                                      int_cmd_gen_read;
                    reg                                      int_cmd_gen_multicast;
                    reg     [CFG_INT_SIZE_WIDTH-1:0]         int_cmd_gen_size;
                    reg     [CFG_LOCAL_ID_WIDTH-1:0]         int_cmd_gen_localid;
                    reg     [CFG_DATA_ID_WIDTH-1:0]          int_cmd_gen_dataid;
                    reg                                      int_cmd_gen_priority;
                    reg                                      int_cmd_gen_rmw_correct;
                    reg                                      int_cmd_gen_rmw_partial;
                    reg                                      int_cmd_gen_autopch;
                    reg                                      int_cmd_gen_complete;
                    
                    // TBP address and command comparison logic
                    always @ (*)
                        begin
                            for(j=0; j<CFG_CTL_TBP_NUM; j=j+1)
                                begin
                                    // Chipselect address
                                    if (int_cmd_gen_chipsel == chipsel[j])
                                        begin
                                            int_same_chipsel_addr[j] = 1'b1;
                                        end
                                    else
                                        begin
                                            int_same_chipsel_addr[j] = 1'b0;
                                        end
                                    
                                    // Bank addr
                                    if (int_cmd_gen_bank == bank[j])
                                        begin
                                            int_same_bank_addr[j] = 1'b1;
                                        end
                                    else
                                        begin
                                            int_same_bank_addr[j] = 1'b0;
                                        end
                                    
                                    // Row addr
                                    if (int_cmd_gen_row[(1 * (CFG_MEM_IF_ROW_WIDTH / 4)) - 1 : (0 * (CFG_MEM_IF_ROW_WIDTH / 4))] == row[j][(1 * (CFG_MEM_IF_ROW_WIDTH / 4)) - 1 : (0 * (CFG_MEM_IF_ROW_WIDTH / 4))])
                                        begin
                                            int_same_row_addr_0[j] = 1'b1;
                                        end
                                    else
                                        begin
                                            int_same_row_addr_0[j] = 1'b0;
                                        end
                                    
                                    if (int_cmd_gen_row[(2 * (CFG_MEM_IF_ROW_WIDTH / 4)) - 1 : (1 * (CFG_MEM_IF_ROW_WIDTH / 4))] == row[j][(2 * (CFG_MEM_IF_ROW_WIDTH / 4)) - 1 : (1 * (CFG_MEM_IF_ROW_WIDTH / 4))])
                                        begin
                                            int_same_row_addr_1[j] = 1'b1;
                                        end
                                    else
                                        begin
                                            int_same_row_addr_1[j] = 1'b0;
                                        end
                                    
                                    if (int_cmd_gen_row[(3 * (CFG_MEM_IF_ROW_WIDTH / 4)) - 1 : (2 * (CFG_MEM_IF_ROW_WIDTH / 4))] == row[j][(3 * (CFG_MEM_IF_ROW_WIDTH / 4)) - 1 : (2 * (CFG_MEM_IF_ROW_WIDTH / 4))])
                                        begin
                                            int_same_row_addr_2[j] = 1'b1;
                                        end
                                    else
                                        begin
                                            int_same_row_addr_2[j] = 1'b0;
                                        end
                                    
                                    if (int_cmd_gen_row[CFG_MEM_IF_ROW_WIDTH             - 1 : (3 * (CFG_MEM_IF_ROW_WIDTH / 4))] == row[j][CFG_MEM_IF_ROW_WIDTH             - 1 : (3 * (CFG_MEM_IF_ROW_WIDTH / 4))])
                                        begin
                                            int_same_row_addr_3[j] = 1'b1;
                                        end
                                    else
                                        begin
                                            int_same_row_addr_3[j] = 1'b0;
                                        end
                                    
                                    // Col addr
                                    if (int_cmd_gen_col == col[j])
                                        begin
                                            int_same_col_addr[j] = 1'b1;
                                        end
                                    else
                                        begin
                                            int_same_col_addr[j] = 1'b0;
                                        end
                                    
                                    // Read command
                                    if (int_cmd_gen_read == read[j])
                                        begin
                                            int_same_read_cmd[j] = 1'b1;
                                        end
                                    else
                                        begin
                                            int_same_read_cmd[j] = 1'b0;
                                        end
                                    
                                    // Write command
                                    if (int_cmd_gen_write == write[j])
                                        begin
                                            int_same_write_cmd[j] = 1'b1;
                                        end
                                    else
                                        begin
                                            int_same_write_cmd[j] = 1'b0;
                                        end
                                end
                        end
                    
                    // Shadow TBP address and command comparison logic
                    always @ (*)
                        begin
                            for(j=0; j<CFG_CTL_SHADOW_TBP_NUM; j=j+1)
                                begin
                                    if (int_queue_full)
                                        begin
                                            // Chipselect address
                                            if (int_cmd_gen_chipsel == shadow_chipsel[j])
                                                begin
                                                    int_same_shadow_chipsel_addr[j] = 1'b1;
                                                end
                                            else
                                                begin
                                                    int_same_shadow_chipsel_addr[j] = 1'b0;
                                                end
                                            
                                            // Bank addr
                                            if (int_cmd_gen_bank == shadow_bank[j])
                                                begin
                                                    int_same_shadow_bank_addr[j] = 1'b1;
                                                end
                                            else
                                                begin
                                                    int_same_shadow_bank_addr[j] = 1'b0;
                                                end
                                            
                                            // Row addr
                                            if (int_cmd_gen_row == shadow_row[j])
                                                begin
                                                    int_same_shadow_row_addr[j] = 1'b1;
                                                end
                                            else
                                                begin
                                                    int_same_shadow_row_addr[j] = 1'b0;
                                                end
                                        end
                                    else
                                        begin
                                            // Chipselect address
                                            if (muxed_cs_addr == shadow_chipsel[j])
                                                begin
                                                    int_same_shadow_chipsel_addr[j] = 1'b1;
                                                end
                                            else
                                                begin
                                                    int_same_shadow_chipsel_addr[j] = 1'b0;
                                                end
                                            
                                            // Bank addr
                                            if (muxed_bank_addr == shadow_bank[j])
                                                begin
                                                    int_same_shadow_bank_addr[j] = 1'b1;
                                                end
                                            else
                                                begin
                                                    int_same_shadow_bank_addr[j] = 1'b0;
                                                end
                                            
                                            // Row addr
                                            if (muxed_row_addr == shadow_row[j])
                                                begin
                                                    int_same_shadow_row_addr[j] = 1'b1;
                                                end
                                            else
                                                begin
                                                    int_same_shadow_row_addr[j] = 1'b0;
                                                end
                                        end
                                end
                        end
                    
                    assign int_queue_full = tbp_full | ((cmd_gen_read & ~rdatap_free_id_valid) | (cmd_gen_write & ~wdatap_free_id_valid));
                    
                    always @ (*)
                        begin
                            int_register_valid       = one;
                            int_cmd_gen_read         = muxed_read;
                            int_cmd_gen_write        = muxed_write;
                            int_cmd_gen_multicast    = muxed_multicast;
                            int_cmd_gen_autopch      = muxed_autopch;
                            int_cmd_gen_priority     = muxed_priority;
                            int_cmd_gen_complete     = muxed_complete;
                            int_cmd_gen_rmw_correct  = muxed_correct;
                            int_cmd_gen_rmw_partial  = muxed_partial;
                            int_cmd_gen_dataid       = muxed_dataid;
                            int_cmd_gen_localid      = muxed_localid;
                            int_cmd_gen_size         = muxed_size;
                            int_cmd_gen_chipsel      = muxed_cs_addr;
                            int_cmd_gen_row          = muxed_row_addr;
                            int_cmd_gen_bank         = muxed_bank_addr;
                            int_cmd_gen_col          = muxed_col_addr;
                        end
                    
                    always @ (*)
                        begin
                            same_chipsel_addr        = int_same_chipsel_addr;
                            same_bank_addr           = int_same_bank_addr;
                            same_row_addr            = int_same_row_addr_0 & int_same_row_addr_1 & int_same_row_addr_2 & int_same_row_addr_3;
                            same_col_addr            = int_same_col_addr;
                            same_read_cmd            = int_same_read_cmd;
                            same_write_cmd           = int_same_write_cmd;
                            
                            same_shadow_chipsel_addr = int_same_shadow_chipsel_addr;
                            same_shadow_bank_addr    = int_same_shadow_bank_addr;
                            same_shadow_row_addr     = int_same_shadow_row_addr;
                        end
                    
                    assign queue_full                       = int_queue_full;
                    assign cmd_gen_load                     = (cmd_gen_read & rdatap_free_id_valid) | (cmd_gen_write & wdatap_free_id_valid);
                    assign cmd_gen_waiting_to_load          = waiting_to_load;
                    assign cmd_gen_read                     = int_cmd_gen_read;
                    assign cmd_gen_write                    = int_cmd_gen_write;
                    assign cmd_gen_multicast                = int_cmd_gen_multicast;
                    assign cmd_gen_autopch                  = int_cmd_gen_autopch;
                    assign cmd_gen_priority                 = int_cmd_gen_priority;
                    assign cmd_gen_complete                 = int_cmd_gen_complete;
                    assign cmd_gen_rmw_correct              = int_cmd_gen_rmw_correct;
                    assign cmd_gen_rmw_partial              = int_cmd_gen_rmw_partial;
                    assign cmd_gen_dataid                   = int_cmd_gen_dataid;
                    assign cmd_gen_localid                  = int_cmd_gen_localid;
                    assign cmd_gen_size                     = int_cmd_gen_size;
                    assign cmd_gen_chipsel                  = int_cmd_gen_chipsel;
                    assign cmd_gen_row                      = int_cmd_gen_row;
                    assign cmd_gen_bank                     = int_cmd_gen_bank;
                    assign cmd_gen_col                      = int_cmd_gen_col;
                    assign cmd_gen_same_chipsel_addr        = same_chipsel_addr;
                    assign cmd_gen_same_bank_addr           = same_bank_addr;
                    assign cmd_gen_same_row_addr            = same_row_addr;
                    assign cmd_gen_same_col_addr            = same_col_addr;
                    assign cmd_gen_same_read_cmd            = same_read_cmd;
                    assign cmd_gen_same_write_cmd           = same_write_cmd;
                    assign cmd_gen_same_shadow_chipsel_addr = same_shadow_chipsel_addr;
                    assign cmd_gen_same_shadow_bank_addr    = same_shadow_bank_addr;
                    assign cmd_gen_same_shadow_row_addr     = same_shadow_row_addr;
                end
        endgenerate
    //======================================================================================
    //
    //  [END] Output Logic
    //
    //======================================================================================
    
    //----------------------------------------------------------------------------------------------------------------

    function integer log2;
        input [31:0] value;
        integer i;
        begin
            log2 = 0;
            
            for(i = 0; 2**i < value; i = i + 1)
                log2 = i + 1;
        end
    endfunction
    
endmodule
