
module nios_tester (
	audio_in_data,
	audio_in_valid,
	audio_in_ready,
	audio_out_data,
	audio_out_valid,
	audio_out_ready,
	dummy_export,
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
	jtag0_jtag_tck,
	jtag0_jtag_tms,
	jtag0_jtag_tdi,
	jtag0_jtag_tdo,
	jtag1_jtag_tck,
	jtag1_jtag_tms,
	jtag1_jtag_tdi,
	jtag1_jtag_tdo,
	mem_mem_req_address,
	mem_mem_req_byte_en,
	mem_mem_req_read_writen,
	mem_mem_req_request,
	mem_mem_req_tag,
	mem_mem_req_wdata,
	mem_mem_resp_dack_tag,
	mem_mem_resp_data,
	mem_mem_resp_rack_tag,
	pio_in_port,
	pio_out_port,
	spi_MISO,
	spi_MOSI,
	spi_SCLK,
	spi_SS_n,
	sys_clock_clk,
	sys_reset_reset_n,
	uart_rxd,
	uart_txd,
	uart_cts_n,
	uart_rts_n,
	jtag_in_data,
	jtag_in_valid,
	jtag_in_ready);	

	input	[31:0]	audio_in_data;
	input		audio_in_valid;
	output		audio_in_ready;
	output	[31:0]	audio_out_data;
	output		audio_out_valid;
	input		audio_out_ready;
	input		dummy_export;
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
	output		jtag0_jtag_tck;
	output		jtag0_jtag_tms;
	output		jtag0_jtag_tdi;
	input		jtag0_jtag_tdo;
	output		jtag1_jtag_tck;
	output		jtag1_jtag_tms;
	output		jtag1_jtag_tdi;
	input		jtag1_jtag_tdo;
	output	[25:0]	mem_mem_req_address;
	output	[3:0]	mem_mem_req_byte_en;
	output		mem_mem_req_read_writen;
	output		mem_mem_req_request;
	output	[7:0]	mem_mem_req_tag;
	output	[31:0]	mem_mem_req_wdata;
	input	[7:0]	mem_mem_resp_dack_tag;
	input	[31:0]	mem_mem_resp_data;
	input	[7:0]	mem_mem_resp_rack_tag;
	input	[31:0]	pio_in_port;
	output	[31:0]	pio_out_port;
	input		spi_MISO;
	output		spi_MOSI;
	output		spi_SCLK;
	output		spi_SS_n;
	input		sys_clock_clk;
	input		sys_reset_reset_n;
	input		uart_rxd;
	output		uart_txd;
	input		uart_cts_n;
	output		uart_rts_n;
	input	[7:0]	jtag_in_data;
	input		jtag_in_valid;
	output		jtag_in_ready;
endmodule
