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



//altera message_off 10230 10762 10036

`include "alt_mem_ddrx_define.iv"

`timescale 1 ps / 1 ps
module alt_mem_ddrx_sideband
    # (parameter
    
        // parameters
        CFG_PORT_WIDTH_TYPE                     =   3,
        CFG_DWIDTH_RATIO                        =   2, //2-FR,4-HR,8-QR
        CFG_REG_GRANT                           =   1,
        CFG_CTL_TBP_NUM                         =   4,
        CFG_MEM_IF_CS_WIDTH                     =   1,
        CFG_MEM_IF_CHIP                         =   1, // one hot
        CFG_MEM_IF_BA_WIDTH                     =   3,
        CFG_PORT_WIDTH_TCL                      =   4,
        CFG_PORT_WIDTH_CS_ADDR_WIDTH            =   2,
        CFG_MEM_IF_CLK_PAIR_COUNT               =   2,
        CFG_RANK_TIMER_OUTPUT_REG               =   0,
        T_PARAM_ARF_TO_VALID_WIDTH              =   10,
        T_PARAM_ARF_PERIOD_WIDTH                =   13,
        T_PARAM_PCH_ALL_TO_VALID_WIDTH          =   10,
        T_PARAM_SRF_TO_VALID_WIDTH              =   10,
        T_PARAM_SRF_TO_ZQ_CAL_WIDTH             =   10,
        T_PARAM_PDN_TO_VALID_WIDTH              =   6,
        T_PARAM_PDN_PERIOD_WIDTH                = 16,
        T_PARAM_POWER_SAVING_EXIT_WIDTH         = 6,
        T_PARAM_MEM_CLK_ENTRY_CYCLES_WIDTH      = 4,
        BANK_TIMER_COUNTER_OFFSET               = 2  //used to be 4
    )
    (
    
        ctl_clk,
        ctl_reset_n,
        
        // local interface
        rfsh_req,
        rfsh_chip,
        rfsh_ack,
        self_rfsh_req,
        self_rfsh_chip,
        self_rfsh_ack,
        deep_powerdn_req,
        deep_powerdn_chip,
        deep_powerdn_ack,
        power_down_ack,
        
        // sideband output
        stall_row_arbiter,
        stall_col_arbiter,
        stall_chip,
        sb_do_precharge_all,
        sb_do_refresh,
        sb_do_self_refresh,
        sb_do_power_down,
        sb_do_deep_pdown,
        sb_do_zq_cal,
        sb_tbp_precharge_all,
        
        // PHY interface
        ctl_mem_clk_disable,
        ctl_cal_req,
        ctl_init_req,
        ctl_cal_success,
        
        // tbp & cmd gen
        cmd_gen_chipsel,
        tbp_chipsel,
        tbp_load,
        
        // timing
        t_param_arf_to_valid,
        t_param_arf_period,
        t_param_pch_all_to_valid,
        t_param_srf_to_valid,
        t_param_srf_to_zq_cal,
        t_param_pdn_to_valid,
        t_param_pdn_period,
        t_param_power_saving_exit,
        t_param_mem_clk_entry_cycles,
        
        // block status
        tbp_empty,
        tbp_bank_closed,
        tbp_timer_ready,
        row_grant,
        col_grant,
        
        // dqs tracking
        afi_ctl_refresh_done,
        afi_seq_busy,
        afi_ctl_long_idle,
        
        // config ports
        cfg_cs_addr_width,
        cfg_enable_dqs_tracking,
        cfg_user_rfsh,
        cfg_type,
        cfg_tcl,
        cfg_regdimm_enable,

        // ZQ Calibration
        zqcal_req,
        
        // to refresh controller
        sideband_in_refresh
    );
    
    // states for "sideband_state" state machine
    localparam IDLE_S1      = 3'b000;
    localparam ARF          = 3'b001;
    localparam PDN          = 3'b010;
    localparam SRF          = 3'b100;
	
    // states for "disable_clk_state" state machine
    localparam IDLE_S2      = 2'b00;
    localparam DISABLECLK1  = 2'b01;
    localparam DISABLECLK2  = 2'b10;
	
    // states for "state" state machine
    localparam IDLE_S3      = 9'b000000000;
    localparam INIT         = 9'b000000001;
    localparam PCHALL       = 9'b000000010;
    localparam REFRESH      = 9'b000000100;
    localparam PDOWN        = 9'b000001000;
    localparam SELFRFSH     = 9'b000010000;
    localparam DEEPPDN      = 9'b000100000;
    localparam INITREQ      = 9'b001000000;
    localparam ZQCAL        = 9'b010000000;
    localparam DQSTRK       = 9'b100000000;
    
    localparam POWER_SAVING_COUNTER_WIDTH      = T_PARAM_SRF_TO_VALID_WIDTH;
    localparam POWER_SAVING_EXIT_COUNTER_WIDTH = T_PARAM_POWER_SAVING_EXIT_WIDTH;
    localparam DISABLE_CLK_COUNTER_WIDTH       = T_PARAM_MEM_CLK_ENTRY_CYCLES_WIDTH;
    localparam ARF_COUNTER_WIDTH               = T_PARAM_ARF_PERIOD_WIDTH;
    localparam PDN_COUNTER_WIDTH               = T_PARAM_PDN_PERIOD_WIDTH;
    
    localparam integer CFG_MEM_IF_BA_WIDTH_SQRD = 2 ** CFG_MEM_IF_BA_WIDTH;
    localparam integer CFG_PORT_WIDTH_TCL_SQRD  = 2 ** CFG_PORT_WIDTH_TCL;
    localparam SAMPLE_EVERY_N_RFSH = 0; //Valid values are 0-7
    
    input                                               ctl_clk;
    input                                               ctl_reset_n;
    
    input                                               rfsh_req;
    input   [CFG_MEM_IF_CHIP-1:0]                       rfsh_chip;
    output                                              rfsh_ack;
    input                                               self_rfsh_req;
    input   [CFG_MEM_IF_CHIP-1:0]                       self_rfsh_chip;
    output                                              self_rfsh_ack;
    input                                               deep_powerdn_req;
    input   [CFG_MEM_IF_CHIP-1:0]                       deep_powerdn_chip;
    output                                              deep_powerdn_ack;
    output                                              power_down_ack;
    
    output                                              stall_row_arbiter;
    output                                              stall_col_arbiter;
    output  [CFG_MEM_IF_CHIP-1:0]                       stall_chip;
    output  [CFG_MEM_IF_CHIP-1:0]                       sb_do_precharge_all;
    output  [CFG_MEM_IF_CHIP-1:0]                       sb_do_refresh;
    output  [CFG_MEM_IF_CHIP-1:0]                       sb_do_self_refresh;
    output  [CFG_MEM_IF_CHIP-1:0]                       sb_do_power_down;
    output  [CFG_MEM_IF_CHIP-1:0]                       sb_do_deep_pdown;
    output  [CFG_MEM_IF_CHIP-1:0]                       sb_do_zq_cal;
    output  [CFG_CTL_TBP_NUM-1:0]                       sb_tbp_precharge_all;
    
    output  [CFG_MEM_IF_CLK_PAIR_COUNT-1:0]             ctl_mem_clk_disable;
    output                                              ctl_cal_req;
    output                                              ctl_init_req;
    input                                               ctl_cal_success;
    
    input   [CFG_MEM_IF_CS_WIDTH-1:0]                   cmd_gen_chipsel;
    input   [(CFG_CTL_TBP_NUM*CFG_MEM_IF_CS_WIDTH)-1:0] tbp_chipsel;
    input   [CFG_CTL_TBP_NUM-1:0]                       tbp_load;
    
    input   [T_PARAM_ARF_TO_VALID_WIDTH-1:0]            t_param_arf_to_valid;
    input   [T_PARAM_ARF_PERIOD_WIDTH-1:0]              t_param_arf_period;
    input   [T_PARAM_PCH_ALL_TO_VALID_WIDTH-1:0]        t_param_pch_all_to_valid;
    input   [T_PARAM_SRF_TO_VALID_WIDTH-1:0]            t_param_srf_to_valid;
    input   [T_PARAM_SRF_TO_ZQ_CAL_WIDTH-1:0]           t_param_srf_to_zq_cal;
    input   [T_PARAM_PDN_TO_VALID_WIDTH-1:0]            t_param_pdn_to_valid;
    input   [T_PARAM_PDN_PERIOD_WIDTH-1:0]              t_param_pdn_period;
    input   [T_PARAM_POWER_SAVING_EXIT_WIDTH-1:0]       t_param_power_saving_exit;
    input   [T_PARAM_MEM_CLK_ENTRY_CYCLES_WIDTH-1:0]    t_param_mem_clk_entry_cycles;
    
    input                                               tbp_empty;
    input   [CFG_MEM_IF_CHIP-1:0]                       tbp_bank_closed;
    input   [CFG_MEM_IF_CHIP-1:0]                       tbp_timer_ready;
    input                                               row_grant;
    input                                               col_grant;
    
    output  [CFG_MEM_IF_CHIP-1:0]                       afi_ctl_refresh_done;
    input   [CFG_MEM_IF_CHIP-1:0]                       afi_seq_busy;
    output  [CFG_MEM_IF_CHIP-1:0]                       afi_ctl_long_idle;
    
    input   [CFG_PORT_WIDTH_CS_ADDR_WIDTH-1:0]          cfg_cs_addr_width;
    input                                               cfg_enable_dqs_tracking;
    input                                               cfg_user_rfsh;
    input   [CFG_PORT_WIDTH_TYPE-1:0]                   cfg_type;
    input   [CFG_PORT_WIDTH_TCL-1:0]                    cfg_tcl;
    input                                               cfg_regdimm_enable;
    
    input                                               zqcal_req;

    output  [CFG_MEM_IF_CHIP-1:0]                       sideband_in_refresh;

    // end of port declaration
    
    wire                                    self_rfsh_ack;
    wire                                    deep_powerdn_ack;
    wire                                    power_down_ack;
    
    wire    [CFG_MEM_IF_CLK_PAIR_COUNT-1:0] ctl_mem_clk_disable;
    
    wire                                    ctl_cal_req;
    wire                                    ctl_init_req;
    
    reg     [CFG_MEM_IF_CHIP-1:0]           sb_do_precharge_all;
    reg     [CFG_MEM_IF_CHIP-1:0]           sb_do_refresh;
    reg     [CFG_MEM_IF_CHIP-1:0]           sb_do_self_refresh;
    reg     [CFG_MEM_IF_CHIP-1:0]           sb_do_power_down;
    reg     [CFG_MEM_IF_CHIP-1:0]           sb_do_deep_pdown;
    reg     [CFG_MEM_IF_CHIP-1:0]           sb_do_zq_cal;
    reg     [CFG_CTL_TBP_NUM-1:0]           sb_tbp_precharge_all;
    
    reg     [CFG_MEM_IF_CHIP-1:0]           do_refresh;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_power_down;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_deep_pdown;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_self_rfsh;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_self_rfsh_r;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_precharge_all;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_zqcal;
    reg     [CFG_MEM_IF_CHIP-1:0]           stall_chip;
    reg     [CFG_MEM_IF_CHIP-1:0]           int_stall_chip;
    reg     [CFG_MEM_IF_CHIP-1:0]           int_stall_chip_combi;
    reg     [CFG_MEM_IF_CHIP-1:0]           stall_arbiter;
    reg     [CFG_MEM_IF_CHIP-1:0]           afi_ctl_refresh_done;
    wire    [CFG_MEM_IF_CHIP-1:0]           afi_ctl_long_idle;
    reg     [CFG_MEM_IF_CHIP-1:0]           dqstrk_exit;
    reg     [CFG_MEM_IF_CHIP-1:0]           doing_zqcal;
    reg     [CFG_MEM_IF_CHIP-1:0]           init_req;
    reg     [CFG_MEM_IF_CHIP-1:0]           disable_clk;
    reg     [CFG_MEM_IF_CHIP-1:0]           int_init_req;
    reg     [CFG_MEM_IF_CHIP-1:0]           int_disable_clk;
    
    reg                                     refresh_req;
    
    reg     [CFG_MEM_IF_CHIP-1:0]           refresh_chip_req;
    reg     [CFG_MEM_IF_CHIP-1:0]           self_refresh_chip_req;
    reg                                     self_rfsh_req_r;
    reg     [CFG_MEM_IF_CHIP-1:0]           deep_pdown_chip_req;
    reg     [CFG_MEM_IF_CHIP-1:0]           power_down_chip_req;
    wire    [CFG_MEM_IF_CHIP-1:0]           power_down_chip_req_combi;
    
    wire    [CFG_MEM_IF_CHIP-1:0]           all_banks_closed;
    wire    [CFG_MEM_IF_CHIP-1:0]           tcom_not_running;
    reg     [CFG_PORT_WIDTH_TCL_SQRD-1:0]   tcom_not_running_pipe [CFG_MEM_IF_CHIP-1:0];
    reg     [CFG_MEM_IF_CHIP-1:0]           can_refresh;
    reg     [CFG_MEM_IF_CHIP-1:0]           can_self_rfsh;
    reg     [CFG_MEM_IF_CHIP-1:0]           can_deep_pdown;
    reg     [CFG_MEM_IF_CHIP-1:0]           can_power_down;
    reg     [CFG_MEM_IF_CHIP-1:0]           can_exit_power_saving_mode;
    reg     [CFG_MEM_IF_CHIP-1:0]           cs_refresh_req;
    wire    grant;
    wire    [CFG_MEM_IF_CHIP-1:0]           cs_zq_cal_req;
    wire    [CFG_MEM_IF_CHIP-1:0]           power_saving_enter_ready;
    wire    [CFG_MEM_IF_CHIP-1:0]           power_saving_exit_ready;
    reg     [PDN_COUNTER_WIDTH  - 1 : 0]    power_down_cnt;
    reg                                     no_command_r1;
    reg     [CFG_MEM_IF_CHIP-1:0]           afi_seq_busy_r; // synchronizer
    reg     [CFG_MEM_IF_CHIP-1:0]           afi_seq_busy_r2; // synchronizer
    
    //new! to avoid contention
    reg     [CFG_MEM_IF_CHIP-1:0]           do_refresh_req;
    reg                                     refresh_req_ack;
    reg                                     dummy_do_refresh;
    reg                                     dummy_do_refresh_r;
    reg                                     do_refresh_r;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_self_rfsh_req;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_self_rfsh_req_r1;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_self_rfsh_req_r2;
    reg                                     self_rfsh_req_ack;
    reg                                     dummy_do_self_rfsh;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_zqcal_req;
    reg                                     zqcal_req_ack;
    reg                                     dummy_do_zqcal;
    reg     [CFG_MEM_IF_CHIP-1:0]           do_pch_all_req;
    reg                                     pch_all_req_ack;
    reg                                     dummy_do_pch_all;
    reg                                     do_refresh_to_all_chip;
    reg                                     do_refresh_to_all_chip_r;
    reg                                     do_self_refresh_to_all_chip;
    reg                                     do_self_refresh_to_all_chip_r;
    reg                                     do_zqcal_to_all_chip;
    reg                                     do_zqcal_to_all_chip_r;
    reg     [CFG_MEM_IF_CHIP-1:0]           sideband_in_refresh;
    reg     [2:0]                           trk_rfsh_cntr [CFG_MEM_IF_CHIP-1:0];
    
    integer i;
    
    assign ctl_mem_clk_disable = {CFG_MEM_IF_CLK_PAIR_COUNT{&int_disable_clk}};
    assign ctl_cal_req         = &int_init_req;
    assign ctl_init_req        = 1'b0;
    assign afi_ctl_long_idle   = {CFG_MEM_IF_CHIP{1'b0}};
    
    //generate *_chip_ok signals by checking can_*[chip], only when for_chip[chip] is 1   
    generate
        genvar chip;
        for (chip = 0; chip < CFG_MEM_IF_CHIP; chip = chip + 1)
        begin : gen_chip_ok
            // check can_* only for chips that we'd like to precharge_all to, ^~ is XNOR
            assign tcom_not_running[chip] = tbp_timer_ready[chip];
            assign all_banks_closed[chip] = tbp_bank_closed[chip];
            
            always @(posedge ctl_clk, negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    tcom_not_running_pipe[chip] <= 0;
                end
                else
                begin
                    if (!tcom_not_running[chip])
                    begin
                        tcom_not_running_pipe[chip] <= 0;
                    end
                    else
                    begin
                        tcom_not_running_pipe[chip] <= {tcom_not_running_pipe[chip][CFG_PORT_WIDTH_TCL_SQRD -2 :0],tcom_not_running[chip]};
                    end
                end
            end
            
            // Generate per chip init_req and disable_clk
            // to be used in MMR case where they actually use smaller CS than the actual design
            // example: 2 CS design, but user only use 1 CS in MMR mode
            always @ (*)
            begin
                if (chip < (2 ** cfg_cs_addr_width))
                begin
                    int_init_req   [chip] = init_req   [chip];
                    int_disable_clk[chip] = disable_clk[chip];
                end
                else
                begin
                    int_init_req   [chip] = 1'b1;
                    int_disable_clk[chip] = 1'b1;
                end
            end
        end
    endgenerate
    
    assign  rfsh_ack         = (!(cfg_regdimm_enable && cfg_type == `MMR_TYPE_DDR3 && CFG_MEM_IF_CHIP != 1)) ? |do_refresh : ((|do_refresh | do_refresh_r) & refresh_req_ack);
    assign  self_rfsh_ack    = |do_self_rfsh;
    assign  deep_powerdn_ack = |do_deep_pdown;
    assign  power_down_ack   = |do_power_down;
    
    // Register sideband signals when CFG_REG_GRANT is '1'
    // to prevent sideband request going out on the same cycle as tbp request
    generate
    begin
        genvar j;
        if (CFG_REG_GRANT == 1)
        begin
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    sb_do_precharge_all <= 0;
                    sb_do_refresh       <= 0;
                    sb_do_self_refresh  <= 0;
                    sb_do_power_down    <= 0;
                    sb_do_deep_pdown    <= 0;
                    sb_do_zq_cal        <= 0;
                end
                else
                begin
                    sb_do_precharge_all <= do_precharge_all;
                    sb_do_refresh       <= do_refresh;
                    sb_do_self_refresh  <= do_self_rfsh;
                    sb_do_power_down    <= do_power_down;
                    sb_do_deep_pdown    <= do_deep_pdown;
                    sb_do_zq_cal        <= do_zqcal;
                end
            end
            
            for (j = 0;j < CFG_CTL_TBP_NUM;j = j + 1)
            begin : tbp_loop_1
                always @ (posedge ctl_clk or negedge ctl_reset_n)
                begin
                    if (!ctl_reset_n)
                    begin
                        sb_tbp_precharge_all [j] <= 1'b0;
                    end
                    else
                    begin
                        if (tbp_load[j])
                        begin
                            sb_tbp_precharge_all [j] <= do_precharge_all [cmd_gen_chipsel];
                        end
                        else
                        begin
                            sb_tbp_precharge_all [j] <= do_precharge_all [tbp_chipsel [(j + 1) * CFG_MEM_IF_CS_WIDTH - 1 : j * CFG_MEM_IF_CS_WIDTH]];
                        end
                    end
                end
            end
        end
        else
        begin
            always @ (*)
            begin
                sb_do_precharge_all = do_precharge_all;
                sb_do_refresh       = do_refresh;
                sb_do_self_refresh  = do_self_rfsh;
                sb_do_power_down    = do_power_down;
                sb_do_deep_pdown    = do_deep_pdown;
                sb_do_zq_cal        = do_zqcal;
            end
            
            for (j = 0;j < CFG_CTL_TBP_NUM;j = j + 1)
            begin : tbp_loop_2
                always @ (*)
                begin
                    sb_tbp_precharge_all [j] = do_precharge_all [tbp_chipsel [(j + 1) * CFG_MEM_IF_CS_WIDTH - 1 : j * CFG_MEM_IF_CS_WIDTH]];
                end
            end
        end
    end
    endgenerate
    
    always @(posedge ctl_clk, negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            refresh_req_ack    <= 0;
            zqcal_req_ack      <= 0;
            pch_all_req_ack    <= 0;
            self_rfsh_req_ack  <= 0;
            dummy_do_refresh_r <= 0;
            do_refresh_r       <= 0;
        end
        else
        begin
            refresh_req_ack    <= dummy_do_refresh;
            zqcal_req_ack      <= dummy_do_zqcal;
            pch_all_req_ack    <= dummy_do_pch_all;
            self_rfsh_req_ack  <= dummy_do_self_rfsh;
            dummy_do_refresh_r <= dummy_do_refresh;
            
            if (dummy_do_refresh && !dummy_do_refresh_r)
            begin
                do_refresh_r <= |do_refresh;
            end
            else
            begin
                do_refresh_r <= 0;
            end
        end
    end
    
    always @(*)
    begin
        i = 0;
        dummy_do_refresh = 0;
        dummy_do_pch_all = 0;
        dummy_do_zqcal   = 0;
        
        if (|do_pch_all_req)
        begin
            do_refresh = 0;
            do_zqcal   = 0;
            
            if (!(cfg_regdimm_enable && cfg_type == `MMR_TYPE_DDR3 && CFG_MEM_IF_CHIP != 1))
            begin
                do_precharge_all = do_pch_all_req;
            end
            else
            begin
                for (i = 0;i < CFG_MEM_IF_CHIP;i = i + 1)
                begin
                    if (i%2 == 0 && !pch_all_req_ack) // case:139868 fixing case where pch_all goes to all cs on the same cycle, which is illegal in RDIMM mode
                    begin
                        do_precharge_all [i] = do_pch_all_req[i];
                        dummy_do_pch_all     = |do_pch_all_req;
                    end
                    else if (i%2 == 1 && pch_all_req_ack)
                    begin
                        do_precharge_all [i] = do_pch_all_req[i];
                    end
                    else
                    begin
                        do_precharge_all [i] = 0;
                    end
                end
            end
        end
        else if (|do_refresh_req)
        begin
            if (!(cfg_regdimm_enable && cfg_type == `MMR_TYPE_DDR3 && CFG_MEM_IF_CHIP != 1)) // if not (regdimm and DDR3), normal refresh
            begin
                do_refresh = do_refresh_req;
            end
            else
            begin
                for (i = 0;i < CFG_MEM_IF_CHIP;i = i + 1)
                begin
                    if (i%2 == 0)
                    begin
                        do_refresh [i]   = do_refresh_req [i];

                        if (&refresh_chip_req) // refresh to all chips in REGDIMM
                        begin
                            dummy_do_refresh = &do_refresh_req;
                        end
                        else
                        begin
                            dummy_do_refresh = |do_refresh_req;
                        end
                    end
                    else if (i%2 == 1 && refresh_req_ack)
                    begin
                        do_refresh [i] = do_refresh_req [i];
                    end
                    else
                    begin
                        do_refresh [i] = 0;
                    end
                end
            end
            
            do_precharge_all = 0;
            do_zqcal         = 0;
        end
        else if (|do_zqcal_req)
        begin
            do_refresh       = 0;
            do_precharge_all = 0;
            
            if (!(cfg_regdimm_enable && cfg_type == `MMR_TYPE_DDR3 && CFG_MEM_IF_CHIP != 1))
            begin
                do_zqcal = do_zqcal_req;
            end
            else
            begin
                for (i = 0;i < CFG_MEM_IF_CHIP;i = i + 1)
                begin
                    if (i%2 == 0)
                    begin
                        do_zqcal [i]   = do_zqcal_req[i];
                        dummy_do_zqcal = |do_zqcal_req;
                    end
                    else if (i%2 == 1 && zqcal_req_ack)
                    begin
                        do_zqcal [i] = do_zqcal_req[i];
                    end
                    else
                    begin
                        do_zqcal [i] = 0;
                    end
                end
            end
        end
        else
        begin
            do_refresh       = 0;
            dummy_do_refresh = 0;
            do_precharge_all = 0;
            dummy_do_pch_all = 0;
            do_zqcal         = 0;
            dummy_do_zqcal   = 0;
        end
    end
    
    always @(*)
    begin
        i                  = 0;
        dummy_do_self_rfsh = 1'b0;
        
        if (|do_refresh_req || |do_precharge_all || |do_zqcal_req)
        begin
            if (|do_self_rfsh_r)
            begin
                do_self_rfsh       = do_self_rfsh_req;
                dummy_do_self_rfsh = 1'b1;
            end
            else
            begin
                do_self_rfsh = 0;
            end
        end
        else
        begin
            if (!(cfg_regdimm_enable && cfg_type == `MMR_TYPE_DDR3 && CFG_MEM_IF_CHIP != 1))
            begin
                do_self_rfsh = do_self_rfsh_req;
            end
            else
            begin
                for (i = 0;i < CFG_MEM_IF_CHIP;i = i + 1)
                begin
                    if (i%2 == 0)
                    begin
                        do_self_rfsh [i]   = do_self_rfsh_req[i];
                        dummy_do_self_rfsh = |do_self_rfsh_req;
                    end
                    else if (i%2 == 1 && self_rfsh_req_ack)
                    begin
                        do_self_rfsh [i] = do_self_rfsh_req[i];
                    end
                    else
                    begin
                        do_self_rfsh [i] = 0;
                    end
                end
            end
        end
    end
    
    always @ (*)
    begin
        do_refresh_to_all_chip      = &do_refresh_req;
        do_self_refresh_to_all_chip = &do_self_rfsh_req;
        do_zqcal_to_all_chip        = &do_zqcal_req;
    end
    
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            do_self_rfsh_req_r1           <= 0;
            do_self_rfsh_req_r2           <= 0;
            
            do_refresh_to_all_chip_r      <= 1'b0;
            do_self_refresh_to_all_chip_r <= 1'b0;
            do_zqcal_to_all_chip_r        <= 1'b0;
        end
        else
        begin
            do_self_rfsh_req_r1           <= do_self_rfsh_req;
            do_self_rfsh_req_r2           <= do_self_rfsh_req_r1;
            
            do_refresh_to_all_chip_r      <= do_refresh_to_all_chip;
            do_self_refresh_to_all_chip_r <= do_self_refresh_to_all_chip;
            do_zqcal_to_all_chip_r        <= do_zqcal_to_all_chip;
        end
    end
    
    assign stall_row_arbiter = |stall_arbiter;
    assign stall_col_arbiter = |stall_arbiter;
    
    assign grant             = (CFG_REG_GRANT == 1) ? (row_grant | col_grant) : 1'b0;
    
    //register self_rfsh_req and deep_powerdn_req
    always @(posedge ctl_clk, negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            self_refresh_chip_req <= 0;
            deep_pdown_chip_req   <= 0;
            self_rfsh_req_r       <= 0;
            do_self_rfsh_r        <= 0;
        end
        else
        begin
            if (self_rfsh_req)
            begin
                self_refresh_chip_req <= self_rfsh_chip;
            end
            else
            begin
                self_refresh_chip_req <= 0;
            end
            
            self_rfsh_req_r <= self_rfsh_req & |self_rfsh_chip;
            do_self_rfsh_r  <= do_self_rfsh;
            
            if (deep_powerdn_req)
            begin
                deep_pdown_chip_req <= deep_powerdn_chip;
            end
            else
            begin
                deep_pdown_chip_req <= 0;
            end
        end
    end
    
    //combi user refresh
    always @(*)
    begin
        if (cfg_user_rfsh)
        begin
            if (rfsh_req)
            begin
                refresh_req      = 1'b1;
                refresh_chip_req = rfsh_chip;
            end
            else
            begin
                refresh_req      = 1'b0;
                refresh_chip_req = 0;
            end
        end
        else
        begin
            refresh_req      = |cs_refresh_req;
            refresh_chip_req =  cs_refresh_req;
        end
    end
    
    always @(posedge ctl_clk, negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            afi_seq_busy_r  <= 0;
            afi_seq_busy_r2 <= 0;
        end
        else
        begin
            afi_seq_busy_r  <= afi_seq_busy;
            afi_seq_busy_r2 <= afi_seq_busy_r;
        end
    end
    
    // cans
    generate
        genvar w_cs;
        for (w_cs = 0;w_cs < CFG_MEM_IF_CHIP;w_cs = w_cs + 1)
        begin : can_signal_per_chip
            // Can refresh signal for each rank
            always @ (*)
            begin
                can_refresh    [w_cs] = power_saving_enter_ready [w_cs] & all_banks_closed [w_cs] & tcom_not_running [w_cs]                                         & ~grant;
            end
            
            // Can self refresh signal for each rank
            always @ (*)
            begin
                can_self_rfsh  [w_cs] = power_saving_enter_ready [w_cs] & all_banks_closed [w_cs] & tcom_not_running [w_cs] & tcom_not_running_pipe [w_cs][cfg_tcl] & ~grant;
            end
            
            always @ (*)
            begin
                can_deep_pdown [w_cs] = power_saving_enter_ready [w_cs] & all_banks_closed [w_cs] & tcom_not_running [w_cs] & tcom_not_running_pipe [w_cs][cfg_tcl] & ~grant;
            end
            
            // Can power down signal for each rank
            always @ (*)
            begin
                can_power_down [w_cs] = power_saving_enter_ready [w_cs] & all_banks_closed [w_cs] & tcom_not_running [w_cs] & tcom_not_running_pipe [w_cs][cfg_tcl] & ~grant;
            end
            
            // Can exit power saving mode signal for each rank
            always @ (*)
            begin
                can_exit_power_saving_mode [w_cs] = power_saving_exit_ready [w_cs];
            end
        end
    endgenerate
    
/*------------------------------------------------------------------------------

    [START] Power Saving Rank Monitor

------------------------------------------------------------------------------*/
    /*------------------------------------------------------------------------------
        Power Saving State Machine
    ------------------------------------------------------------------------------*/
    generate
        genvar u_cs;
        for (u_cs = 0;u_cs < CFG_MEM_IF_CHIP;u_cs = u_cs + 1)
        begin : power_saving_logic_per_chip
            reg  [POWER_SAVING_COUNTER_WIDTH      - 1 : 0] power_saving_cnt;
            reg  [POWER_SAVING_EXIT_COUNTER_WIDTH - 1 : 0] power_saving_exit_cnt;
            reg  [8                                   : 0] state;
            reg  [2                                   : 0] sideband_state;
            reg  [1                                   : 0] disable_clk_state;
            reg                                            disable_clk_state_busy;
            reg                                            disable_clk_state_busy_r;
            reg                                            int_enter_power_saving_ready;
            reg                                            int_exit_power_saving_ready;
            reg                                            registered_reset;
            reg                                            int_zq_cal_req;
            reg                                            int_do_power_down;
            reg                                            int_do_power_down_r1;
            reg                                            int_do_deep_power_down;
            reg                                            int_do_deep_power_down_r1;
            reg                                            int_do_self_refresh;
            reg                                            int_do_self_refresh_r1;
            reg [DISABLE_CLK_COUNTER_WIDTH        - 1 : 0] disable_clk_cnt;
            reg                                            disable_clk_entry;
            reg                                            disable_clk_exit;
            reg                                            int_disable_clk;
            reg                                            int_disable_clk_r1;
            reg                                            in_refresh;
            
            // assignment
            assign power_saving_enter_ready [u_cs] = int_enter_power_saving_ready;
            assign power_saving_exit_ready  [u_cs] = int_exit_power_saving_ready  & ~((int_do_power_down & ~int_do_power_down_r1) | (int_do_self_refresh & ~int_do_self_refresh_r1) | (int_do_deep_power_down & ~int_do_deep_power_down_r1));
            
            assign cs_zq_cal_req            [u_cs] = int_zq_cal_req;
            
            // Counter for power saving state machine
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    power_saving_cnt <= 0;
                end
                else
                begin
                    if (do_precharge_all[u_cs] || do_refresh[u_cs] || do_self_rfsh[u_cs] || do_power_down[u_cs])
                    begin
                        power_saving_cnt <= BANK_TIMER_COUNTER_OFFSET;
                    end
                    else if (power_saving_cnt != {POWER_SAVING_COUNTER_WIDTH{1'b1}})
                    begin
                        power_saving_cnt <= power_saving_cnt + 1'b1;
                    end
                end
            end
            
            // Counter for power saving exit cycles
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    power_saving_exit_cnt <= 0;
                end
                else
                begin
                    if ((int_do_power_down & !int_do_power_down_r1) || (int_do_self_refresh & !int_do_self_refresh_r1) || (int_do_deep_power_down & !int_do_deep_power_down_r1))
                    begin
                        power_saving_exit_cnt <= BANK_TIMER_COUNTER_OFFSET;
                    end
                    else if (power_saving_exit_cnt != {POWER_SAVING_EXIT_COUNTER_WIDTH{1'b1}})
                    begin
                        power_saving_exit_cnt <= power_saving_exit_cnt + 1'b1;
                    end
                end
            end
            
            // Disable clock counter
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    disable_clk_cnt <= 0;
                end
                else
                begin
                    if ((int_do_self_refresh & !int_do_self_refresh_r1) || (int_do_deep_power_down & !int_do_deep_power_down_r1) || (!int_disable_clk & int_disable_clk_r1))
                    begin
                        disable_clk_cnt <= BANK_TIMER_COUNTER_OFFSET;
                    end
                    else if (disable_clk_cnt != {DISABLE_CLK_COUNTER_WIDTH{1'b1}})
                    begin
                        disable_clk_cnt <= disable_clk_cnt + 1'b1;
                    end
                end
            end
            
            // Do power down, deep power down and self refresh register
            always @ (*)
            begin
                int_do_power_down = do_power_down[u_cs];
            end
            
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    int_do_power_down_r1 <= 1'b0;
                end
                else
                begin
                    int_do_power_down_r1 <= int_do_power_down;
                end
            end
            
            always @ (*)
            begin
                int_do_deep_power_down = do_deep_pdown[u_cs];
            end
            
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    int_do_deep_power_down_r1 <= 1'b0;
                end
                else
                begin
                    int_do_deep_power_down_r1 <= int_do_deep_power_down;
                end
            end
            
            always @ (*)
            begin
                int_do_self_refresh = do_self_rfsh[u_cs];
            end
            
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    int_do_self_refresh_r1 <= 1'b0;
                end
                else
                begin
                    int_do_self_refresh_r1 <= int_do_self_refresh;
                end
            end
            
            // Disable clock registers
            always @ (*)
            begin
                int_disable_clk = disable_clk[u_cs];
            end
            
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    int_disable_clk_r1 <= 1'b0;
                end
                else
                begin
                    int_disable_clk_r1 <= int_disable_clk;
                end
            end
            
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    disable_clk_state_busy_r <= 1'b0;
                end
                else
                begin
                    disable_clk_state_busy_r <= disable_clk_state_busy;
                end
            end
            
            // Power saving mode exit ready information
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    int_exit_power_saving_ready <= 1'b0;
                end
                else
                begin
                    if      ((int_do_power_down)      && (!int_do_power_down_r1))       // positive edge detector but late by one clock cycle
                    begin
                        int_exit_power_saving_ready <= 1'b0;
                    end
                    else if ((int_do_self_refresh)    && (!int_do_self_refresh_r1))     // positive edge detector
                    begin
                        int_exit_power_saving_ready <= 1'b0;
                    end
                    else if ((int_do_deep_power_down) && (!int_do_deep_power_down_r1))  // positive edge detector
                    begin
                        int_exit_power_saving_ready <= 1'b0;
                    end
                    else if (power_saving_exit_cnt >= t_param_power_saving_exit)
                    begin
                        int_exit_power_saving_ready <= 1'b1;
                    end
                    else
                    begin
                        int_exit_power_saving_ready <= 1'b0;
                    end
                end
            end
            
            // Disable clock entry and exit logic
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    disable_clk_entry <= 1'b0;
                    disable_clk_exit  <= 1'b0;
                end
                else
                begin
                    if      ((int_do_self_refresh)    && (!int_do_self_refresh_r1))     // positive edge detector
                    begin
                        disable_clk_entry <= 1'b0;
                        disable_clk_exit  <= 1'b0;
                    end
                    else if ((int_do_deep_power_down) && (!int_do_deep_power_down_r1))  // positive edge detector
                    begin
                        disable_clk_entry <= 1'b0;
                        disable_clk_exit  <= 1'b0;
                    end
                    else if ((!int_disable_clk)       && (int_disable_clk_r1))          // negative edge detector
                    begin
                        disable_clk_entry <= 1'b0;
                        disable_clk_exit  <= 1'b0;
                    end
                    else if (disable_clk_cnt >= t_param_mem_clk_entry_cycles)
                    begin
                        disable_clk_entry <= 1'b1;
                        disable_clk_exit  <= 1'b1;
                    end
                    else
                    begin
                        disable_clk_entry <= 1'b0;
                        disable_clk_exit  <= 1'b0;
                    end
                end
            end
            
            // stall_chip output signal
            always @ (*)
            begin
                if (CFG_RANK_TIMER_OUTPUT_REG)
                begin
                    stall_chip[u_cs] = int_stall_chip[u_cs] | int_stall_chip_combi[u_cs];
                end
                else
                begin
                    stall_chip[u_cs] = int_stall_chip[u_cs];
                end
            end
            
            // int_stall_chip_combi signal, we need to issue stall chip one clock cycle earlier to rank timer
            // because rank timer is using a register output
            always @ (*)
            begin
                if (state == IDLE_S3)
                begin
                    if (refresh_chip_req[u_cs] && !do_refresh[u_cs])
                    begin
                        int_stall_chip_combi[u_cs] = 1'b1;
                    end
                    else if (self_refresh_chip_req[u_cs])
                    begin
                        int_stall_chip_combi[u_cs] = 1'b1;
                    end
                    else if (deep_pdown_chip_req[u_cs])
                    begin
                        int_stall_chip_combi[u_cs] = 1'b1;
                    end
                    else if (power_down_chip_req_combi[u_cs])
                    begin
                        int_stall_chip_combi[u_cs] = 1'b1;
                    end
                    else
                    begin
                        int_stall_chip_combi[u_cs] = 1'b0;
                    end
                end
                else
                begin
                    int_stall_chip_combi[u_cs] = 1'b0;
                end
            end
            
            // command issuing state machine
            always @(posedge ctl_clk, negedge ctl_reset_n)
            begin : FSM
                if (!ctl_reset_n)
                begin
                    state                      <= INIT;
                    int_stall_chip[u_cs]       <= 1'b0;
                    stall_arbiter[u_cs]        <= 1'b0;
                    do_power_down[u_cs]        <= 1'b0;
                    do_deep_pdown[u_cs]        <= 1'b0;
                    do_self_rfsh_req[u_cs]     <= 1'b0;
                    do_zqcal_req[u_cs]         <= 1'b0;
                    doing_zqcal[u_cs]          <= 1'b0;
                    do_pch_all_req[u_cs]       <= 1'b0;
                    do_refresh_req[u_cs]       <= 1'b0;
                    afi_ctl_refresh_done[u_cs] <= 1'b0;
                    dqstrk_exit[u_cs]          <= 1'b0;
                    init_req[u_cs]             <= 1'b0;
                    trk_rfsh_cntr[u_cs]        <= 0;
                end
                else
                begin
                    case(state)
                        INIT :
                            begin
                                if (ctl_cal_success == 1'b1)
                                begin
                                    state                <= IDLE_S3;
                                    int_stall_chip[u_cs] <= 1'b0;
                                end
                                else
                                begin
                                    state                <= INIT;
                                    int_stall_chip[u_cs] <= 1'b1;
                                end
                            end
                        IDLE_S3 :
                            begin
                                do_pch_all_req[u_cs] <= 1'b0;
                                
                                if (do_zqcal_req[u_cs])
                                begin
                                    if (do_zqcal[u_cs])
                                    begin
                                        do_zqcal_req[u_cs]  <= 1'b0;
                                        doing_zqcal[u_cs]   <= 1'b0;
                                        stall_arbiter[u_cs] <= 1'b0;
                                    end
                                end
                                else if (refresh_chip_req[u_cs] && !do_refresh[u_cs])
                                begin
                                    int_stall_chip[u_cs] <= 1'b1;
                                    
                                    if (all_banks_closed[u_cs])
                                    begin
                                        state <= REFRESH;
                                    end
                                    else
                                    begin
                                        state <= PCHALL;
                                    end
                                end
                                else if (self_refresh_chip_req[u_cs])
                                begin
                                    int_stall_chip[u_cs] <= 1'b1;
                                    
                                    if (all_banks_closed[u_cs])
                                    begin
                                        state <= SELFRFSH;
                                    end
                                    else
                                    begin
                                        state <= PCHALL;
                                    end
                                end
                                else if (deep_pdown_chip_req[u_cs])
                                begin
                                    int_stall_chip[u_cs] <= 1'b1;
                                    
                                    if (all_banks_closed[u_cs])
                                    begin
                                        state <= DEEPPDN;
                                    end
                                    else
                                    begin
                                        state <= PCHALL;
                                    end
                                end
                                else if (power_down_chip_req_combi[u_cs])
                                begin
                                    int_stall_chip[u_cs] <= 1'b1;
                                    
                                    if (all_banks_closed[u_cs])
                                    begin
                                        state <= PDOWN;
                                    end
                                    else
                                    begin
                                        state <= PCHALL;
                                    end
                                end
                                else if (int_stall_chip[u_cs] && !do_refresh[u_cs] && power_saving_enter_ready[u_cs])
                                begin
                                    int_stall_chip[u_cs] <= 1'b0;
                                end
                            end
                        PCHALL :
                            begin
                                if (refresh_chip_req[u_cs] | self_refresh_chip_req[u_cs] | power_down_chip_req_combi[u_cs] | deep_pdown_chip_req[u_cs])
                                begin
                                    if (do_precharge_all[u_cs] || all_banks_closed[u_cs])
                                    begin
                                        do_pch_all_req[u_cs] <= 1'b0;
                                        stall_arbiter[u_cs]  <= 1'b0;
                                        
                                        if (refresh_chip_req[u_cs])
                                        begin
                                            state <= REFRESH;
                                        end
                                        else if (self_refresh_chip_req[u_cs])
                                        begin
                                            state <= SELFRFSH;
                                        end
                                        else if (deep_pdown_chip_req[u_cs])
                                        begin
                                            state <= DEEPPDN;
                                        end
                                        else
                                        begin
                                            state <= PDOWN;
                                        end
                                    end
                                    else if (refresh_chip_req[u_cs])
                                    begin
                                        if ((~all_banks_closed & refresh_chip_req     ) == (~all_banks_closed & tcom_not_running & refresh_chip_req     ) && !grant)
                                        begin
                                            do_pch_all_req[u_cs] <= 1'b1;
                                            stall_arbiter[u_cs]  <= 1'b1;
                                        end
                                    end
                                    else if (self_refresh_chip_req[u_cs])
                                    begin
                                        if ((~all_banks_closed & self_refresh_chip_req) == (~all_banks_closed & tcom_not_running & self_refresh_chip_req) && !grant)
                                        begin
                                            do_pch_all_req[u_cs] <= 1'b1;
                                            stall_arbiter[u_cs]  <= 1'b1;
                                        end
                                    end
                                    else if (&tcom_not_running && !grant) // for power down and deep power down request, since these request must go to all chips
                                    begin
                                        do_pch_all_req[u_cs] <= 1'b1;
                                        stall_arbiter[u_cs]  <= 1'b1;
                                    end
                                end
                                else
                                begin
                                    state                <= IDLE_S3;
                                    do_pch_all_req[u_cs] <= 1'b0;
                                    stall_arbiter[u_cs]  <= 1'b0;
                                end
                            end
                        REFRESH :
                            begin
                                if (do_refresh[u_cs])
                                begin
                                    do_refresh_req[u_cs] <= 1'b0;
                                    stall_arbiter[u_cs]  <= 1'b0;
                                    if (cfg_enable_dqs_tracking && (do_refresh_to_all_chip | do_refresh_to_all_chip_r))
                                        trk_rfsh_cntr[u_cs]  <= trk_rfsh_cntr[u_cs] + 1'b1;
                                    
                                    if (cfg_enable_dqs_tracking && (do_refresh_to_all_chip | do_refresh_to_all_chip_r) && trk_rfsh_cntr[u_cs] == SAMPLE_EVERY_N_RFSH)
                                    begin
                                        state <= DQSTRK;
                                        trk_rfsh_cntr[u_cs] <= 0;
                                    end
                                    else if (power_down_chip_req_combi[u_cs])
                                    begin
                                        state <= PDOWN;
                                    end
                                    else if (zqcal_req)
                                    begin
                                        state <= ZQCAL;
                                        doing_zqcal[u_cs] <= 1'b1;
                                    end
                                    else
                                    begin
                                        state <= IDLE_S3;
                                    end
                                end
                                else if (refresh_chip_req[u_cs])
                                begin
                                    if (!all_banks_closed[u_cs])
                                    begin
                                        state <= PCHALL;
                                    end
                                    else if (refresh_chip_req == (can_refresh & refresh_chip_req))
                                    begin
                                        do_refresh_req[u_cs] <= 1'b1;
                                        stall_arbiter[u_cs]  <= 1'b1;
                                    end
                                end
                                else
                                begin
                                    if (zqcal_req)
                                    begin
                                        state <= ZQCAL;
                                        doing_zqcal[u_cs] <= 1'b1;
                                    end
                                    else
                                    begin
                                    state <= IDLE_S3;
                                    end
                                    stall_arbiter[u_cs] <= 1'b0;
                                end
                            end
                        SELFRFSH :
                            begin
                                if (!all_banks_closed[u_cs])
                                begin
                                    state <= PCHALL;
                                end
                                else if (!self_refresh_chip_req[u_cs] && can_exit_power_saving_mode[u_cs] && !disable_clk_state_busy)
                                begin
                                    do_self_rfsh_req[u_cs] <= 1'b0;
                                    stall_arbiter[u_cs]    <= 1'b0;
                                    
                                    if (cfg_type == `MMR_TYPE_DDR3) // DDR3
                                    begin
                                        state             <= ZQCAL;
                                        doing_zqcal[u_cs] <= 1'b1;
                                    end
                                    else
                                    begin
                                        state <= IDLE_S3;
                                    end
                                end
                                else if (do_self_rfsh_req_r2[u_cs])
                                begin
                                    stall_arbiter[u_cs] <= 1'b0; // only assert for three clock cycle for self refresh entry (thress instead of one to solve conflicting command issues in half/quarter rate and regdimm design)
                                end
                                else if (self_refresh_chip_req == (can_self_rfsh & self_refresh_chip_req) && !(|do_precharge_all))
                                begin
                                    do_self_rfsh_req[u_cs] <= 1'b1;
                                    stall_arbiter[u_cs]    <= 1'b1;
                                end
                            end
                        PDOWN :
                            begin
                                if (refresh_chip_req[u_cs] && !do_refresh[u_cs] && can_exit_power_saving_mode[u_cs])
                                begin
                                    state               <= REFRESH;
                                    do_power_down[u_cs] <= 1'b0;
                                    stall_arbiter[u_cs] <= 1'b0;
                                end
                                else if (!power_down_chip_req_combi[u_cs] && can_exit_power_saving_mode[u_cs])
                                begin
                                    do_power_down[u_cs] <= 1'b0;
                                    stall_arbiter[u_cs] <= 1'b0;
                                    state               <= IDLE_S3;
                                end
                                else if (&can_power_down && !refresh_req)
                                begin
                                    do_power_down[u_cs] <= 1'b1;
                                    stall_arbiter[u_cs] <= 1'b1;
                                end
                                else
                                begin
                                    state <= PDOWN;
                                end
                            end
                        DEEPPDN :
                            begin
                                if (!all_banks_closed[u_cs])
                                begin
                                    state <= PCHALL;
                                end
                                else if (!deep_pdown_chip_req[u_cs] && can_exit_power_saving_mode[u_cs] && !disable_clk_state_busy)
                                begin
                                    do_deep_pdown[u_cs] <= 1'b0;
                                    state               <= INITREQ;
                                end
                                else if (&can_deep_pdown && !(|do_precharge_all))
                                begin
                                    do_deep_pdown[u_cs] <= 1'b1;
                                    stall_arbiter[u_cs] <= 1'b1;
                                end
                            end
                        ZQCAL :
                            begin
                                if (cs_zq_cal_req[u_cs])
                                begin
                                    do_zqcal_req[u_cs]  <= 1'b1;
                                    stall_arbiter[u_cs] <= 1'b1;
                                    
                                    state <= IDLE_S3;
                                end
                            end
                        DQSTRK :
                            begin
                                if      (!dqstrk_exit[u_cs] && !afi_ctl_refresh_done[u_cs] && !do_refresh[u_cs] && power_saving_enter_ready[u_cs])
                                begin
                                    afi_ctl_refresh_done[u_cs] <= 1'b1;
                                end
                                else if (!dqstrk_exit[u_cs] &&  afi_ctl_refresh_done[u_cs] && afi_seq_busy_r2[u_cs]) // stall until seq_busy is deasserted
                                begin
                                    dqstrk_exit[u_cs] <= 1'b1;
                                end
                                else if (dqstrk_exit[u_cs] && !afi_seq_busy_r2[u_cs])
                                begin
                                    afi_ctl_refresh_done[u_cs] <= 1'b0;
                                    dqstrk_exit[u_cs]          <= 1'b0;
                                    state                      <= IDLE_S3;
                                end
                            end
                        INITREQ :
                            begin
                                if (!init_req[u_cs])
                                begin
                                    init_req[u_cs] <= 1'b1;
                                end
                                else if (!ctl_cal_success) // wait for cal_success to go low
                                begin
                                    init_req[u_cs] <= 1'b0;
                                    state          <= INIT;
                                end
                            end
                        default :
                            begin
                                state <= IDLE_S3;
                            end
                    endcase
                end
            end
            
            // Disable memory clock state
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    disable_clk_state      <= IDLE_S2;
                    disable_clk_state_busy <= 1'b0;
                    disable_clk[u_cs]      <= 1'b0;
                end
                else
                begin
                    case (disable_clk_state)
                        IDLE_S2 :
                            begin
                                if (do_self_rfsh[u_cs] && !disable_clk_state_busy_r) // to prevent it from re-entering disable clock state
                                begin
                                    disable_clk_state      <= DISABLECLK1;
                                    disable_clk_state_busy <= 1'b1;
                                end
                                else if (do_deep_pdown[u_cs] && !disable_clk_state_busy_r) // to prevent it from re-entering disable clock state
                                begin
                                    disable_clk_state      <= DISABLECLK1;
                                    disable_clk_state_busy <= 1'b1;
                                end
                                else
                                begin
                                    disable_clk_state      <= IDLE_S2;
                                    disable_clk_state_busy <= 1'b0;
                                    disable_clk[u_cs]      <= 1'b0;
                                end
                            end
                        DISABLECLK1 :
                            begin
                                if ((!deep_pdown_chip_req[u_cs] && !self_refresh_chip_req[u_cs]) && can_exit_power_saving_mode[u_cs]) // exit both power saving state
                                begin
                                    disable_clk_state <= DISABLECLK2;
                                    disable_clk[u_cs] <= 1'b0;
                                end
                                else if (disable_clk_entry) // can disable memory clock now
                                begin
                                    disable_clk[u_cs] <= 1'b1;
                                end
                            end
                        DISABLECLK2 :
                            begin
                                if (!(!int_disable_clk && int_disable_clk_r1) && disable_clk_exit) // delay by N clock cycles before exting deep power down or self refresh
                                begin
                                    disable_clk_state      <= IDLE_S2;
                                    disable_clk_state_busy <= 1'b0;
                                end
                            end
                        default :
                            begin
                                disable_clk_state <= IDLE_S2;
                            end
                    endcase
                end
            end
            
            // sideband state machine
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    sideband_state               <= IDLE_S1;
                    int_enter_power_saving_ready <= 1'b0;
                    int_zq_cal_req               <= 1'b0;
                    sideband_in_refresh[u_cs]    <= 1'b0;
                end
                else
                begin
                    case (sideband_state)
                        IDLE_S1 :
                            begin
                                int_zq_cal_req <= 1'b0;
                                
                                if (power_saving_cnt >= t_param_pch_all_to_valid)
                                begin
                                    int_enter_power_saving_ready <= 1'b1;
                                end
                                else
                                begin
                                    int_enter_power_saving_ready <= 1'b0;
                                end
                                
                                if (do_precharge_all[u_cs])
                                begin
                                    int_enter_power_saving_ready <= 1'b0;
                                end
                                
                                if (do_refresh[u_cs])
                                begin
                                    sideband_state               <= ARF;
                                    int_enter_power_saving_ready <= 1'b0;
                                    
                                end
                                
                                if (do_self_rfsh[u_cs])
                                begin
                                    sideband_state               <= SRF;
                                    int_enter_power_saving_ready <= 1'b0;
                                end
                                
                                if (do_power_down[u_cs])
                                begin
                                    sideband_state               <= PDN;
                                    int_enter_power_saving_ready <= 1'b0;
                                end
                               
                                if (do_refresh[u_cs])
                                begin
                                    sideband_in_refresh[u_cs]    <= 1'b1;
                                end
                                else
                                begin
                                    sideband_in_refresh[u_cs]    <= 1'b0;
                                end
                                          
                            end
                        ARF :
                            begin
                                if (power_saving_cnt >= t_param_arf_to_valid)
                                begin
                                    sideband_state               <= IDLE_S1;
                                    int_enter_power_saving_ready <= 1'b1;
                                    sideband_in_refresh[u_cs]    <= 1'b0;
                                    int_zq_cal_req               <= 1'b1;
                                end
                                else
                                begin
                                    sideband_state               <= ARF;
                                    int_enter_power_saving_ready <= 1'b0;
                                    sideband_in_refresh[u_cs]    <= 1'b1;
                                    int_zq_cal_req               <= 1'b0;
                                end
                            end
                        SRF :
                            begin
                                sideband_in_refresh[u_cs]          <= 1'b0;
										  
                                // ZQ request to state machine
                                if (power_saving_cnt == t_param_srf_to_zq_cal) // only one cycle
                                begin
                                    int_zq_cal_req <= 1'b1;
                                end
                                else
                                begin
                                    int_zq_cal_req <= 1'b0;
                                end
                                
                                if (!do_self_rfsh[u_cs] && power_saving_cnt >= t_param_srf_to_valid)
                                begin
                                    sideband_state               <= IDLE_S1;
                                    int_enter_power_saving_ready <= 1'b1;
                                end
                                else
                                begin
                                    sideband_state               <= SRF;
                                    int_enter_power_saving_ready <= 1'b0;
                                end
                            end
                        PDN :
                            begin
                                int_zq_cal_req <= 1'b0;
                                sideband_in_refresh[u_cs]          <= 1'b0;
                                
                                if (!do_power_down[u_cs] && power_saving_cnt >= t_param_pdn_to_valid)
                                begin
                                    sideband_state               <= IDLE_S1;
                                    int_enter_power_saving_ready <= 1'b1;
                                end
                                else
                                begin
                                    sideband_state               <= PDN;
                                    int_enter_power_saving_ready <= 1'b0;
                                end
                            end
                        default :
                            begin
                                sideband_state <= IDLE_S1;
                                sideband_in_refresh[u_cs]          <= 1'b0;
                            end
                    endcase
                end
            end
        end
    endgenerate
    
    /*------------------------------------------------------------------------------
        Refresh Request
    ------------------------------------------------------------------------------*/
    generate
        genvar s_cs;
        for (s_cs = 0;s_cs < CFG_MEM_IF_CHIP;s_cs = s_cs + 1)
        begin : auto_refresh_logic_per_chip
            reg [ARF_COUNTER_WIDTH - 1 : 0] refresh_cnt;
            
            // refresh counter
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    refresh_cnt <= 0;
                end
                else
                begin
                    if (self_rfsh_req && !self_rfsh_req_r && |self_rfsh_chip && !do_refresh[s_cs])
                    begin
                        refresh_cnt <= {ARF_COUNTER_WIDTH{1'b1}};
                    end
                    else if (do_refresh[s_cs])
                    begin
                        refresh_cnt <= 3;
                    end
                    else if (refresh_cnt != {ARF_COUNTER_WIDTH{1'b1}})
                    begin
                        refresh_cnt <= refresh_cnt + 1'b1;
                    end
                end
            end
            
            // refresh request logic
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    cs_refresh_req [s_cs] <= 1'b0;
                end
                else
                begin
                    if (self_rfsh_req && !self_rfsh_req_r && |self_rfsh_chip && !do_refresh[s_cs])
                    begin
                        cs_refresh_req [s_cs] <= 1'b1;
                    end
                    else if (do_refresh[s_cs] || do_self_rfsh[s_cs])
                    begin
                        cs_refresh_req [s_cs] <= 1'b0;
                    end
                    else if (refresh_cnt >= t_param_arf_period)
                    begin
                        cs_refresh_req [s_cs] <= 1'b1;
                    end
                    else
                    begin
                        cs_refresh_req [s_cs] <= 1'b0;
                    end
                end
            end
        end
    endgenerate
    
    /*------------------------------------------------------------------------------
        Power Down Request
    ------------------------------------------------------------------------------*/
    // register no command signal
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            no_command_r1 <= 1'b0;
        end
        else
        begin
            no_command_r1 <= tbp_empty;
        end
    end
    
    // power down counter
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            power_down_cnt <= 0;
        end
        else
        begin
            if ((!tbp_empty && no_command_r1) || self_rfsh_req) // negative edge detector
            begin
                power_down_cnt <= 3;
            end
            else if (tbp_empty && power_down_cnt != {PDN_COUNTER_WIDTH{1'b1}} && ctl_cal_success)
            begin
                power_down_cnt <= power_down_cnt + 1'b1;
            end
        end
    end
    
    // power down request logic
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            power_down_chip_req <= 0;
        end
        else
        begin
            if (t_param_pdn_period == 0) // when auto power down cycles is set to '0', auto power down mode will be disabled
            begin
                power_down_chip_req <= 0;
            end
            else
            begin
                if (!tbp_empty || self_rfsh_req) // we need to make sure power down request to go low as fast as possible to avoid unnecessary power down
                begin
                    power_down_chip_req <= 0;
                end
                else if (power_down_chip_req == 0)
                begin
                    if (power_down_cnt >= t_param_pdn_period && !(|doing_zqcal))
                    begin
                        power_down_chip_req <= {CFG_MEM_IF_CHIP{1'b1}};
                    end
                    else
                    begin
                        power_down_chip_req <= 0;
                    end
                end
                else if (!(power_down_cnt >= t_param_pdn_period))
                begin
                    power_down_chip_req <= 0;
                end
            end
        end
    end
    
    assign power_down_chip_req_combi = power_down_chip_req & {CFG_MEM_IF_CHIP{tbp_empty}} & {CFG_MEM_IF_CHIP{~refresh_req}};

/*------------------------------------------------------------------------------

    [END] Power Saving Rank Monitor

------------------------------------------------------------------------------*/
    
    
endmodule
