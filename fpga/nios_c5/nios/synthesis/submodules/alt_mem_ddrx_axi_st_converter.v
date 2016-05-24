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
module alt_mem_ddrx_axi_st_converter #
    ( parameter
        AXI_ID_WIDTH      = 4,
        AXI_ADDR_WIDTH    = 32,
        AXI_LEN_WIDTH     = 4,
        AXI_SIZE_WIDTH    = 3,
        AXI_BURST_WIDTH   = 2,
        AXI_LOCK_WIDTH    = 2,
        AXI_CACHE_WIDTH   = 4,
        AXI_PROT_WIDTH    = 3,
        AXI_DATA_WIDTH    = 32,
        AXI_RESP_WIDTH    = 4,
        
        ST_ADDR_WIDTH     = 32,
        ST_SIZE_WIDTH     = 5,
        ST_ID_WIDTH       = 4,
        ST_DATA_WIDTH     = 32,
        
        COMMAND_ARB_TYPE  = "ROUND_ROBIN",
        
        REGISTERED        = 1
    )
    (
        ctl_clk,
        ctl_reset_n,
        
        // AXI Interface
        // Write address channel
        awid,
        awaddr,
        awlen,
        awsize,
        awburst,
        awlock,
        awcache,
        awprot,
        awvalid,
        awready,
        
        // Write data channel
        wid,
        wdata,
        wstrb,
        wlast,
        wvalid,
        wready,
        
        // Write response channel
        bid,
        bresp,
        bvalid,
        bready,
        
        // Read address channel
        arid,
        araddr,
        arlen,
        arsize,
        arburst,
        arlock,
        arcache,
        arprot,
        arvalid,
        arready,
        
        // Read data channel
        rid,
        rdata,
        rresp,
        rlast,
        rvalid,
        rready,
        
        // Avalon ST Interface
        // Command channel
        itf_cmd_ready,
        itf_cmd_valid,
        itf_cmd,
        itf_cmd_address,
        itf_cmd_burstlen,
        itf_cmd_id,
        itf_cmd_priority,
        itf_cmd_autoprecharge,
        itf_cmd_multicast,
        
        // Write data channel
        itf_wr_data_ready,
        itf_wr_data_valid,
        itf_wr_data,
        itf_wr_data_byte_en,
        itf_wr_data_begin,
        itf_wr_data_last,
        itf_wr_data_id,
        
        // Read data channel
        itf_rd_data_ready,
        itf_rd_data_valid,
        itf_rd_data,
        itf_rd_data_error,
        itf_rd_data_begin,
        itf_rd_data_last,
        itf_rd_data_id
    );

input                               ctl_clk;
input                               ctl_reset_n;

// AXI Interface
// Write address channel
input  [AXI_ID_WIDTH       - 1 : 0] awid;
input  [AXI_ADDR_WIDTH     - 1 : 0] awaddr;
input  [AXI_LEN_WIDTH      - 1 : 0] awlen;
input  [AXI_SIZE_WIDTH     - 1 : 0] awsize;
input  [AXI_BURST_WIDTH    - 1 : 0] awburst;
input  [AXI_LOCK_WIDTH     - 1 : 0] awlock;
input  [AXI_CACHE_WIDTH    - 1 : 0] awcache;
input  [AXI_PROT_WIDTH     - 1 : 0] awprot;
input                               awvalid;
output                              awready;

// Write data channel
input  [AXI_ID_WIDTH       - 1 : 0] wid;
input  [AXI_DATA_WIDTH     - 1 : 0] wdata;
input  [AXI_DATA_WIDTH / 8 - 1 : 0] wstrb;
input                               wlast;
input                               wvalid;
output                              wready;

// Write response channel
output [AXI_ID_WIDTH       - 1 : 0] bid;
output [AXI_RESP_WIDTH     - 1 : 0] bresp;
output                              bvalid;
input                               bready;

// Read address channel
input  [AXI_ID_WIDTH       - 1 : 0] arid;
input  [AXI_ADDR_WIDTH     - 1 : 0] araddr;
input  [AXI_LEN_WIDTH      - 1 : 0] arlen;
input  [AXI_SIZE_WIDTH     - 1 : 0] arsize;
input  [AXI_BURST_WIDTH    - 1 : 0] arburst;
input  [AXI_LOCK_WIDTH     - 1 : 0] arlock;
input  [AXI_CACHE_WIDTH    - 1 : 0] arcache;
input  [AXI_PROT_WIDTH     - 1 : 0] arprot;
input                               arvalid;
output                              arready;

// Read data channel
output [AXI_ID_WIDTH       - 1 : 0] rid;
output [AXI_DATA_WIDTH     - 1 : 0] rdata;
output [AXI_RESP_WIDTH     - 1 : 0] rresp;
output                              rlast;
output                              rvalid;
input                               rready;

// Avalon ST Interface
// Command channel
input                               itf_cmd_ready;
output                              itf_cmd_valid;
output                              itf_cmd;
output [ST_ADDR_WIDTH      - 1 : 0] itf_cmd_address;
output [ST_SIZE_WIDTH      - 1 : 0] itf_cmd_burstlen;
output [ST_ID_WIDTH        - 1 : 0] itf_cmd_id;
output                              itf_cmd_priority;
output                              itf_cmd_autoprecharge;
output                              itf_cmd_multicast;

// Write data channel
input                               itf_wr_data_ready;
output                              itf_wr_data_valid;
output [ST_DATA_WIDTH      - 1 : 0] itf_wr_data;
output [ST_DATA_WIDTH / 8  - 1 : 0] itf_wr_data_byte_en;
output                              itf_wr_data_begin;
output                              itf_wr_data_last;
output [ST_ID_WIDTH        - 1 : 0] itf_wr_data_id;

// Read data channel
output                              itf_rd_data_ready;
input                               itf_rd_data_valid;
input [ST_DATA_WIDTH       - 1 : 0] itf_rd_data;
input                               itf_rd_data_error;
input                               itf_rd_data_begin;
input                               itf_rd_data_last;
input [ST_ID_WIDTH         - 1 : 0] itf_rd_data_id;

//--------------------------------------------------------------------------------------------------------
//
//  [START] Registers & Wires
//
//--------------------------------------------------------------------------------------------------------
    // AXI outputs
    reg                              awready;
    reg                              wready;
    reg  [AXI_ID_WIDTH      - 1 : 0] bid;
    reg  [AXI_RESP_WIDTH    - 1 : 0] bresp;
    reg                              bvalid;
    reg                              arready;
    reg  [AXI_ID_WIDTH      - 1 : 0] rid;
    reg  [AXI_DATA_WIDTH    - 1 : 0] rdata;
    reg  [AXI_RESP_WIDTH    - 1 : 0] rresp;
    reg                              rlast;
    reg                              rvalid;
    
    // ST outputs
    reg                              itf_cmd_valid;
    reg                              itf_cmd;
    reg  [ST_ADDR_WIDTH     - 1 : 0] itf_cmd_address;
    reg  [ST_SIZE_WIDTH     - 1 : 0] itf_cmd_burstlen;
    reg  [ST_ID_WIDTH       - 1 : 0] itf_cmd_id;
    reg                              itf_cmd_priority;
    reg                              itf_cmd_autoprecharge;
    reg                              itf_cmd_multicast;
    reg                              itf_wr_data_valid;
    reg  [ST_DATA_WIDTH     - 1 : 0] itf_wr_data;
    reg  [ST_DATA_WIDTH / 8 - 1 : 0] itf_wr_data_byte_en;
    reg                              itf_wr_data_begin;
    reg                              itf_wr_data_last;
    reg  [ST_ID_WIDTH       - 1 : 0] itf_wr_data_id;
    reg                              itf_rd_data_ready;
    
    wire one  = 1'b1;
    wire zero = 1'b0;
    
    // Command channel
    localparam WORD_ADDR_OFFSET = log2(AXI_DATA_WIDTH / 8);
    localparam NATIVE_AXI_SIZE  = log2(AXI_DATA_WIDTH) - 3;
    
    integer i;
    
    wire [AXI_ADDR_WIDTH      - 1 : 0] byte_rd_addr;
    wire [AXI_ADDR_WIDTH      - 1 : 0] byte_wr_addr;
    
    wire                               rd_req;
    wire                               wr_req;
    wire [1                       : 0] cmd_req;
    wire                               rd_grant;
    wire                               wr_grant;
    
    wire [ST_ID_WIDTH         - 1 : 0] rd_id;
    wire [ST_ID_WIDTH         - 1 : 0] wr_id;
    
    reg  [AXI_ID_WIDTH        - 1 : 0] int_awid;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] int_awaddr;
    reg  [AXI_LEN_WIDTH       - 1 : 0] int_awlen;
    reg  [AXI_SIZE_WIDTH      - 1 : 0] int_awsize;
    reg  [AXI_BURST_WIDTH     - 1 : 0] int_awburst;
    reg  [AXI_LOCK_WIDTH      - 1 : 0] int_awlock;
    reg  [AXI_CACHE_WIDTH     - 1 : 0] int_awcache;
    reg  [AXI_PROT_WIDTH      - 1 : 0] int_awprot;
    reg                                int_awvalid;
    
    reg  [AXI_ID_WIDTH        - 1 : 0] int_arid;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] int_araddr;
    reg  [AXI_LEN_WIDTH       - 1 : 0] int_arlen;
    reg  [AXI_SIZE_WIDTH      - 1 : 0] int_arsize;
    reg  [AXI_BURST_WIDTH     - 1 : 0] int_arburst;
    reg  [AXI_LOCK_WIDTH      - 1 : 0] int_arlock;
    reg  [AXI_CACHE_WIDTH     - 1 : 0] int_arcache;
    reg  [AXI_PROT_WIDTH      - 1 : 0] int_arprot;
    reg                                int_arvalid;
    
    reg                                int_cmd_valid;
    reg                                int_cmd;
    reg  [ST_ADDR_WIDTH       - 1 : 0] int_cmd_address;
    reg  [ST_SIZE_WIDTH       - 1 : 0] int_cmd_burstlen;
    reg  [ST_ID_WIDTH         - 1 : 0] int_cmd_id;
    reg                                int_cmd_priority;
    reg                                int_cmd_autoprecharge;
    reg                                int_cmd_multicast;
    
    reg                                int_awready;
    reg                                int_arready;
    
    reg  [2 ** AXI_SIZE_WIDTH - 1 : 0] rd_size;
    reg  [2 ** AXI_SIZE_WIDTH - 1 : 0] wr_size;
    reg  [2 ** AXI_SIZE_WIDTH - 1 : 0] current_size;
    
    reg  [2 ** AXI_SIZE_WIDTH - 1 : 0] int_size;
    
    reg  [AXI_ADDR_WIDTH      - 1 : 0] byte_addr;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] byte_addr_counter;
    reg  [ST_ADDR_WIDTH       - 1 : 0] registered_word_addr;
    reg  [ST_ADDR_WIDTH       - 1 : 0] word_rd_addr;
    reg  [ST_ADDR_WIDTH       - 1 : 0] word_wr_addr;
    
    reg  [AXI_ADDR_WIDTH      - 1 : 0] int_byte_rd_addr;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] int_byte_wr_addr;
    
    reg  [AXI_ADDR_WIDTH      - 1 : 0] aligned_byte_rd_addr_mask;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] aligned_byte_wr_addr_mask;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] boundary_byte_rd_addr_mask;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] boundary_byte_wr_addr_mask;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] boundary_byte_rd_addr_increment;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] boundary_byte_wr_addr_increment;
    
    reg  [AXI_ADDR_WIDTH      - 1 : 0] int_aligned_byte_rd_addr_mask;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] int_aligned_byte_wr_addr_mask;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] int_boundary_byte_rd_addr_mask;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] int_boundary_byte_wr_addr_mask;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] int_boundary_byte_rd_addr_increment;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] int_boundary_byte_wr_addr_increment;
    
    reg  [AXI_ADDR_WIDTH      - 1 : 0] aligned_byte_addr;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] aligned_byte_rd_addr;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] aligned_byte_wr_addr;
    
    reg  [AXI_ADDR_WIDTH      - 1 : 0] lower_wrap_boundary_byte_rd_addr;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] upper_wrap_boundary_byte_rd_addr;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] lower_wrap_boundary_byte_wr_addr;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] upper_wrap_boundary_byte_wr_addr;
    
    reg  [AXI_ADDR_WIDTH      - 1 : 0] lower_wrap_boundary_byte_addr;
    reg  [AXI_ADDR_WIDTH      - 1 : 0] upper_wrap_boundary_byte_addr;
    
    reg                                upper_boundary_reached;
    
    reg  [AXI_BURST_WIDTH     - 1 : 0] burst_type;
    
    reg                                doing_write;
    reg  [1                       : 0] cmd_grant;
    reg  [1                       : 0] prev_cmd_grant;
    
    reg                                split_axi_cmd;
    reg                                doing_split;
    reg  [ST_SIZE_WIDTH       - 1 : 0] split_counter;
    reg  [ST_SIZE_WIDTH       - 1 : 0] rd_burstlen;
    reg  [ST_SIZE_WIDTH       - 1 : 0] wr_burstlen;
    
    reg                                int_grant;
    reg                                int_rd_grant;
    reg                                int_wr_grant;
    reg                                int_doing_split;
    
    reg  [ST_ID_WIDTH         - 1 : 0] registered_id;
    
    // Write data channel
    reg                              int_wr_data_valid;
    reg  [ST_DATA_WIDTH     - 1 : 0] int_wr_data;
    reg  [ST_DATA_WIDTH / 8 - 1 : 0] int_wr_data_byte_en;
    reg                              int_wr_data_begin;
    reg                              int_wr_data_last;
    reg  [ST_ID_WIDTH       - 1 : 0] int_wr_data_id;
    
    reg                              write_data_begin_n;
    
    // Write response channel
    localparam WR_CMD_FIFO_DATA_WIDTH  = AXI_ID_WIDTH;
    localparam WR_CMD_FIFO_ADDR_WIDTH  = 5; // expected to be able to hold 32 in-flight command information
    localparam WR_CMD_FIFO_DEPTH       = 2 ** WR_CMD_FIFO_ADDR_WIDTH;
    
    localparam WR_DATA_FIFO_DATA_WIDTH = AXI_ID_WIDTH;
    localparam WR_DATA_FIFO_ADDR_WIDTH = 5; // expected to be able to hold 32 in-flight command information
    localparam WR_DATA_FIFO_DEPTH      = 2 ** WR_DATA_FIFO_ADDR_WIDTH;
    
    wire                                  wr_cmd_fifo_write;
    wire [WR_CMD_FIFO_DATA_WIDTH - 1 : 0] wr_cmd_fifo_write_data;
    wire                                  wr_cmd_fifo_read;
    wire [WR_CMD_FIFO_DATA_WIDTH - 1 : 0] wr_cmd_fifo_read_data;
    wire                                  wr_cmd_fifo_read_data_valid;
    wire                                  wr_cmd_fifo_empty;
    wire                                  wr_cmd_fifo_almost_full;
    
    wire                                  wr_data_fifo_write;
    wire [WR_CMD_FIFO_DATA_WIDTH - 1 : 0] wr_data_fifo_write_data;
    wire                                  wr_data_fifo_read;
    wire [WR_CMD_FIFO_DATA_WIDTH - 1 : 0] wr_data_fifo_read_data;
    wire                                  wr_data_fifo_read_data_valid;
    wire                                  wr_data_fifo_empty;
    wire                                  wr_data_fifo_almost_full;
    
    reg                                   wr_cmd_fifo_ready;
    reg                                   wr_data_fifo_ready;
    
    reg                                   id_matched;
    
    reg  [AXI_ID_WIDTH           - 1 : 0] int_bid;
    reg  [AXI_RESP_WIDTH         - 1 : 0] int_bresp;
    reg                                   int_bvalid;
    
    // Read data channel
    localparam RD_CMD_FIFO_DATA_WIDTH  = AXI_LEN_WIDTH + 1;
    localparam RD_CMD_FIFO_ADDR_WIDTH  = 5; // expected to be able to hold 32 in-flight command information
    localparam RD_CMD_FIFO_DEPTH       = 2 ** RD_CMD_FIFO_ADDR_WIDTH;
    
    wire                                  rd_cmd_fifo_write;
    wire [RD_CMD_FIFO_DATA_WIDTH - 1 : 0] rd_cmd_fifo_write_data;
    wire                                  rd_cmd_fifo_read;
    wire [RD_CMD_FIFO_DATA_WIDTH - 1 : 0] rd_cmd_fifo_read_data;
    wire                                  rd_cmd_fifo_read_data_valid;
    wire                                  rd_cmd_fifo_empty;
    wire                                  rd_cmd_fifo_almost_full;
    
    reg                                   rd_cmd_fifo_ready;
    
    reg  [AXI_ID_WIDTH           - 1 : 0] int_rid;
    reg  [AXI_DATA_WIDTH         - 1 : 0] int_rdata;
    reg  [AXI_RESP_WIDTH         - 1 : 0] int_rresp;
    reg                                   int_rlast;
    reg                                   int_rvalid;
    
    reg                                   int_rd_data_ready;
    
    reg [RD_CMD_FIFO_DATA_WIDTH  - 1 : 0] read_data_counter;
    reg                                   read_data_last;
    
//--------------------------------------------------------------------------------------------------------
//
//  [END] Registers & Wires
//
//--------------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------------
//
//  [START] Command Channel
//
//--------------------------------------------------------------------------------------------------------
    //--------------------------------------------------------------------------
    // AXI inputs
    //--------------------------------------------------------------------------
    always @ (*)
    begin
        int_awid    = awid;
        int_awaddr  = awaddr;
        int_awlen   = awlen;
        int_awsize  = awsize;
        int_awburst = awburst;
        int_awlock  = awlock;
        int_awcache = awcache;
        int_awprot  = awprot;
        int_awvalid = awvalid;
        
        int_arid    = arid;
        int_araddr  = araddr;
        int_arlen   = arlen;
        int_arsize  = arsize;
        int_arburst = arburst;
        int_arlock  = arlock;
        int_arcache = arcache;
        int_arprot  = arprot;
        int_arvalid = arvalid;
    end
    
    //--------------------------------------------------------------------------
    // Size related logics
    //--------------------------------------------------------------------------
    always @ (*)
    begin
        rd_size = (2 ** int_arsize);
        wr_size = (2 ** int_awsize);
    end
    
    generate
    begin
        if (REGISTERED)
        begin
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    int_size    <= 0;
                end
                else
                begin
                    if (rd_grant)
                    begin
                        int_size <= rd_size;
                    end
                    else if (wr_grant)
                    begin
                        int_size <= wr_size;
                    end
                end
            end
        end
        else
        begin
            always @ (*)
            begin
                if (rd_grant)
                begin
                    int_size = rd_size;
                end
                else if (wr_grant)
                begin
                    int_size = wr_size;
                end
                else
                begin
                    int_size = 0;
                end
            end
        end
    end
    endgenerate
    
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            current_size <= 0;
        end
        else
        begin
            if (rd_grant)
            begin
                current_size <= rd_size;
            end
            else if (wr_grant)
            begin
                current_size <= wr_size;
            end
        end
    end
    
    //--------------------------------------------------------------------------
    // Address related logics
    //--------------------------------------------------------------------------
    assign byte_rd_addr = int_araddr;
    assign byte_wr_addr = int_awaddr;
    
    generate
    begin
        if (REGISTERED)
        begin
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    int_byte_rd_addr <= 0;
                    int_byte_wr_addr <= 0;
                end
                else
                begin
                    if (itf_cmd_ready)
                    begin
                        int_byte_rd_addr <= byte_rd_addr;
                        int_byte_wr_addr <= byte_wr_addr;
                    end
                end
            end
        end
        else
        begin
            always @ (*)
            begin
                int_byte_rd_addr = byte_rd_addr;
                int_byte_wr_addr = byte_wr_addr;
            end
        end
    end
    endgenerate
    
    // Obtain address mask for aligned address
    always @ (*)
    begin
        aligned_byte_rd_addr_mask = {AXI_ADDR_WIDTH{1'b1}};
        aligned_byte_wr_addr_mask = {AXI_ADDR_WIDTH{1'b1}};
        
        for (i = 0;i < (2 ** AXI_SIZE_WIDTH);i = i + 1'b1)
        begin
            if (int_arsize > i)
            begin
                aligned_byte_rd_addr_mask [i] = 1'b0;
            end
            
            if (int_awsize > i)
            begin
                aligned_byte_wr_addr_mask [i] = 1'b0;
            end
        end
    end
    
    generate
    begin
        if (REGISTERED)
        begin
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    int_aligned_byte_rd_addr_mask <= 0;
                    int_aligned_byte_wr_addr_mask <= 0;
                end
                else
                begin
                    if (itf_cmd_ready)
                    begin
                        int_aligned_byte_rd_addr_mask <= aligned_byte_rd_addr_mask;
                        int_aligned_byte_wr_addr_mask <= aligned_byte_wr_addr_mask;
                    end
                end
            end
        end
        else
        begin
            always @ (*)
            begin
                int_aligned_byte_rd_addr_mask = aligned_byte_rd_addr_mask;
                int_aligned_byte_wr_addr_mask = aligned_byte_wr_addr_mask;
            end
        end
    end
    endgenerate
    
    // Obtain aligned address
    always @ (*)
    begin
        aligned_byte_rd_addr = (int_byte_rd_addr & int_aligned_byte_rd_addr_mask);
        aligned_byte_wr_addr = (int_byte_wr_addr & int_aligned_byte_wr_addr_mask);
    end
    
    always @ (*)
    begin
        if (int_rd_grant)
        begin
            aligned_byte_addr = aligned_byte_rd_addr;
        end
        else if (int_wr_grant)
        begin
            aligned_byte_addr = aligned_byte_wr_addr;
        end
        else
        begin
            aligned_byte_addr = 0;
        end
    end
    
    // Obtain boundary address mask for wrapping burst
    always @ (*)
    begin
        boundary_byte_rd_addr_mask      = {AXI_ADDR_WIDTH{1'b1}};
        boundary_byte_wr_addr_mask      = {AXI_ADDR_WIDTH{1'b1}};
        
        for (i = 0;i < ((2 ** AXI_SIZE_WIDTH) + 4);i = i + 1'b1) // extend by another 4 because max length for wrapping burst is 16
        begin
            if ((int_arsize + log2_minus_one(int_arlen[3:0])) > i) // constraint burstlen to 4 bits because max length for wrapping burst is 16
            begin
                boundary_byte_rd_addr_mask [i] = 1'b0;
            end
            
            if ((int_awsize + log2_minus_one(int_awlen[3:0])) > i) // constraint burstlen to 4 bits because max length for wrapping burst is 16
            begin
                boundary_byte_wr_addr_mask [i] = 1'b0;
            end
        end
    end
    
    // Obtain boundary increment value for wrapping burst
    always @ (*)
    begin
        boundary_byte_rd_addr_increment = aligned_byte_rd_addr_mask & ~boundary_byte_rd_addr_mask;
        boundary_byte_wr_addr_increment = aligned_byte_wr_addr_mask & ~boundary_byte_wr_addr_mask;
    end
    
    generate
    begin
        if (REGISTERED)
        begin
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    int_boundary_byte_rd_addr_mask      <= 0;
                    int_boundary_byte_wr_addr_mask      <= 0;
                    int_boundary_byte_rd_addr_increment <= 0;
                    int_boundary_byte_wr_addr_increment <= 0;
                end
                else
                begin
                    if (itf_cmd_ready)
                    begin
                        int_boundary_byte_rd_addr_mask      <= boundary_byte_rd_addr_mask;
                        int_boundary_byte_wr_addr_mask      <= boundary_byte_wr_addr_mask;
                        int_boundary_byte_rd_addr_increment <= boundary_byte_rd_addr_increment;
                        int_boundary_byte_wr_addr_increment <= boundary_byte_wr_addr_increment;
                    end
                end
            end
        end
        else
        begin
            always @ (*)
            begin
                int_boundary_byte_rd_addr_mask      = boundary_byte_rd_addr_mask;
                int_boundary_byte_wr_addr_mask      = boundary_byte_wr_addr_mask;
                int_boundary_byte_rd_addr_increment = boundary_byte_rd_addr_increment;
                int_boundary_byte_wr_addr_increment = boundary_byte_wr_addr_increment;
            end
        end
    end
    endgenerate
    
    // Obtain upper and lower boundary of wrapping burst
    always @ (*)
    begin
        lower_wrap_boundary_byte_rd_addr = (int_byte_rd_addr & int_boundary_byte_rd_addr_mask);
        lower_wrap_boundary_byte_wr_addr = (int_byte_wr_addr & int_boundary_byte_wr_addr_mask);
        upper_wrap_boundary_byte_rd_addr = (int_byte_rd_addr & int_boundary_byte_rd_addr_mask) + int_boundary_byte_rd_addr_increment;
        upper_wrap_boundary_byte_wr_addr = (int_byte_wr_addr & int_boundary_byte_wr_addr_mask) + int_boundary_byte_wr_addr_increment;
    end
    
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            lower_wrap_boundary_byte_addr <= 0;
            upper_wrap_boundary_byte_addr <= 0;
        end
        else
        begin
            if (int_wr_grant)
            begin
                lower_wrap_boundary_byte_addr <= lower_wrap_boundary_byte_wr_addr;
                upper_wrap_boundary_byte_addr <= upper_wrap_boundary_byte_wr_addr;
            end
            else if (int_rd_grant)
            begin
                lower_wrap_boundary_byte_addr <= lower_wrap_boundary_byte_rd_addr;
                upper_wrap_boundary_byte_addr <= upper_wrap_boundary_byte_rd_addr;
            end
        end
    end
    
    // Byte address counter, to count up for a AXI command with len larger than 1
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            byte_addr_counter <= 0;
        end
        else
        begin
            if (int_grant)
            begin
                byte_addr_counter <= aligned_byte_addr + int_size;
            end
            else if (itf_cmd_ready)
            begin
                byte_addr_counter <= byte_addr + current_size;
            end
        end
    end
    
    // Determine whether current burst reached upper wrap boundary
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            upper_boundary_reached <= 1'b0;
        end
        else
        begin
            if (int_rd_grant)
            begin
                if (aligned_byte_rd_addr [(2 ** AXI_SIZE_WIDTH) + 3 : 0] == upper_wrap_boundary_byte_rd_addr [(2 ** AXI_SIZE_WIDTH) + 3 : 0])
                begin
                    upper_boundary_reached <= 1'b1;
                end
                else
                begin
                    upper_boundary_reached <= 1'b0;
                end
            end
            else if (int_wr_grant)
            begin
                if (aligned_byte_wr_addr [(2 ** AXI_SIZE_WIDTH) + 3 : 0] == upper_wrap_boundary_byte_wr_addr [(2 ** AXI_SIZE_WIDTH) + 3 : 0])
                begin
                    upper_boundary_reached <= 1'b1;
                end
                else
                begin
                    upper_boundary_reached <= 1'b0;
                end
            end
            else if (itf_cmd_ready)
            begin
                if (byte_addr [(2 ** AXI_SIZE_WIDTH) + 3 : 0] == upper_wrap_boundary_byte_addr [(2 ** AXI_SIZE_WIDTH) + 3 : 0])
                begin
                    upper_boundary_reached <= 1'b1;
                end
                else
                begin
                    upper_boundary_reached <= 1'b0;
                end
            end
        end
    end
    
    // Byte address for wrapping burst command
    always @ (*)
    begin
        if (burst_type == 2'd2 && upper_boundary_reached)
        begin
            byte_addr = lower_wrap_boundary_byte_addr;
        end
        else
        begin
            byte_addr = byte_addr_counter;
        end
    end
    
    // Byte to word address conversion logic
    always @ (*)
    begin
        word_rd_addr         = 0;
        word_wr_addr         = 0;
        registered_word_addr = 0;
        
        word_rd_addr         = int_byte_rd_addr [AXI_ADDR_WIDTH - 1 : WORD_ADDR_OFFSET];
        word_wr_addr         = int_byte_wr_addr [AXI_ADDR_WIDTH - 1 : WORD_ADDR_OFFSET];
        registered_word_addr = byte_addr        [AXI_ADDR_WIDTH - 1 : WORD_ADDR_OFFSET];
    end
    
    // Command address to Avalon ST interface
    always @ (*)
    begin
        if (int_wr_grant)
        begin
            int_cmd_address = word_wr_addr;
        end
        else if (int_rd_grant)
        begin
            int_cmd_address = word_rd_addr;
        end
        else if (int_doing_split)
        begin
            int_cmd_address = registered_word_addr;
        end
        else
        begin
            int_cmd_address = 0;
        end
    end
    
    //--------------------------------------------------------------------------
    // Command related logics
    //--------------------------------------------------------------------------
    assign rd_req  = int_arvalid & int_arready;
    assign wr_req  = int_awvalid & int_awready;
    assign cmd_req = {rd_req, wr_req};
    
    assign {rd_grant, wr_grant} = cmd_grant;
    
    // Command arbitration logic
    generate
    begin
        if (COMMAND_ARB_TYPE == "ROUND_ROBIN")
        begin
            always @ (*)
            begin
                if (&cmd_req) // both command requesting at the same time
                begin
                    cmd_grant = prev_cmd_grant;
                end
                else
                begin
                    cmd_grant = cmd_req;
                end
            end
        end
        else if (COMMAND_ARB_TYPE == "WRITE_PRIORITY")
        begin
            always @ (*)
            begin
                if (&cmd_req) // both command requesting at the same time
                begin
                    cmd_grant = 2'b01; // make sure we always grant write request
                end
                else
                begin
                    cmd_grant = cmd_req;
                end
            end
        end
        else if (COMMAND_ARB_TYPE == "READ_PRIORITY")
        begin
            always @ (*)
            begin
                if (&cmd_req) // both command requesting at the same time
                begin
                    cmd_grant = 2'b10; // make sure we always grant read request
                end
                else
                begin
                    cmd_grant = cmd_req;
                end
            end
        end
    end
    endgenerate
    
    // Previous command grant
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            prev_cmd_grant <= 2'b01; // default round robin priority
        end
        else
        begin
            if (|cmd_grant)
            begin
                prev_cmd_grant <= ~cmd_grant;
            end
        end
    end
    
    // Grant signal for registered output
    generate
    begin
        if (REGISTERED)
        begin
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    int_grant       <= 1'b0;
                    int_wr_grant    <= 1'b0;
                    int_rd_grant    <= 1'b0;
                    int_doing_split <= 1'b0;
                end
                else
                begin
                    if (itf_cmd_ready)
                    begin
                        int_grant       <= wr_grant | rd_grant;
                        int_wr_grant    <= wr_grant;
                        int_rd_grant    <= rd_grant;
                        int_doing_split <= doing_split;
                    end
                end
            end
        end
        else
        begin
            always @ (*)
            begin
                int_grant       = wr_grant | rd_grant;
                int_wr_grant    = wr_grant;
                int_rd_grant    = rd_grant;
                int_doing_split = doing_split;
            end
        end
    end
    endgenerate
    
    // Doing write logic, indicate what we did last
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            doing_write <= 1'b0;
        end
        else
        begin
            if (wr_grant)
            begin
                doing_write <= 1'b1;
            end
            else if (rd_grant)
            begin
                doing_write <= 1'b0;
            end
        end
    end
    
    // Command & valid to Avalon ST interface
    always @ (*)
    begin
        if (wr_grant)
        begin
            int_cmd       = 1'b1; // Set command to '1' when there is a write, else '0'
            int_cmd_valid = 1'b1;
        end
        else if (rd_grant)
        begin
            int_cmd       = 1'b0; // Set command to '1' when there is a write, else '0'
            int_cmd_valid = 1'b1;
        end
        else if (doing_split)
        begin
            int_cmd       = doing_write;
            int_cmd_valid = 1'b1;
        end
        else
        begin
            int_cmd       = 1'b0;
            int_cmd_valid = 1'b0;
        end
    end
    
    //--------------------------------------------------------------------------
    // Burst related logics
    //--------------------------------------------------------------------------
    // Splitting logic
    // we will split AXI command into multiple smaller Avalon ST commands if size if not equal to data width
    // or when burst type is set to WRAP
    always @ (*)
    begin
        if (wr_grant)
        begin
            split_axi_cmd = ((int_awsize != NATIVE_AXI_SIZE || int_awburst == 2) && int_awlen != 0) ? 1'b1 : 1'b0; // don't need to split when size is '1'
        end
        else if (rd_grant)
        begin
            split_axi_cmd = ((int_arsize != NATIVE_AXI_SIZE || int_arburst == 2) && int_arlen != 0) ? 1'b1 : 1'b0; // don't need to split when size is '1'
        end
        else
        begin
            split_axi_cmd = 1'b0;
        end
    end
    
    // Splitting logic
    // to keep track of how many commands to split
    // also to tell other logic that it's currently doing split now
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            split_counter <= 0;
            doing_split   <= 1'b0;
        end
        else
        begin
            if (wr_grant)
            begin
                split_counter <= int_awlen;
                
                if (split_axi_cmd)
                begin
                    doing_split <= 1'b1;
                end
                else
                begin
                    doing_split <= 1'b0;
                end
            end
            else if (rd_grant)
            begin
                split_counter <= int_arlen;
                
                if (split_axi_cmd)
                begin
                    doing_split <= 1'b1;
                end
                else
                begin
                    doing_split <= 1'b0;
                end
            end
            else if (itf_cmd_ready)
            begin
                if (split_counter != {ST_SIZE_WIDTH{1'b0}})
                begin
                    split_counter <= split_counter - 1'b1;
                end
                
                if (split_counter == 1'b1)
                begin
                    doing_split <= 1'b0;
                end
            end
        end
    end
    
    // Convert AXI len to Avalon ST burst size
    always @ (*)
    begin
        rd_burstlen = int_arlen + 1'b1;
        wr_burstlen = int_awlen + 1'b1;
    end
    
    // Burst size to Avalon ST interface
    always @ (*)
    begin
        if (wr_grant)
        begin
            int_cmd_burstlen = (split_axi_cmd) ? 1'b1: wr_burstlen;
        end
        else if (rd_grant)
        begin
            int_cmd_burstlen = (split_axi_cmd) ? 1'b1: rd_burstlen;
        end
        else if (doing_split)
        begin
            int_cmd_burstlen = 1'b1; // Size will always equal to '1' during spliting process
        end
        else
        begin
            int_cmd_burstlen = 0;
        end
    end
    
    // Burst type
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            burst_type <= 0;
        end
        else
        begin
            if (wr_grant)
            begin
                burst_type <= int_awburst;
            end
            else if (rd_grant)
            begin
                burst_type <= int_arburst;
            end
        end
    end
    
    //--------------------------------------------------------------------------
    // ID related logics
    //--------------------------------------------------------------------------
    assign rd_id = int_arid;
    assign wr_id = int_awid;
    
    // Registered ID, keep track of previous ID
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            registered_id <= 0;
        end
        else
        begin
            if (wr_grant)
            begin
                registered_id <= wr_id;
            end
            else if (rd_grant)
            begin
                registered_id <= rd_id;
            end
        end
    end
    
    // Transfer AXI ID to Avalon ST interface
    always @ (*)
    begin
        if (wr_grant)
        begin
            int_cmd_id = wr_id;
        end
        else if (rd_grant)
        begin
            int_cmd_id = rd_id;
        end
        else if (doing_split)
        begin
            int_cmd_id = registered_id;
        end
        else
        begin
            int_cmd_id = 0;
        end
    end
    
    //--------------------------------------------------------------------------
    // Others
    //--------------------------------------------------------------------------
    // Setting inband signals to '0'
    always @ (*)
    begin
        int_cmd_priority      = zero;
        int_cmd_autoprecharge = zero;
        int_cmd_multicast     = zero;
    end
    
    //--------------------------------------------------------------------------
    // Outputs
    //--------------------------------------------------------------------------
    // AXI command output assignment
    always @ (*)
    begin
        // disable ready signal during split or when FIFO is full
        int_awready = itf_cmd_ready & ~doing_split & wr_cmd_fifo_ready;
        int_arready = itf_cmd_ready & ~doing_split & rd_cmd_fifo_ready;
    end
    
    always @ (*)
    begin
        // disable ready signal when granting other channel
        awready = int_awready & ~rd_grant;
        arready = int_arready & ~wr_grant;
    end
    
    // Avalon ST command output assignment
    generate
    begin
        if (REGISTERED)
        begin
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    itf_cmd_valid         <= 0;
                    itf_cmd               <= 0;
                    itf_cmd_burstlen      <= 0;
                    itf_cmd_id            <= 0;
                    itf_cmd_priority      <= 0;
                    itf_cmd_autoprecharge <= 0;
                    itf_cmd_multicast     <= 0;
                end
                else
                begin
                    if (itf_cmd_ready)
                    begin
                        itf_cmd_valid         <= int_cmd_valid;
                        itf_cmd               <= int_cmd;
                        itf_cmd_burstlen      <= int_cmd_burstlen;
                        itf_cmd_id            <= int_cmd_id;
                        itf_cmd_priority      <= int_cmd_priority;
                        itf_cmd_autoprecharge <= int_cmd_autoprecharge;
                        itf_cmd_multicast     <= int_cmd_multicast;
                    end
                end
            end
            
            always @ (*)
            begin
                itf_cmd_address       = int_cmd_address;
            end
        end
        else
        begin
            always @ (*)
            begin
                itf_cmd_valid         = int_cmd_valid;
                itf_cmd               = int_cmd;
                itf_cmd_address       = int_cmd_address;
                itf_cmd_burstlen      = int_cmd_burstlen;
                itf_cmd_id            = int_cmd_id;
                itf_cmd_priority      = int_cmd_priority;
                itf_cmd_autoprecharge = int_cmd_autoprecharge;
                itf_cmd_multicast     = int_cmd_multicast;
            end
        end
    end
    endgenerate
    
//--------------------------------------------------------------------------------------------------------
//
//  [END] Command Channel
//
//--------------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------------
//
//  [START] Write Data Channel
//
//--------------------------------------------------------------------------------------------------------
    //--------------------------------------------------------------------------
    // Write data begin logic
    //--------------------------------------------------------------------------
    // Determine write data begin based on write data last information
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            write_data_begin_n <= 1'b0;
        end
        else
        begin
            if (wlast & wready)
            begin
                write_data_begin_n <= 1'b0;
            end
            else if (wvalid & wready) // received a write data packet
            begin
                write_data_begin_n <= 1'b1;
            end
        end
    end
    
    // Write data begin logic
    always @ (*)
    begin
        if (wvalid & wready)
        begin
            int_wr_data_begin = ~write_data_begin_n;
        end
        else
        begin
            int_wr_data_begin = 1'b0;
        end
    end
    
    //--------------------------------------------------------------------------
    // Others
    //--------------------------------------------------------------------------
    always @ (*)
    begin
        int_wr_data_valid   = wvalid & wready;
        int_wr_data         = wdata;
        int_wr_data_byte_en = wstrb;
        int_wr_data_last    = wlast & wready;
        int_wr_data_id      = wid;
    end
    
    //--------------------------------------------------------------------------
    // Outputs
    //--------------------------------------------------------------------------
    // AXI write data channel output assignment
    always @ (*)
    begin
        wready = itf_wr_data_ready & wr_data_fifo_ready;
    end
    
    // Avalon ST data chanel output assignment
    generate
    begin
        if (REGISTERED)
        begin
            always @ (posedge ctl_clk or negedge ctl_reset_n)
            begin
                if (!ctl_reset_n)
                begin
                    itf_wr_data_valid   <= 0;
                    itf_wr_data         <= 0;
                    itf_wr_data_byte_en <= 0;
                    itf_wr_data_begin   <= 0;
                    itf_wr_data_last    <= 0;
                    itf_wr_data_id      <= 0;
                end
                else
                begin
                    if (itf_wr_data_ready)
                    begin
                        itf_wr_data_valid   <= int_wr_data_valid;
                        itf_wr_data         <= int_wr_data;
                        itf_wr_data_byte_en <= int_wr_data_byte_en;
                        itf_wr_data_begin   <= int_wr_data_begin;
                        itf_wr_data_last    <= int_wr_data_last;
                        itf_wr_data_id      <= int_wr_data_id;
                    end
                end
            end
        end
        else
        begin
            always @ (*)
            begin
                itf_wr_data_valid   = int_wr_data_valid;
                itf_wr_data         = int_wr_data;
                itf_wr_data_byte_en = int_wr_data_byte_en;
                itf_wr_data_begin   = int_wr_data_begin;
                itf_wr_data_last    = int_wr_data_last;
                itf_wr_data_id      = int_wr_data_id;
            end
        end
    end
    endgenerate
    
//--------------------------------------------------------------------------------------------------------
//
//  [END] Write Data Channel
//
//--------------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------------
//
//  [START] Write Response Channel
//
//--------------------------------------------------------------------------------------------------------
    assign wr_cmd_fifo_write             = wr_grant;              // write into FIFO after receiving write request
    assign wr_cmd_fifo_write_data        = wr_id;
    assign wr_cmd_fifo_read              = id_matched;            // pop from FIFO when ID from both FIFO is matching
    assign wr_cmd_fifo_read_data_valid   = ~wr_cmd_fifo_empty;
    
    assign wr_data_fifo_write            = int_wr_data_last;      // write into FIFO after receiving last data
    assign wr_data_fifo_write_data       = int_wr_data_id;
    assign wr_data_fifo_read             = id_matched;            // pop from FIFO when ID from both FIFO is matching
    assign wr_data_fifo_read_data_valid  = ~wr_data_fifo_empty;
    
    // FIFO instantiation to store write command information
    scfifo # (
        .add_ram_output_register    ("ON"                          ),
        .intended_device_family     ("Stratix IV"                  ),
        .lpm_numwords               (WR_CMD_FIFO_DEPTH             ),
        .lpm_showahead              ("ON"                          ),
        .lpm_type                   ("scfifo"                      ),
        .lpm_width                  (WR_CMD_FIFO_DATA_WIDTH        ),
        .lpm_widthu                 (WR_CMD_FIFO_ADDR_WIDTH        ),
        .overflow_checking          ("OFF"                         ),
        .underflow_checking         ("OFF"                         ),
        .use_eab                    ("ON"                          ),
        .almost_full_value          (WR_CMD_FIFO_DEPTH - 1         )
    ) wr_cmd_fifo (
        .aclr                       (~ctl_reset_n                  ),
        .clock                      (ctl_clk                       ),
        .data                       (wr_cmd_fifo_write_data        ),
        .rdreq                      (wr_cmd_fifo_read              ),
        .wrreq                      (wr_cmd_fifo_write             ),
        .empty                      (wr_cmd_fifo_empty             ),
        .full                       (                              ),
        .q                          (wr_cmd_fifo_read_data         ),
        .almost_empty               (                              ),
        .almost_full                (wr_cmd_fifo_almost_full       ),
        .sclr                       (zero                          ),
        .usedw                      (                              )
    );
    
    // FIFO instantiation to store write data information
    scfifo # (
        .add_ram_output_register    ("ON"                          ),
        .intended_device_family     ("Stratix IV"                  ),
        .lpm_numwords               (WR_DATA_FIFO_DEPTH            ),
        .lpm_showahead              ("ON"                          ),
        .lpm_type                   ("scfifo"                      ),
        .lpm_width                  (WR_DATA_FIFO_DATA_WIDTH       ),
        .lpm_widthu                 (WR_DATA_FIFO_ADDR_WIDTH       ),
        .overflow_checking          ("OFF"                         ),
        .underflow_checking         ("OFF"                         ),
        .use_eab                    ("ON"                          ),
        .almost_full_value          (WR_DATA_FIFO_DEPTH - 1        )
    ) wr_data_fifo (
        .aclr                       (~ctl_reset_n                  ),
        .clock                      (ctl_clk                       ),
        .data                       (wr_data_fifo_write_data       ),
        .rdreq                      (wr_data_fifo_read             ),
        .wrreq                      (wr_data_fifo_write            ),
        .empty                      (wr_data_fifo_empty            ),
        .full                       (                              ),
        .q                          (wr_data_fifo_read_data        ),
        .almost_empty               (                              ),
        .almost_full                (wr_data_fifo_almost_full      ),
        .sclr                       (zero                          ),
        .usedw                      (                              )
    );
    
    // FIFO full logic
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            wr_cmd_fifo_ready  <= 1'b0;
            wr_data_fifo_ready <= 1'b0;
        end
        else
        begin
            // Set to '1' when either FIFO almost full, to prevent converter from accepting new commands/data
            wr_cmd_fifo_ready  <= ~wr_cmd_fifo_almost_full;
            wr_data_fifo_ready <= ~wr_data_fifo_almost_full;
        end
    end
    
    // ID macthing logic
    always @ (*)
    begin
        if (wr_cmd_fifo_read_data == wr_data_fifo_read_data && wr_cmd_fifo_read_data_valid && wr_data_fifo_read_data_valid)
        begin
            id_matched = 1'b1;
        end
        else
        begin
            id_matched = 1'b0;
        end
    end
    
    // Response logic
    // logic will keep valid high till response channel is ready to accept the command
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            int_bid    <= 0;
            int_bresp  <= 0;
            int_bvalid <= 1'b0;
        end
        else
        begin
            if (id_matched)
            begin
                int_bid    <= wr_cmd_fifo_read_data;
                int_bresp  <= 0;
                int_bvalid <= 1'b1;
            end
            else if (bready)
            begin
                int_bid    <= 0;
                int_bresp  <= 0;
                int_bvalid <= 0;
            end
        end
    end
    
    // AXI write response channel output assignment
    always @ (*)
    begin
        bid    = int_bid;
        bresp  = int_bresp;
        bvalid = int_bvalid;
    end
//--------------------------------------------------------------------------------------------------------
//
//  [END] Write Response Channel
//
//--------------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------------
//
//  [START] Read Data Channel
//
//--------------------------------------------------------------------------------------------------------
    assign rd_cmd_fifo_write             = rd_grant;                // write into FIFO after receiving read request
    assign rd_cmd_fifo_write_data        = rd_burstlen;
    assign rd_cmd_fifo_read              = read_data_last;          // pop from FIFO after sending last read data
    assign rd_cmd_fifo_read_data_valid   = ~rd_cmd_fifo_empty;
    
    // FIFO to store read command information
    scfifo # (
        .add_ram_output_register    ("ON"                          ),
        .intended_device_family     ("Stratix IV"                  ),
        .lpm_numwords               (RD_CMD_FIFO_DEPTH             ),
        .lpm_showahead              ("ON"                          ),
        .lpm_type                   ("scfifo"                      ),
        .lpm_width                  (RD_CMD_FIFO_DATA_WIDTH        ),
        .lpm_widthu                 (RD_CMD_FIFO_ADDR_WIDTH        ),
        .overflow_checking          ("OFF"                         ),
        .underflow_checking         ("OFF"                         ),
        .use_eab                    ("ON"                          ),
        .almost_full_value          (RD_CMD_FIFO_DEPTH - 1         )
    ) rd_cmd_fifo (
        .aclr                       (~ctl_reset_n                  ),
        .clock                      (ctl_clk                       ),
        .data                       (rd_cmd_fifo_write_data        ),
        .rdreq                      (rd_cmd_fifo_read              ),
        .wrreq                      (rd_cmd_fifo_write             ),
        .empty                      (rd_cmd_fifo_empty             ),
        .full                       (                              ),
        .q                          (rd_cmd_fifo_read_data         ),
        .almost_empty               (                              ),
        .almost_full                (rd_cmd_fifo_almost_full       ),
        .sclr                       (zero                          ),
        .usedw                      (                              )
    );
    
    // FIFO full logic
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            rd_cmd_fifo_ready  <= 1'b0;
        end
        else
        begin
            // Set to '1' when either FIFO almost full, to prevent converter from accepting new commands/data
            rd_cmd_fifo_ready  <= ~rd_cmd_fifo_almost_full;
        end
    end
    
    // Read data counter
    // keep tracks of read data count, to be used in read data last logic
    always @ (posedge ctl_clk or negedge ctl_reset_n)
    begin
        if (!ctl_reset_n)
        begin
            read_data_counter <= 1;
        end
        else
        begin
            if (int_rvalid && int_rd_data_ready && read_data_last) // reset counter value after reaching last burst
            begin
                read_data_counter <= 1;
            end
            else if (int_rvalid && int_rd_data_ready) // count up when there is a read data begin transfered
            begin
                read_data_counter <= read_data_counter + 1'b1;
            end
        end
    end
    
    // Read data last logic
    // indicate which read data is the last burst
    always @ (*)
    begin
        if (int_rvalid && int_rd_data_ready && read_data_counter == rd_cmd_fifo_read_data) // set last to '1' when counter matches FIFO output
        begin
            read_data_last = 1'b1;
        end
        else
        begin
            read_data_last = 1'b0;
        end
    end
    
    // Others
    always @ (*)
    begin
        int_rid           = itf_rd_data_id;
        int_rdata         = itf_rd_data;
        int_rresp         = {itf_rd_data_error, 1'b0}; // If there is an error, it will indicate SLVERR on AXI interface
        int_rlast         = read_data_last;
        int_rvalid        = itf_rd_data_valid;
        
        int_rd_data_ready = rready;
    end
    
    // AXI read data channel output assignment
    always @ (*)
    begin
        rid    = int_rid;
        rdata  = int_rdata;
        rresp  = int_rresp;
        rlast  = int_rlast;
        rvalid = int_rvalid;
    end
    
    // Avalon ST read data channel output assignment
    always @ (*)
    begin
        itf_rd_data_ready = int_rd_data_ready;
    end
    
//--------------------------------------------------------------------------------------------------------
//
//  [END] Read Data Channel
//
//--------------------------------------------------------------------------------------------------------

function integer log2;
    input [31 : 0] value;
    integer        i;
    begin
        log2 = 0;
        for(i = 0;2 ** i < value;i = i + 1)
        begin
            log2 = i + 1;
        end
    end
endfunction

function integer log2_minus_one;
    input [31 : 0] value;
    integer        i;
    begin
        log2_minus_one = 1;
        for(i = 0;2 ** i < value;i = i + 1)
        begin
            log2_minus_one = i + 1;
        end
    end
endfunction

endmodule
