
module nios_solo (
	clk_clk,
	io_ack,
	io_rdata,
	io_read,
	io_wdata,
	io_write,
	io_address,
	io_irq,
	io_u2p_ack,
	io_u2p_rdata,
	io_u2p_read,
	io_u2p_wdata,
	io_u2p_write,
	io_u2p_address,
	io_u2p_irq,
	mem_mem_req_address,
	mem_mem_req_byte_en,
	mem_mem_req_read_writen,
	mem_mem_req_request,
	mem_mem_req_tag,
	mem_mem_req_wdata,
	mem_mem_resp_dack_tag,
	mem_mem_resp_data,
	mem_mem_resp_rack_tag,
	reset_reset_n,
	dummy_export);	

	input		clk_clk;
	input		io_ack;
	input	[7:0]	io_rdata;
	output		io_read;
	output	[7:0]	io_wdata;
	output		io_write;
	output	[19:0]	io_address;
	input		io_irq;
	input		io_u2p_ack;
	input	[7:0]	io_u2p_rdata;
	output		io_u2p_read;
	output	[7:0]	io_u2p_wdata;
	output		io_u2p_write;
	output	[19:0]	io_u2p_address;
	input		io_u2p_irq;
	output	[25:0]	mem_mem_req_address;
	output	[3:0]	mem_mem_req_byte_en;
	output		mem_mem_req_read_writen;
	output		mem_mem_req_request;
	output	[7:0]	mem_mem_req_tag;
	output	[31:0]	mem_mem_req_wdata;
	input	[7:0]	mem_mem_resp_dack_tag;
	input	[31:0]	mem_mem_resp_data;
	input	[7:0]	mem_mem_resp_rack_tag;
	input		reset_reset_n;
	input		dummy_export;
endmodule
