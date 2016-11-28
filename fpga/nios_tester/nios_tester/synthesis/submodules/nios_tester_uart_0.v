//Legal Notice: (C)2016 Altera Corporation. All rights reserved.  Your
//use of Altera Corporation's design tools, logic functions and other
//software and tools, and its AMPP partner logic functions, and any
//output files any of the foregoing (including device programming or
//simulation files), and any associated documentation or information are
//expressly subject to the terms and conditions of the Altera Program
//License Subscription Agreement or other applicable license agreement,
//including, without limitation, that your use is for the sole purpose
//of programming logic devices manufactured by Altera and sold by Altera
//or its authorized distributors.  Please refer to the applicable
//agreement for further details.

// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 

module nios_tester_uart_0_tx (
                               // inputs:
                                baud_divisor,
                                begintransfer,
                                clk,
                                clk_en,
                                do_force_break,
                                reset_n,
                                status_wr_strobe,
                                tx_data,
                                tx_wr_strobe,

                               // outputs:
                                tx_overrun,
                                tx_ready,
                                tx_shift_empty,
                                txd
                             )
;

  output           tx_overrun;
  output           tx_ready;
  output           tx_shift_empty;
  output           txd;
  input   [  9: 0] baud_divisor;
  input            begintransfer;
  input            clk;
  input            clk_en;
  input            do_force_break;
  input            reset_n;
  input            status_wr_strobe;
  input   [  7: 0] tx_data;
  input            tx_wr_strobe;

  reg              baud_clk_en;
  reg     [  9: 0] baud_rate_counter;
  wire             baud_rate_counter_is_zero;
  reg              do_load_shifter;
  wire             do_shift;
  reg              pre_txd;
  wire             shift_done;
  wire    [  9: 0] tx_load_val;
  reg              tx_overrun;
  reg              tx_ready;
  reg              tx_shift_empty;
  wire             tx_shift_reg_out;
  wire    [  9: 0] tx_shift_register_contents;
  wire             tx_wr_strobe_onset;
  reg              txd;
  wire    [  9: 0] unxshiftxtx_shift_register_contentsxtx_shift_reg_outxx6_in;
  reg     [  9: 0] unxshiftxtx_shift_register_contentsxtx_shift_reg_outxx6_out;
  assign tx_wr_strobe_onset = tx_wr_strobe && begintransfer;
  assign tx_load_val = {{1 {1'b1}},
    tx_data,
    1'b0};

  assign shift_done = ~(|tx_shift_register_contents);
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          do_load_shifter <= 0;
      else if (clk_en)
          do_load_shifter <= (~tx_ready) && shift_done;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          tx_ready <= 1'b1;
      else if (clk_en)
          if (tx_wr_strobe_onset)
              tx_ready <= 0;
          else if (do_load_shifter)
              tx_ready <= -1;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          tx_overrun <= 0;
      else if (clk_en)
          if (status_wr_strobe)
              tx_overrun <= 0;
          else if (~tx_ready && tx_wr_strobe_onset)
              tx_overrun <= -1;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          tx_shift_empty <= 1'b1;
      else if (clk_en)
          tx_shift_empty <= tx_ready && shift_done;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          baud_rate_counter <= 0;
      else if (clk_en)
          if (baud_rate_counter_is_zero || do_load_shifter)
              baud_rate_counter <= baud_divisor;
          else 
            baud_rate_counter <= baud_rate_counter - 1;
    end


  assign baud_rate_counter_is_zero = baud_rate_counter == 0;
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          baud_clk_en <= 0;
      else if (clk_en)
          baud_clk_en <= baud_rate_counter_is_zero;
    end


  assign do_shift = baud_clk_en  && 
    (~shift_done) && 
    (~do_load_shifter);

  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          pre_txd <= 1;
      else if (~shift_done)
          pre_txd <= tx_shift_reg_out;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          txd <= 1;
      else if (clk_en)
          txd <= pre_txd & ~do_force_break;
    end


  //_reg, which is an e_register
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          unxshiftxtx_shift_register_contentsxtx_shift_reg_outxx6_out <= 0;
      else if (clk_en)
          unxshiftxtx_shift_register_contentsxtx_shift_reg_outxx6_out <= unxshiftxtx_shift_register_contentsxtx_shift_reg_outxx6_in;
    end


  assign unxshiftxtx_shift_register_contentsxtx_shift_reg_outxx6_in = (do_load_shifter)? tx_load_val :
    (do_shift)? {1'b0,
    unxshiftxtx_shift_register_contentsxtx_shift_reg_outxx6_out[9 : 1]} :
    unxshiftxtx_shift_register_contentsxtx_shift_reg_outxx6_out;

  assign tx_shift_register_contents = unxshiftxtx_shift_register_contentsxtx_shift_reg_outxx6_out;
  assign tx_shift_reg_out = unxshiftxtx_shift_register_contentsxtx_shift_reg_outxx6_out[0];

endmodule


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 

module nios_tester_uart_0_rx_stimulus_source (
                                               // inputs:
                                                baud_divisor,
                                                clk,
                                                clk_en,
                                                reset_n,
                                                rx_char_ready,
                                                rxd,

                                               // outputs:
                                                source_rxd
                                             )
;

  output           source_rxd;
  input   [  9: 0] baud_divisor;
  input            clk;
  input            clk_en;
  input            reset_n;
  input            rx_char_ready;
  input            rxd;

  reg     [  7: 0] d1_stim_data;
  reg              delayed_unxrx_char_readyxx0;
  wire             do_send_stim_data;
  wire             pickup_pulse;
  wire             source_rxd;
  wire    [  7: 0] stim_data;
  wire             unused_empty;
  wire             unused_overrun;
  wire             unused_ready;

//synthesis translate_off
//////////////// SIMULATION-ONLY CONTENTS
  //stimulus_transmitter, which is an e_instance
  nios_tester_uart_0_tx stimulus_transmitter
    (
      .baud_divisor     (baud_divisor),
      .begintransfer    (do_send_stim_data),
      .clk              (clk),
      .clk_en           (clk_en),
      .do_force_break   (1'b0),
      .reset_n          (reset_n),
      .status_wr_strobe (1'b0),
      .tx_data          (d1_stim_data),
      .tx_overrun       (unused_overrun),
      .tx_ready         (unused_ready),
      .tx_shift_empty   (unused_empty),
      .tx_wr_strobe     (1'b1),
      .txd              (source_rxd)
    );

  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          d1_stim_data <= 0;
      else if (do_send_stim_data)
          d1_stim_data <= stim_data;
    end


  assign stim_data = 8'b0;
  //delayed_unxrx_char_readyxx0, which is an e_register
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          delayed_unxrx_char_readyxx0 <= 0;
      else if (clk_en)
          delayed_unxrx_char_readyxx0 <= rx_char_ready;
    end


  assign pickup_pulse = ~(rx_char_ready) &  (delayed_unxrx_char_readyxx0);
  assign do_send_stim_data = (pickup_pulse || 1'b0) && 1'b0;

//////////////// END SIMULATION-ONLY CONTENTS

//synthesis translate_on
//synthesis read_comments_as_HDL on
//  assign source_rxd = rxd;
//synthesis read_comments_as_HDL off

endmodule


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 

module nios_tester_uart_0_rx (
                               // inputs:
                                baud_divisor,
                                begintransfer,
                                clk,
                                clk_en,
                                reset_n,
                                rx_rd_strobe,
                                rxd,
                                status_wr_strobe,

                               // outputs:
                                break_detect,
                                framing_error,
                                parity_error,
                                rx_char_ready,
                                rx_data,
                                rx_overrun
                             )
;

  output           break_detect;
  output           framing_error;
  output           parity_error;
  output           rx_char_ready;
  output  [  7: 0] rx_data;
  output           rx_overrun;
  input   [  9: 0] baud_divisor;
  input            begintransfer;
  input            clk;
  input            clk_en;
  input            reset_n;
  input            rx_rd_strobe;
  input            rxd;
  input            status_wr_strobe;

  reg              baud_clk_en;
  wire    [  9: 0] baud_load_value;
  reg     [  9: 0] baud_rate_counter;
  wire             baud_rate_counter_is_zero;
  reg              break_detect;
  reg              delayed_unxrx_in_processxx3;
  reg              delayed_unxsync_rxdxx1;
  reg              delayed_unxsync_rxdxx2;
  reg              do_start_rx;
  reg              framing_error;
  wire             got_new_char;
  wire    [  8: 0] half_bit_cell_divisor;
  wire             is_break;
  wire             is_framing_error;
  wire             parity_error;
  wire    [  7: 0] raw_data_in;
  reg              rx_char_ready;
  reg     [  7: 0] rx_data;
  wire             rx_in_process;
  reg              rx_overrun;
  wire             rx_rd_strobe_onset;
  wire             rxd_edge;
  wire             rxd_falling;
  wire    [  9: 0] rxd_shift_reg;
  wire             sample_enable;
  wire             shift_reg_start_bit_n;
  wire             source_rxd;
  wire             stop_bit;
  wire             sync_rxd;
  wire             unused_start_bit;
  wire    [  9: 0] unxshiftxrxd_shift_regxshift_reg_start_bit_nxx7_in;
  reg     [  9: 0] unxshiftxrxd_shift_regxshift_reg_start_bit_nxx7_out;
  nios_tester_uart_0_rx_stimulus_source the_nios_tester_uart_0_rx_stimulus_source
    (
      .baud_divisor  (baud_divisor),
      .clk           (clk),
      .clk_en        (clk_en),
      .reset_n       (reset_n),
      .rx_char_ready (rx_char_ready),
      .rxd           (rxd),
      .source_rxd    (source_rxd)
    );

  altera_std_synchronizer the_altera_std_synchronizer
    (
      .clk (clk),
      .din (source_rxd),
      .dout (sync_rxd),
      .reset_n (reset_n)
    );

  defparam the_altera_std_synchronizer.depth = 2;

  //delayed_unxsync_rxdxx1, which is an e_register
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          delayed_unxsync_rxdxx1 <= 0;
      else if (clk_en)
          delayed_unxsync_rxdxx1 <= sync_rxd;
    end


  assign rxd_falling = ~(sync_rxd) &  (delayed_unxsync_rxdxx1);
  //delayed_unxsync_rxdxx2, which is an e_register
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          delayed_unxsync_rxdxx2 <= 0;
      else if (clk_en)
          delayed_unxsync_rxdxx2 <= sync_rxd;
    end


  assign rxd_edge = (sync_rxd) ^  (delayed_unxsync_rxdxx2);
  assign rx_rd_strobe_onset = rx_rd_strobe && begintransfer;
  assign half_bit_cell_divisor = baud_divisor[9 : 1];
  assign baud_load_value = (rxd_edge)? half_bit_cell_divisor :
    baud_divisor;

  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          baud_rate_counter <= 0;
      else if (clk_en)
          if (baud_rate_counter_is_zero || rxd_edge)
              baud_rate_counter <= baud_load_value;
          else 
            baud_rate_counter <= baud_rate_counter - 1;
    end


  assign baud_rate_counter_is_zero = baud_rate_counter == 0;
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          baud_clk_en <= 0;
      else if (clk_en)
          if (rxd_edge)
              baud_clk_en <= 0;
          else 
            baud_clk_en <= baud_rate_counter_is_zero;
    end


  assign sample_enable = baud_clk_en && rx_in_process;
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          do_start_rx <= 0;
      else if (clk_en)
          if (~rx_in_process && rxd_falling)
              do_start_rx <= 1;
          else 
            do_start_rx <= 0;
    end


  assign rx_in_process = shift_reg_start_bit_n;
  assign {stop_bit,
raw_data_in,
unused_start_bit} = rxd_shift_reg;
  assign is_break = ~(|rxd_shift_reg);
  assign is_framing_error = ~stop_bit && ~is_break;
  //delayed_unxrx_in_processxx3, which is an e_register
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          delayed_unxrx_in_processxx3 <= 0;
      else if (clk_en)
          delayed_unxrx_in_processxx3 <= rx_in_process;
    end


  assign got_new_char = ~(rx_in_process) &  (delayed_unxrx_in_processxx3);
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          rx_data <= 0;
      else if (got_new_char)
          rx_data <= raw_data_in;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          framing_error <= 0;
      else if (clk_en)
          if (status_wr_strobe)
              framing_error <= 0;
          else if (got_new_char && is_framing_error)
              framing_error <= -1;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          break_detect <= 0;
      else if (clk_en)
          if (status_wr_strobe)
              break_detect <= 0;
          else if (got_new_char && is_break)
              break_detect <= -1;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          rx_overrun <= 0;
      else if (clk_en)
          if (status_wr_strobe)
              rx_overrun <= 0;
          else if (got_new_char && rx_char_ready)
              rx_overrun <= -1;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          rx_char_ready <= 0;
      else if (clk_en)
          if (rx_rd_strobe_onset)
              rx_char_ready <= 0;
          else if (got_new_char)
              rx_char_ready <= -1;
    end


  assign parity_error = 0;
  //_reg, which is an e_register
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          unxshiftxrxd_shift_regxshift_reg_start_bit_nxx7_out <= 0;
      else if (clk_en)
          unxshiftxrxd_shift_regxshift_reg_start_bit_nxx7_out <= unxshiftxrxd_shift_regxshift_reg_start_bit_nxx7_in;
    end


  assign unxshiftxrxd_shift_regxshift_reg_start_bit_nxx7_in = (do_start_rx)? {10{1'b1}} :
    (sample_enable)? {sync_rxd,
    unxshiftxrxd_shift_regxshift_reg_start_bit_nxx7_out[9 : 1]} :
    unxshiftxrxd_shift_regxshift_reg_start_bit_nxx7_out;

  assign rxd_shift_reg = unxshiftxrxd_shift_regxshift_reg_start_bit_nxx7_out;
  assign shift_reg_start_bit_n = unxshiftxrxd_shift_regxshift_reg_start_bit_nxx7_out[0];

endmodule


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 

module nios_tester_uart_0_regs (
                                 // inputs:
                                  address,
                                  break_detect,
                                  chipselect,
                                  clk,
                                  clk_en,
                                  cts_n,
                                  framing_error,
                                  parity_error,
                                  read_n,
                                  reset_n,
                                  rx_char_ready,
                                  rx_data,
                                  rx_overrun,
                                  tx_overrun,
                                  tx_ready,
                                  tx_shift_empty,
                                  write_n,
                                  writedata,

                                 // outputs:
                                  baud_divisor,
                                  dataavailable,
                                  do_force_break,
                                  irq,
                                  readdata,
                                  readyfordata,
                                  rts_n,
                                  rx_rd_strobe,
                                  status_wr_strobe,
                                  tx_data,
                                  tx_wr_strobe
                               )
;

  output  [  9: 0] baud_divisor;
  output           dataavailable;
  output           do_force_break;
  output           irq;
  output  [ 15: 0] readdata;
  output           readyfordata;
  output           rts_n;
  output           rx_rd_strobe;
  output           status_wr_strobe;
  output  [  7: 0] tx_data;
  output           tx_wr_strobe;
  input   [  2: 0] address;
  input            break_detect;
  input            chipselect;
  input            clk;
  input            clk_en;
  input            cts_n;
  input            framing_error;
  input            parity_error;
  input            read_n;
  input            reset_n;
  input            rx_char_ready;
  input   [  7: 0] rx_data;
  input            rx_overrun;
  input            tx_overrun;
  input            tx_ready;
  input            tx_shift_empty;
  input            write_n;
  input   [ 15: 0] writedata;

  wire             any_error;
  wire    [  9: 0] baud_divisor;
  reg     [ 12: 0] control_reg;
  wire             control_wr_strobe;
  wire             cts_edge;
  reg              cts_status_bit;
  reg              d1_rx_char_ready;
  reg              d1_tx_ready;
  wire             dataavailable;
  reg              dcts_status_bit;
  reg              delayed_unxcts_status_bitxx5;
  reg              delayed_unxtx_readyxx4;
  wire    [  9: 0] divisor_constant;
  wire             do_force_break;
  wire             do_write_char;
  wire             eop_status_bit;
  wire             ie_any_error;
  wire             ie_break_detect;
  wire             ie_dcts;
  wire             ie_framing_error;
  wire             ie_parity_error;
  wire             ie_rx_char_ready;
  wire             ie_rx_overrun;
  wire             ie_tx_overrun;
  wire             ie_tx_ready;
  wire             ie_tx_shift_empty;
  reg              irq;
  wire             qualified_irq;
  reg     [ 15: 0] readdata;
  wire             readyfordata;
  wire             rts_control_bit;
  wire             rts_n;
  wire             rx_rd_strobe;
  wire    [ 15: 0] selected_read_data;
  wire    [ 12: 0] status_reg;
  wire             status_wr_strobe;
  reg     [  7: 0] tx_data;
  wire             tx_wr_strobe;
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          readdata <= 0;
      else if (clk_en)
          readdata <= selected_read_data;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          irq <= 0;
      else if (clk_en)
          irq <= qualified_irq;
    end


  assign rx_rd_strobe = chipselect && ~read_n  && (address == 3'd0);
  assign tx_wr_strobe = chipselect && ~write_n && (address == 3'd1);
  assign status_wr_strobe = chipselect && ~write_n && (address == 3'd2);
  assign control_wr_strobe = chipselect && ~write_n && (address == 3'd3);
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          tx_data <= 0;
      else if (tx_wr_strobe)
          tx_data <= writedata[7 : 0];
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          control_reg <= 0;
      else if (control_wr_strobe)
          control_reg <= writedata[12 : 0];
    end


  assign baud_divisor = divisor_constant;
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          cts_status_bit <= 1;
      else if (clk_en)
          cts_status_bit <= ~cts_n;
    end


  //delayed_unxcts_status_bitxx5, which is an e_register
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          delayed_unxcts_status_bitxx5 <= 0;
      else if (clk_en)
          delayed_unxcts_status_bitxx5 <= cts_status_bit;
    end


  assign cts_edge = (cts_status_bit) ^  (delayed_unxcts_status_bitxx5);
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          dcts_status_bit <= 0;
      else if (clk_en)
          if (status_wr_strobe)
              dcts_status_bit <= 0;
          else if (cts_edge)
              dcts_status_bit <= -1;
    end


  assign rts_n = ~rts_control_bit;
  assign {rts_control_bit,
ie_dcts,
do_force_break,
ie_any_error,
ie_rx_char_ready,
ie_tx_ready,
ie_tx_shift_empty,
ie_tx_overrun,
ie_rx_overrun,
ie_break_detect,
ie_framing_error,
ie_parity_error} = control_reg;
  assign any_error = tx_overrun ||
    rx_overrun ||
    parity_error ||
    framing_error ||
    break_detect;

  assign status_reg = {eop_status_bit,
    cts_status_bit,
    dcts_status_bit,
    1'b0,
    any_error,
    rx_char_ready,
    tx_ready,
    tx_shift_empty,
    tx_overrun,
    rx_overrun,
    break_detect,
    framing_error,
    parity_error};

  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          d1_rx_char_ready <= 0;
      else if (clk_en)
          d1_rx_char_ready <= rx_char_ready;
    end


  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          d1_tx_ready <= 0;
      else if (clk_en)
          d1_tx_ready <= tx_ready;
    end


  assign dataavailable = d1_rx_char_ready;
  assign readyfordata = d1_tx_ready;
  assign eop_status_bit = 1'b0;
  assign selected_read_data = ({16 {(address == 3'd0)}} & rx_data) |
    ({16 {(address == 3'd1)}} & tx_data) |
    ({16 {(address == 3'd2)}} & status_reg) |
    ({16 {(address == 3'd3)}} & control_reg);

  assign qualified_irq = (ie_dcts           && dcts_status_bit ) ||
    (ie_any_error      && any_error      ) ||
    (ie_tx_shift_empty && tx_shift_empty ) ||
    (ie_tx_overrun     && tx_overrun     ) ||
    (ie_rx_overrun     && rx_overrun     ) ||
    (ie_break_detect   && break_detect   ) ||
    (ie_framing_error  && framing_error  ) ||
    (ie_parity_error   && parity_error   ) ||
    (ie_rx_char_ready  && rx_char_ready  ) ||
    (ie_tx_ready       && tx_ready       );


//synthesis translate_off
//////////////// SIMULATION-ONLY CONTENTS
  //delayed_unxtx_readyxx4, which is an e_register
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          delayed_unxtx_readyxx4 <= 0;
      else if (clk_en)
          delayed_unxtx_readyxx4 <= tx_ready;
    end


  assign do_write_char = (tx_ready) & ~(delayed_unxtx_readyxx4);
  always @(posedge clk)
    begin
      if (do_write_char)
          $write("%c", tx_data);
    end


  assign divisor_constant = 4;

//////////////// END SIMULATION-ONLY CONTENTS

//synthesis translate_on
//synthesis read_comments_as_HDL on
//  assign divisor_constant = 543;
//synthesis read_comments_as_HDL off

endmodule


// synthesis translate_off
`timescale 1ns / 1ps
// synthesis translate_on

// turn off superfluous verilog processor warnings 
// altera message_level Level1 
// altera message_off 10034 10035 10036 10037 10230 10240 10030 

module nios_tester_uart_0 (
                            // inputs:
                             address,
                             begintransfer,
                             chipselect,
                             clk,
                             cts_n,
                             read_n,
                             reset_n,
                             rxd,
                             write_n,
                             writedata,

                            // outputs:
                             dataavailable,
                             irq,
                             readdata,
                             readyfordata,
                             rts_n,
                             txd
                          )
  /* synthesis altera_attribute = "-name SYNCHRONIZER_IDENTIFICATION OFF" */ ;

  output           dataavailable;
  output           irq;
  output  [ 15: 0] readdata;
  output           readyfordata;
  output           rts_n;
  output           txd;
  input   [  2: 0] address;
  input            begintransfer;
  input            chipselect;
  input            clk;
  input            cts_n;
  input            read_n;
  input            reset_n;
  input            rxd;
  input            write_n;
  input   [ 15: 0] writedata;

  wire    [  9: 0] baud_divisor;
  wire             break_detect;
  wire             clk_en;
  wire             dataavailable;
  wire             do_force_break;
  wire             framing_error;
  wire             irq;
  wire             parity_error;
  wire    [ 15: 0] readdata;
  wire             readyfordata;
  wire             rts_n;
  wire             rx_char_ready;
  wire    [  7: 0] rx_data;
  wire             rx_overrun;
  wire             rx_rd_strobe;
  wire             status_wr_strobe;
  wire    [  7: 0] tx_data;
  wire             tx_overrun;
  wire             tx_ready;
  wire             tx_shift_empty;
  wire             tx_wr_strobe;
  wire             txd;
  assign clk_en = 1;
  nios_tester_uart_0_tx the_nios_tester_uart_0_tx
    (
      .baud_divisor     (baud_divisor),
      .begintransfer    (begintransfer),
      .clk              (clk),
      .clk_en           (clk_en),
      .do_force_break   (do_force_break),
      .reset_n          (reset_n),
      .status_wr_strobe (status_wr_strobe),
      .tx_data          (tx_data),
      .tx_overrun       (tx_overrun),
      .tx_ready         (tx_ready),
      .tx_shift_empty   (tx_shift_empty),
      .tx_wr_strobe     (tx_wr_strobe),
      .txd              (txd)
    );

  nios_tester_uart_0_rx the_nios_tester_uart_0_rx
    (
      .baud_divisor     (baud_divisor),
      .begintransfer    (begintransfer),
      .break_detect     (break_detect),
      .clk              (clk),
      .clk_en           (clk_en),
      .framing_error    (framing_error),
      .parity_error     (parity_error),
      .reset_n          (reset_n),
      .rx_char_ready    (rx_char_ready),
      .rx_data          (rx_data),
      .rx_overrun       (rx_overrun),
      .rx_rd_strobe     (rx_rd_strobe),
      .rxd              (rxd),
      .status_wr_strobe (status_wr_strobe)
    );

  nios_tester_uart_0_regs the_nios_tester_uart_0_regs
    (
      .address          (address),
      .baud_divisor     (baud_divisor),
      .break_detect     (break_detect),
      .chipselect       (chipselect),
      .clk              (clk),
      .clk_en           (clk_en),
      .cts_n            (cts_n),
      .dataavailable    (dataavailable),
      .do_force_break   (do_force_break),
      .framing_error    (framing_error),
      .irq              (irq),
      .parity_error     (parity_error),
      .read_n           (read_n),
      .readdata         (readdata),
      .readyfordata     (readyfordata),
      .reset_n          (reset_n),
      .rts_n            (rts_n),
      .rx_char_ready    (rx_char_ready),
      .rx_data          (rx_data),
      .rx_overrun       (rx_overrun),
      .rx_rd_strobe     (rx_rd_strobe),
      .status_wr_strobe (status_wr_strobe),
      .tx_data          (tx_data),
      .tx_overrun       (tx_overrun),
      .tx_ready         (tx_ready),
      .tx_shift_empty   (tx_shift_empty),
      .tx_wr_strobe     (tx_wr_strobe),
      .write_n          (write_n),
      .writedata        (writedata)
    );

  //s1, which is an e_avalon_slave

endmodule

