	nios_solo u0 (
		.clk_clk                 (<connected-to-clk_clk>),                 //    clk.clk
		.io_ack                  (<connected-to-io_ack>),                  //     io.ack
		.io_rdata                (<connected-to-io_rdata>),                //       .rdata
		.io_read                 (<connected-to-io_read>),                 //       .read
		.io_wdata                (<connected-to-io_wdata>),                //       .wdata
		.io_write                (<connected-to-io_write>),                //       .write
		.io_address              (<connected-to-io_address>),              //       .address
		.io_irq                  (<connected-to-io_irq>),                  //       .irq
		.io_u2p_ack              (<connected-to-io_u2p_ack>),              // io_u2p.ack
		.io_u2p_rdata            (<connected-to-io_u2p_rdata>),            //       .rdata
		.io_u2p_read             (<connected-to-io_u2p_read>),             //       .read
		.io_u2p_wdata            (<connected-to-io_u2p_wdata>),            //       .wdata
		.io_u2p_write            (<connected-to-io_u2p_write>),            //       .write
		.io_u2p_address          (<connected-to-io_u2p_address>),          //       .address
		.io_u2p_irq              (<connected-to-io_u2p_irq>),              //       .irq
		.mem_mem_req_address     (<connected-to-mem_mem_req_address>),     //    mem.mem_req_address
		.mem_mem_req_byte_en     (<connected-to-mem_mem_req_byte_en>),     //       .mem_req_byte_en
		.mem_mem_req_read_writen (<connected-to-mem_mem_req_read_writen>), //       .mem_req_read_writen
		.mem_mem_req_request     (<connected-to-mem_mem_req_request>),     //       .mem_req_request
		.mem_mem_req_tag         (<connected-to-mem_mem_req_tag>),         //       .mem_req_tag
		.mem_mem_req_wdata       (<connected-to-mem_mem_req_wdata>),       //       .mem_req_wdata
		.mem_mem_resp_dack_tag   (<connected-to-mem_mem_resp_dack_tag>),   //       .mem_resp_dack_tag
		.mem_mem_resp_data       (<connected-to-mem_mem_resp_data>),       //       .mem_resp_data
		.mem_mem_resp_rack_tag   (<connected-to-mem_mem_resp_rack_tag>),   //       .mem_resp_rack_tag
		.reset_reset_n           (<connected-to-reset_reset_n>),           //  reset.reset_n
		.dummy_export            (<connected-to-dummy_export>)             //  dummy.export
	);

