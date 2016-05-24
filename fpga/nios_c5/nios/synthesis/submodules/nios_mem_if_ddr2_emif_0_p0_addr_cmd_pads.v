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

module nios_mem_if_ddr2_emif_0_p0_addr_cmd_pads(
    reset_n,
    reset_n_afi_clk,
    pll_afi_clk,
    pll_mem_clk,
    pll_mem_phy_clk,
    pll_afi_phy_clk,
    pll_c2p_write_clk,
    pll_write_clk,
    pll_hr_clk,
    phy_ddio_addr_cmd_clk,
    phy_ddio_address,
    dll_delayctrl_in,
    enable_mem_clk,
    phy_ddio_bank,
    phy_ddio_cs_n,
    phy_ddio_cke,
    phy_ddio_odt,
    phy_ddio_we_n,
    phy_ddio_ras_n,
    phy_ddio_cas_n,
    phy_mem_address,
    phy_mem_bank,
    phy_mem_cs_n,
    phy_mem_cke,
    phy_mem_odt,
    phy_mem_we_n,
    phy_mem_ras_n,
    phy_mem_cas_n,
    phy_mem_ck,
    phy_mem_ck_n
);

parameter DEVICE_FAMILY = "";
parameter MEM_ADDRESS_WIDTH     = ""; 
parameter MEM_BANK_WIDTH        = ""; 
parameter MEM_CHIP_SELECT_WIDTH = ""; 
parameter MEM_CLK_EN_WIDTH 		= ""; 
parameter MEM_CK_WIDTH 			= ""; 
parameter MEM_ODT_WIDTH 		= ""; 
parameter MEM_CONTROL_WIDTH     = ""; 

parameter AFI_ADDRESS_WIDTH         = ""; 
parameter AFI_BANK_WIDTH            = ""; 
parameter AFI_CHIP_SELECT_WIDTH     = ""; 
parameter AFI_CLK_EN_WIDTH 			= ""; 
parameter AFI_ODT_WIDTH 			= ""; 
parameter AFI_CONTROL_WIDTH         = ""; 
parameter DLL_WIDTH                 = "";
parameter REGISTER_C2P              = "";
parameter IS_HHP_HPS                = "";

input	reset_n;
input	reset_n_afi_clk;
input	pll_afi_clk;
input	pll_mem_clk;

input   pll_mem_phy_clk;
input   pll_afi_phy_clk;

input	pll_write_clk;
input	pll_hr_clk;
input	pll_c2p_write_clk;
input	phy_ddio_addr_cmd_clk;
input 	[DLL_WIDTH-1:0] dll_delayctrl_in;
input   [MEM_CK_WIDTH-1:0] enable_mem_clk;


input	[AFI_ADDRESS_WIDTH-1:0]	phy_ddio_address;



input   [AFI_BANK_WIDTH-1:0]    phy_ddio_bank;
input   [AFI_CHIP_SELECT_WIDTH-1:0] phy_ddio_cs_n;
input   [AFI_CLK_EN_WIDTH-1:0] phy_ddio_cke;
input   [AFI_ODT_WIDTH-1:0] phy_ddio_odt;
input   [AFI_CONTROL_WIDTH-1:0] phy_ddio_ras_n;
input   [AFI_CONTROL_WIDTH-1:0] phy_ddio_cas_n;
input   [AFI_CONTROL_WIDTH-1:0] phy_ddio_we_n;

output  [MEM_ADDRESS_WIDTH-1:0] phy_mem_address;
output  [MEM_BANK_WIDTH-1:0]    phy_mem_bank;
output  [MEM_CHIP_SELECT_WIDTH-1:0] phy_mem_cs_n;
output  [MEM_CLK_EN_WIDTH-1:0] phy_mem_cke;
output  [MEM_ODT_WIDTH-1:0] phy_mem_odt;
output  [MEM_CONTROL_WIDTH-1:0] phy_mem_we_n;
output  [MEM_CONTROL_WIDTH-1:0] phy_mem_ras_n;
output  [MEM_CONTROL_WIDTH-1:0] phy_mem_cas_n;
output  [MEM_CK_WIDTH-1:0]	phy_mem_ck;
output  [MEM_CK_WIDTH-1:0]	phy_mem_ck_n;


wire	[MEM_ADDRESS_WIDTH-1:0]	address_l;
wire	[MEM_ADDRESS_WIDTH-1:0]	address_h;
wire	adc_ldc_ck;
wire	[MEM_CHIP_SELECT_WIDTH-1:0] cs_n_l;
wire	[MEM_CHIP_SELECT_WIDTH-1:0] cs_n_h;
wire	[MEM_CLK_EN_WIDTH-1:0] cke_l;
wire	[MEM_CLK_EN_WIDTH-1:0] cke_h;



reg   [AFI_ADDRESS_WIDTH-1:0] phy_ddio_address_hr;
reg   [AFI_BANK_WIDTH-1:0] phy_ddio_bank_hr;
reg   [AFI_CHIP_SELECT_WIDTH-1:0] phy_ddio_cs_n_hr;
reg   [AFI_CLK_EN_WIDTH-1:0] phy_ddio_cke_hr;
reg   [AFI_ODT_WIDTH-1:0] phy_ddio_odt_hr;
reg   [AFI_CONTROL_WIDTH-1:0] phy_ddio_ras_n_hr;
reg   [AFI_CONTROL_WIDTH-1:0] phy_ddio_cas_n_hr;
reg   [AFI_CONTROL_WIDTH-1:0] phy_ddio_we_n_hr;

generate
if (REGISTER_C2P == "false") begin
	always @(*) begin
		phy_ddio_address_hr = phy_ddio_address;	
		phy_ddio_bank_hr = phy_ddio_bank;
		phy_ddio_cs_n_hr = phy_ddio_cs_n;
		phy_ddio_cke_hr = phy_ddio_cke;
		phy_ddio_odt_hr = phy_ddio_odt;
		phy_ddio_ras_n_hr = phy_ddio_ras_n;
		phy_ddio_cas_n_hr = phy_ddio_cas_n;
		phy_ddio_we_n_hr = phy_ddio_we_n;
	end
end else begin
	always @(posedge phy_ddio_addr_cmd_clk) begin
		phy_ddio_address_hr <= phy_ddio_address;	
		phy_ddio_bank_hr <= phy_ddio_bank;
		phy_ddio_cs_n_hr <= phy_ddio_cs_n;
		phy_ddio_cke_hr <= phy_ddio_cke;
		phy_ddio_odt_hr <= phy_ddio_odt;
		phy_ddio_ras_n_hr <= phy_ddio_ras_n;
		phy_ddio_cas_n_hr <= phy_ddio_cas_n;
		phy_ddio_we_n_hr <= phy_ddio_we_n;
	end
end
endgenerate	




wire	[MEM_ADDRESS_WIDTH-1:0]	phy_ddio_address_l;
wire	[MEM_ADDRESS_WIDTH-1:0]	phy_ddio_address_h;
wire	[MEM_BANK_WIDTH-1:0] phy_ddio_bank_l;
wire	[MEM_BANK_WIDTH-1:0] phy_ddio_bank_h;
wire	[MEM_CHIP_SELECT_WIDTH-1:0] phy_ddio_cs_n_l;
wire	[MEM_CHIP_SELECT_WIDTH-1:0] phy_ddio_cs_n_h;
wire	[MEM_CLK_EN_WIDTH-1:0] phy_ddio_cke_l;
wire	[MEM_CLK_EN_WIDTH-1:0] phy_ddio_cke_h;
wire	[MEM_ODT_WIDTH-1:0] phy_ddio_odt_l;
wire	[MEM_ODT_WIDTH-1:0] phy_ddio_odt_h;
wire	[MEM_CONTROL_WIDTH-1:0] phy_ddio_ras_n_l;
wire	[MEM_CONTROL_WIDTH-1:0] phy_ddio_ras_n_h;
wire	[MEM_CONTROL_WIDTH-1:0] phy_ddio_cas_n_l;
wire	[MEM_CONTROL_WIDTH-1:0] phy_ddio_cas_n_h;
wire	[MEM_CONTROL_WIDTH-1:0] phy_ddio_we_n_l;
wire	[MEM_CONTROL_WIDTH-1:0] phy_ddio_we_n_h;

// each signal has a high and a low portion,
// connecting to the high and low inputs of the DDIO_OUT,
// for the purpose of creating double data rate
	assign phy_ddio_address_l = phy_ddio_address_hr[MEM_ADDRESS_WIDTH-1:0];
	assign phy_ddio_bank_l = phy_ddio_bank_hr[MEM_BANK_WIDTH-1:0];
	assign phy_ddio_cke_l = phy_ddio_cke_hr[MEM_CLK_EN_WIDTH-1:0];
	assign phy_ddio_odt_l = phy_ddio_odt_hr[MEM_ODT_WIDTH-1:0];
	assign phy_ddio_cs_n_l = phy_ddio_cs_n_hr[MEM_CHIP_SELECT_WIDTH-1:0];
	assign phy_ddio_we_n_l = phy_ddio_we_n_hr[MEM_CONTROL_WIDTH-1:0];
	assign phy_ddio_ras_n_l = phy_ddio_ras_n_hr[MEM_CONTROL_WIDTH-1:0];
	assign phy_ddio_cas_n_l = phy_ddio_cas_n_hr[MEM_CONTROL_WIDTH-1:0];

	assign phy_ddio_address_h = phy_ddio_address_hr[2*MEM_ADDRESS_WIDTH-1:MEM_ADDRESS_WIDTH];
	assign phy_ddio_bank_h = phy_ddio_bank_hr[2*MEM_BANK_WIDTH-1:MEM_BANK_WIDTH];
	assign phy_ddio_cke_h = phy_ddio_cke_hr[2*MEM_CLK_EN_WIDTH-1:MEM_CLK_EN_WIDTH];
	assign phy_ddio_odt_h = phy_ddio_odt_hr[2*MEM_ODT_WIDTH-1:MEM_ODT_WIDTH];
	assign phy_ddio_cs_n_h = phy_ddio_cs_n_hr[2*MEM_CHIP_SELECT_WIDTH-1:MEM_CHIP_SELECT_WIDTH];
	assign phy_ddio_we_n_h = phy_ddio_we_n_hr[2*MEM_CONTROL_WIDTH-1:MEM_CONTROL_WIDTH];
	assign phy_ddio_ras_n_h = phy_ddio_ras_n_hr[2*MEM_CONTROL_WIDTH-1:MEM_CONTROL_WIDTH];
	assign phy_ddio_cas_n_h = phy_ddio_cas_n_hr[2*MEM_CONTROL_WIDTH-1:MEM_CONTROL_WIDTH];


	assign address_l = phy_ddio_address_l;
	assign address_h = phy_ddio_address_h;

    altddio_out	uaddress_pad(
		.aclr	    (~reset_n),
		.aset	    (1'b0),
		.datain_h   (address_l),
		.datain_l   (address_h),
		.dataout    (phy_mem_address),
		.oe	    	(1'b1),
		.outclock   (phy_ddio_addr_cmd_clk),
		.outclocken (1'b1)
    );

    defparam 
		uaddress_pad.extend_oe_disable = "UNUSED",
		uaddress_pad.intended_device_family = DEVICE_FAMILY,
		uaddress_pad.invert_output = "OFF",
		uaddress_pad.lpm_hint = "UNUSED",
		uaddress_pad.lpm_type = "altddio_out",
		uaddress_pad.oe_reg = "UNUSED",
		uaddress_pad.power_up_high = "OFF",
		uaddress_pad.width = MEM_ADDRESS_WIDTH;





    altddio_out	ubank_pad(
		.aclr	    (~reset_n),
		.aset	    (1'b0),
		.datain_h   (phy_ddio_bank_l),
		.datain_l   (phy_ddio_bank_h),
		.dataout    (phy_mem_bank),
		.oe	    	(1'b1),
		.outclock   (phy_ddio_addr_cmd_clk),
		.outclocken (1'b1)
    );

    defparam 
		ubank_pad.extend_oe_disable = "UNUSED",
		ubank_pad.intended_device_family = DEVICE_FAMILY,
		ubank_pad.invert_output = "OFF",
		ubank_pad.lpm_hint = "UNUSED",
		ubank_pad.lpm_type = "altddio_out",
		ubank_pad.oe_reg = "UNUSED",
		ubank_pad.power_up_high = "OFF",
		ubank_pad.width = MEM_BANK_WIDTH;

	assign cs_n_l = phy_ddio_cs_n_l;
	assign cs_n_h = phy_ddio_cs_n_h;

    altddio_out	ucs_n_pad(
		.aclr	    (1'b0),
		.aset	    (~reset_n),
		.datain_h   (cs_n_l),
		.datain_l   (cs_n_h),
		.dataout    (phy_mem_cs_n),
		.oe	    	(1'b1),
		.outclock   (phy_ddio_addr_cmd_clk),
		.outclocken (1'b1)
    );

    defparam 
		ucs_n_pad.extend_oe_disable = "UNUSED",
		ucs_n_pad.intended_device_family = DEVICE_FAMILY,
		ucs_n_pad.invert_output = "OFF",
		ucs_n_pad.lpm_hint = "UNUSED",
		ucs_n_pad.lpm_type = "altddio_out",
		ucs_n_pad.oe_reg = "UNUSED",
		ucs_n_pad.power_up_high = "OFF",
		ucs_n_pad.width = MEM_CHIP_SELECT_WIDTH;

	assign cke_l = phy_ddio_cke_l;
	assign cke_h = phy_ddio_cke_h;

    altddio_out ucke_pad(
        .aclr       (~reset_n),
        .aset       (1'b0),
        .datain_h   (cke_l),
        .datain_l   (cke_h),
        .dataout    (phy_mem_cke),
        .oe         (1'b1),
        .outclock   (phy_ddio_addr_cmd_clk),
        .outclocken (1'b1)
    );

    defparam
        ucke_pad.extend_oe_disable = "UNUSED",
        ucke_pad.intended_device_family = DEVICE_FAMILY,
        ucke_pad.invert_output = "OFF",
        ucke_pad.lpm_hint = "UNUSED",
        ucke_pad.lpm_type = "altddio_out",
        ucke_pad.oe_reg = "UNUSED",
        ucke_pad.power_up_high = "OFF",
        ucke_pad.width = MEM_CLK_EN_WIDTH;

    altddio_out uodt_pad(
        .aclr       (~reset_n),
        .aset       (1'b0),
        .datain_h   (phy_ddio_odt_l),
        .datain_l   (phy_ddio_odt_h),
        .dataout    (phy_mem_odt),
        .oe         (1'b1),
        .outclock   (phy_ddio_addr_cmd_clk),
        .outclocken (1'b1)
    );

    defparam
        uodt_pad.extend_oe_disable = "UNUSED",
        uodt_pad.intended_device_family = DEVICE_FAMILY,
        uodt_pad.invert_output = "OFF",
        uodt_pad.lpm_hint = "UNUSED",
        uodt_pad.lpm_type = "altddio_out",
        uodt_pad.oe_reg = "UNUSED",
        uodt_pad.power_up_high = "OFF",
        uodt_pad.width = MEM_ODT_WIDTH;

    altddio_out	uwe_n_pad(
		.aclr	    (1'b0),
		.aset	    (~reset_n),
		.datain_h   (phy_ddio_we_n_l),
		.datain_l   (phy_ddio_we_n_h),
		.dataout    (phy_mem_we_n),
		.oe	    	(1'b1),
		.outclock   (phy_ddio_addr_cmd_clk),
		.outclocken (1'b1)
    );

    defparam 
		uwe_n_pad.extend_oe_disable = "UNUSED",
		uwe_n_pad.intended_device_family = DEVICE_FAMILY,
		uwe_n_pad.invert_output = "OFF",
		uwe_n_pad.lpm_hint = "UNUSED",
		uwe_n_pad.lpm_type = "altddio_out",
		uwe_n_pad.oe_reg = "UNUSED",
		uwe_n_pad.power_up_high = "OFF",
		uwe_n_pad.width = MEM_CONTROL_WIDTH;


    altddio_out	uras_n_pad(
		.aclr	    (1'b0),
		.aset	    (~reset_n),
		.datain_h   (phy_ddio_ras_n_l),
		.datain_l   (phy_ddio_ras_n_h),
		.dataout    (phy_mem_ras_n),
		.oe	    	(1'b1),
		.outclock   (phy_ddio_addr_cmd_clk),
		.outclocken (1'b1)
    );

    defparam 
		uras_n_pad.extend_oe_disable = "UNUSED",
		uras_n_pad.intended_device_family = DEVICE_FAMILY,
		uras_n_pad.invert_output = "OFF",
		uras_n_pad.lpm_hint = "UNUSED",
		uras_n_pad.lpm_type = "altddio_out",
		uras_n_pad.oe_reg = "UNUSED",
		uras_n_pad.power_up_high = "OFF",
		uras_n_pad.width = MEM_CONTROL_WIDTH;



    altddio_out	ucas_n_pad(
		.aclr	    (1'b0),
		.aset	    (~reset_n),
		.datain_h   (phy_ddio_cas_n_l),
		.datain_l   (phy_ddio_cas_n_h),
		.dataout    (phy_mem_cas_n),
		.oe	    	(1'b1),
		.outclock   (phy_ddio_addr_cmd_clk),
		.outclocken (1'b1)
    );

    defparam 
		ucas_n_pad.extend_oe_disable = "UNUSED",
		ucas_n_pad.intended_device_family = DEVICE_FAMILY,
		ucas_n_pad.invert_output = "OFF",
		ucas_n_pad.lpm_hint = "UNUSED",
		ucas_n_pad.lpm_type = "altddio_out",
		ucas_n_pad.oe_reg = "UNUSED",
		ucas_n_pad.power_up_high = "OFF",
		ucas_n_pad.width = MEM_CONTROL_WIDTH;




  wire  [MEM_CK_WIDTH-1:0] mem_ck_source;
  wire	[MEM_CK_WIDTH-1:0] mem_ck;


localparam USE_ADDR_CMD_CPS_FOR_MEM_CK = "true";

generate
genvar clock_width;
    for (clock_width=0; clock_width<MEM_CK_WIDTH; clock_width=clock_width+1)
    begin: clock_gen




if(USE_ADDR_CMD_CPS_FOR_MEM_CK == "true")
begin
     nios_mem_if_ddr2_emif_0_p0_acv_ldc # (
     	.DLL_DELAY_CTRL_WIDTH(DLL_WIDTH),
     	.ADC_PHASE_SETTING(0),
     	.ADC_INVERT_PHASE("false"),
	.IS_HHP_HPS(IS_HHP_HPS)
     ) acv_ck_ldc (
     	.pll_hr_clk(pll_afi_phy_clk),
     	.pll_dq_clk(pll_write_clk),
     	.pll_dqs_clk (pll_mem_phy_clk),
     	.dll_phy_delayctrl (dll_delayctrl_in),
     	.adc_clk_cps (mem_ck_source[clock_width])
     );
end
else
begin
	wire [3:0] phy_clk_in;
	wire [3:0] phy_clk_out;
	assign phy_clk_in = {pll_afi_phy_clk,pll_write_clk,pll_mem_phy_clk,1'b0};
		
	cyclonev_phy_clkbuf phy_clkbuf (
		.inclk (phy_clk_in),
		.outclk (phy_clk_out)
	);

	wire [3:0] leveled_dqs_clocks;
	cyclonev_leveling_delay_chain leveling_delay_chain_dqs (
		.clkin (phy_clk_out[1]),
		.delayctrlin (dll_delayctrl_in),
		.clkout(leveled_dqs_clocks)
	);
	defparam leveling_delay_chain_dqs.physical_clock_source = "DQS";

	cyclonev_clk_phase_select clk_phase_select_dqs (
		`ifndef SIMGEN
		.clkin(leveled_dqs_clocks[0]),
		`else
		.clkin(leveled_dqs_clocks),
		`endif
		.clkout(mem_ck_source[clock_width])
	);
	defparam clk_phase_select_dqs.physical_clock_source = "DQS";
	defparam clk_phase_select_dqs.use_phasectrlin = "false";
	defparam clk_phase_select_dqs.phase_setting = 0;
end



    altddio_out umem_ck_pad(
    	.aclr       (1'b0),
    	.aset       (1'b0),
    	.datain_h   (enable_mem_clk[clock_width]),
    	.datain_l   (1'b0),
    	.dataout    (mem_ck[clock_width]),
    	.oe     	(1'b1),
    	.outclock   (mem_ck_source[clock_width]),
    	.outclocken (1'b1)
    );

    defparam
    	umem_ck_pad.extend_oe_disable = "UNUSED",
    	umem_ck_pad.intended_device_family = DEVICE_FAMILY,
    	umem_ck_pad.invert_output = "OFF",
    	umem_ck_pad.lpm_hint = "UNUSED",
    	umem_ck_pad.lpm_type = "altddio_out",
    	umem_ck_pad.oe_reg = "UNUSED",
    	umem_ck_pad.power_up_high = "OFF",
    	umem_ck_pad.width = 1;

	wire mem_ck_temp;

	assign mem_ck_temp = mem_ck[clock_width];

    nios_mem_if_ddr2_emif_0_p0_clock_pair_generator    uclk_generator(
        .datain     (mem_ck_temp),
        .dataout    (phy_mem_ck[clock_width]),
        .dataout_b  (phy_mem_ck_n[clock_width])
    );
	end
endgenerate


endmodule
