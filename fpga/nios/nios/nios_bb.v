
module nios (
	altmemddr_0_auxfull_clk,
	altmemddr_0_auxhalf_clk,
	clk50_clk,
	io_ack,
	io_rdata,
	io_read,
	io_wdata,
	io_write,
	io_address,
	io_irq,
	mem32_address,
	mem32_direction,
	mem32_byte_en,
	mem32_wdata,
	mem32_request,
	mem32_tag,
	mem32_dack_tag,
	mem32_rdata,
	mem32_rack,
	mem32_rack_tag,
	mem_external_local_refresh_ack,
	mem_external_local_init_done,
	mem_external_reset_phy_clk_n,
	memory_mem_odt,
	memory_mem_clk,
	memory_mem_clk_n,
	memory_mem_cs_n,
	memory_mem_cke,
	memory_mem_addr,
	memory_mem_ba,
	memory_mem_ras_n,
	memory_mem_cas_n,
	memory_mem_we_n,
	memory_mem_dq,
	memory_mem_dqs,
	memory_mem_dm,
	pio_in_port,
	pio_out_port,
	reset_reset_n,
	sys_clock_clk,
	sys_reset_reset_n);	

	output		altmemddr_0_auxfull_clk;
	output		altmemddr_0_auxhalf_clk;
	input		clk50_clk;
	input		io_ack;
	input	[7:0]	io_rdata;
	output		io_read;
	output	[7:0]	io_wdata;
	output		io_write;
	output	[19:0]	io_address;
	input		io_irq;
	input	[25:0]	mem32_address;
	input		mem32_direction;
	input	[3:0]	mem32_byte_en;
	input	[31:0]	mem32_wdata;
	input		mem32_request;
	input	[7:0]	mem32_tag;
	output	[7:0]	mem32_dack_tag;
	output	[31:0]	mem32_rdata;
	output		mem32_rack;
	output	[7:0]	mem32_rack_tag;
	output		mem_external_local_refresh_ack;
	output		mem_external_local_init_done;
	output		mem_external_reset_phy_clk_n;
	output	[0:0]	memory_mem_odt;
	inout	[0:0]	memory_mem_clk;
	inout	[0:0]	memory_mem_clk_n;
	output	[0:0]	memory_mem_cs_n;
	output	[0:0]	memory_mem_cke;
	output	[13:0]	memory_mem_addr;
	output	[1:0]	memory_mem_ba;
	output		memory_mem_ras_n;
	output		memory_mem_cas_n;
	output		memory_mem_we_n;
	inout	[7:0]	memory_mem_dq;
	inout	[0:0]	memory_mem_dqs;
	output	[0:0]	memory_mem_dm;
	input	[31:0]	pio_in_port;
	output	[31:0]	pio_out_port;
	input		reset_reset_n;
	output		sys_clock_clk;
	output		sys_reset_reset_n;
endmodule
