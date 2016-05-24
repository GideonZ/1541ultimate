
module nios (
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
	memory_mem_a,
	memory_mem_ba,
	memory_mem_ck,
	memory_mem_ck_n,
	memory_mem_cke,
	memory_mem_cs_n,
	memory_mem_dm,
	memory_mem_ras_n,
	memory_mem_cas_n,
	memory_mem_we_n,
	memory_mem_dq,
	memory_mem_dqs,
	memory_mem_dqs_n,
	memory_mem_odt,
	oct_rzqin,
	pio_in_port,
	pio_out_port,
	reset_reset_n,
	status_local_init_done,
	status_local_cal_success,
	status_local_cal_fail,
	sys_clock_clk,
	sys_reset_reset_n);	

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
	output	[13:0]	memory_mem_a;
	output	[1:0]	memory_mem_ba;
	output	[0:0]	memory_mem_ck;
	output	[0:0]	memory_mem_ck_n;
	output	[0:0]	memory_mem_cke;
	output	[0:0]	memory_mem_cs_n;
	output	[0:0]	memory_mem_dm;
	output	[0:0]	memory_mem_ras_n;
	output	[0:0]	memory_mem_cas_n;
	output	[0:0]	memory_mem_we_n;
	inout	[7:0]	memory_mem_dq;
	inout	[0:0]	memory_mem_dqs;
	inout	[0:0]	memory_mem_dqs_n;
	output	[0:0]	memory_mem_odt;
	input		oct_rzqin;
	input	[31:0]	pio_in_port;
	output	[31:0]	pio_out_port;
	input		reset_reset_n;
	output		status_local_init_done;
	output		status_local_cal_success;
	output		status_local_cal_fail;
	output		sys_clock_clk;
	output		sys_reset_reset_n;
endmodule
