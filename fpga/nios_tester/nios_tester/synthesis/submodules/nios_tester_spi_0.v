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

//Register map:
//addr      register      type
//0         read data     r
//1         write data    w
//2         status        r/w
//3         control       r/w
//4         reserved
//5         slave-enable  r/w
//6         end-of-packet-value r/w
//INPUT_CLOCK: 62500000
//ISMASTER: 1
//DATABITS: 8
//TARGETCLOCK: 2000000
//NUMSLAVES: 1
//CPOL: 0
//CPHA: 0
//LSBFIRST: 0
//EXTRADELAY: 0
//TARGETSSDELAY: 0

module nios_tester_spi_0 (
                           // inputs:
                            MISO,
                            clk,
                            data_from_cpu,
                            mem_addr,
                            read_n,
                            reset_n,
                            spi_select,
                            write_n,

                           // outputs:
                            MOSI,
                            SCLK,
                            SS_n,
                            data_to_cpu,
                            dataavailable,
                            endofpacket,
                            irq,
                            readyfordata
                         )
;

  output           MOSI;
  output           SCLK;
  output           SS_n;
  output  [ 15: 0] data_to_cpu;
  output           dataavailable;
  output           endofpacket;
  output           irq;
  output           readyfordata;
  input            MISO;
  input            clk;
  input   [ 15: 0] data_from_cpu;
  input   [  2: 0] mem_addr;
  input            read_n;
  input            reset_n;
  input            spi_select;
  input            write_n;

  wire             E;
  reg              EOP;
  reg              MISO_reg;
  wire             MOSI;
  reg              ROE;
  reg              RRDY;
  wire             SCLK;
  reg              SCLK_reg;
  reg              SSO_reg;
  wire             SS_n;
  wire             TMT;
  reg              TOE;
  wire             TRDY;
  wire             control_wr_strobe;
  reg              data_rd_strobe;
  reg     [ 15: 0] data_to_cpu;
  reg              data_wr_strobe;
  wire             dataavailable;
  wire             ds_MISO;
  wire             enableSS;
  wire             endofpacket;
  reg     [ 15: 0] endofpacketvalue_reg;
  wire             endofpacketvalue_wr_strobe;
  reg              iEOP_reg;
  reg              iE_reg;
  reg              iROE_reg;
  reg              iRRDY_reg;
  reg              iTMT_reg;
  reg              iTOE_reg;
  reg              iTRDY_reg;
  wire             irq;
  reg              irq_reg;
  wire             p1_data_rd_strobe;
  wire    [ 15: 0] p1_data_to_cpu;
  wire             p1_data_wr_strobe;
  wire             p1_rd_strobe;
  wire    [  4: 0] p1_slowcount;
  wire             p1_wr_strobe;
  reg              rd_strobe;
  wire             readyfordata;
  reg     [  7: 0] rx_holding_reg;
  reg     [  7: 0] shift_reg;
  wire             slaveselect_wr_strobe;
  wire             slowclock;
  reg     [  4: 0] slowcount;
  wire    [ 10: 0] spi_control;
  reg     [ 15: 0] spi_slave_select_holding_reg;
  reg     [ 15: 0] spi_slave_select_reg;
  wire    [ 10: 0] spi_status;
  reg     [  4: 0] state;
  reg              stateZero;
  wire             status_wr_strobe;
  reg              transmitting;
  reg              tx_holding_primed;
  reg     [  7: 0] tx_holding_reg;
  reg              wr_strobe;
  wire             write_shift_reg;
  wire             write_tx_holding;
  //spi_control_port, which is an e_avalon_slave
  assign p1_rd_strobe = ~rd_strobe & spi_select & ~read_n;
  // Read is a two-cycle event.
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          rd_strobe <= 0;
      else 
        rd_strobe <= p1_rd_strobe;
    end


  assign p1_data_rd_strobe = p1_rd_strobe & (mem_addr == 0);
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          data_rd_strobe <= 0;
      else 
        data_rd_strobe <= p1_data_rd_strobe;
    end


  assign p1_wr_strobe = ~wr_strobe & spi_select & ~write_n;
  // Write is a two-cycle event.
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          wr_strobe <= 0;
      else 
        wr_strobe <= p1_wr_strobe;
    end


  assign p1_data_wr_strobe = p1_wr_strobe & (mem_addr == 1);
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          data_wr_strobe <= 0;
      else 
        data_wr_strobe <= p1_data_wr_strobe;
    end


  assign control_wr_strobe = wr_strobe & (mem_addr == 3);
  assign status_wr_strobe = wr_strobe & (mem_addr == 2);
  assign slaveselect_wr_strobe = wr_strobe & (mem_addr == 5);
  assign endofpacketvalue_wr_strobe = wr_strobe & (mem_addr == 6);
  assign TMT = ~transmitting & ~tx_holding_primed;
  assign E = ROE | TOE;
  assign spi_status = {EOP, E, RRDY, TRDY, TMT, TOE, ROE, 3'b0};
  // Streaming data ready for pickup.
  assign dataavailable = RRDY;

  // Ready to accept streaming data.
  assign readyfordata = TRDY;

  // Endofpacket condition detected.
  assign endofpacket = EOP;

  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
        begin
          iEOP_reg <= 0;
          iE_reg <= 0;
          iRRDY_reg <= 0;
          iTRDY_reg <= 0;
          iTMT_reg <= 0;
          iTOE_reg <= 0;
          iROE_reg <= 0;
          SSO_reg <= 0;
        end
      else if (control_wr_strobe)
        begin
          iEOP_reg <= data_from_cpu[9];
          iE_reg <= data_from_cpu[8];
          iRRDY_reg <= data_from_cpu[7];
          iTRDY_reg <= data_from_cpu[6];
          iTMT_reg <= data_from_cpu[5];
          iTOE_reg <= data_from_cpu[4];
          iROE_reg <= data_from_cpu[3];
          SSO_reg <= data_from_cpu[10];
        end
    end


  assign spi_control = {SSO_reg, iEOP_reg, iE_reg, iRRDY_reg, iTRDY_reg, 1'b0, iTOE_reg, iROE_reg, 3'b0};
  // IRQ output.
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          irq_reg <= 0;
      else 
        irq_reg <= (EOP & iEOP_reg) | ((TOE | ROE) & iE_reg) | (RRDY & iRRDY_reg) | (TRDY & iTRDY_reg) | (TOE & iTOE_reg) | (ROE & iROE_reg);
    end


  assign irq = irq_reg;
  // Slave select register.
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          spi_slave_select_reg <= 1;
      else if (write_shift_reg || control_wr_strobe & data_from_cpu[10] & ~SSO_reg)
          spi_slave_select_reg <= spi_slave_select_holding_reg;
    end


  // Slave select holding register.
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          spi_slave_select_holding_reg <= 1;
      else if (slaveselect_wr_strobe)
          spi_slave_select_holding_reg <= data_from_cpu;
    end


  // slowclock is active once every 16 system clock pulses.
  assign slowclock = slowcount == 5'hF;

  assign p1_slowcount = ({5 {(transmitting && !slowclock)}} & (slowcount + 1)) |
    ({5 {(~((transmitting && !slowclock)))}} & 0);

  // Divide counter for SPI clock.
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          slowcount <= 0;
      else 
        slowcount <= p1_slowcount;
    end


  // End-of-packet value register.
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          endofpacketvalue_reg <= 0;
      else if (endofpacketvalue_wr_strobe)
          endofpacketvalue_reg <= data_from_cpu;
    end


  assign p1_data_to_cpu = ((mem_addr == 2))? spi_status :
    ((mem_addr == 3))? spi_control :
    ((mem_addr == 6))? endofpacketvalue_reg :
    ((mem_addr == 5))? spi_slave_select_reg :
    rx_holding_reg;

  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
          data_to_cpu <= 0;
      else 
        // Data to cpu.
        data_to_cpu <= p1_data_to_cpu;

    end


  // 'state' counts from 0 to 17.
  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
        begin
          state <= 0;
          stateZero <= 1;
        end
      else if (transmitting & slowclock)
        begin
          stateZero <= state == 17;
          if (state == 17)
              state <= 0;
          else 
            state <= state + 1;
        end
    end


  assign enableSS = transmitting & ~stateZero;
  assign MOSI = shift_reg[7];
  assign SS_n = (enableSS | SSO_reg) ? ~spi_slave_select_reg : {1 {1'b1} };
  assign SCLK = SCLK_reg;
  // As long as there's an empty spot somewhere,
  //it's safe to write data.
  assign TRDY = ~(transmitting & tx_holding_primed);

  // Enable write to tx_holding_register.
  assign write_tx_holding = data_wr_strobe & TRDY;

  // Enable write to shift register.
  assign write_shift_reg = tx_holding_primed & ~transmitting;

  always @(posedge clk or negedge reset_n)
    begin
      if (reset_n == 0)
        begin
          shift_reg <= 0;
          rx_holding_reg <= 0;
          EOP <= 0;
          RRDY <= 0;
          ROE <= 0;
          TOE <= 0;
          tx_holding_reg <= 0;
          tx_holding_primed <= 0;
          transmitting <= 0;
          SCLK_reg <= 0;
          MISO_reg <= 0;
        end
      else 
        begin
          if (write_tx_holding)
            begin
              tx_holding_reg <= data_from_cpu;
              tx_holding_primed <= 1;
            end
          if (data_wr_strobe & ~TRDY)
              // You wrote when I wasn't ready.
              TOE <= 1;

          // EOP must be updated by the last (2nd) cycle of access.
          if ((p1_data_rd_strobe && (rx_holding_reg == endofpacketvalue_reg)) || (p1_data_wr_strobe && (data_from_cpu[7 : 0] == endofpacketvalue_reg)))
              EOP <= 1;
          if (write_shift_reg)
            begin
              shift_reg <= tx_holding_reg;
              transmitting <= 1;
            end
          if (write_shift_reg & ~write_tx_holding)
              // Clear tx_holding_primed
              tx_holding_primed <= 0;

          if (data_rd_strobe)
              // On data read, clear the RRDY bit.
              RRDY <= 0;

          if (status_wr_strobe)
            begin
              // On status write, clear all status bits (ignore the data).
              EOP <= 0;

              RRDY <= 0;
              ROE <= 0;
              TOE <= 0;
            end
          if (slowclock)
            begin
              if (state == 17)
                begin
                  transmitting <= 0;
                  RRDY <= 1;
                  rx_holding_reg <= shift_reg;
                  SCLK_reg <= 0;
                  if (RRDY)
                      ROE <= 1;
                end
              else if (state != 0)
                  if (transmitting)
                      SCLK_reg <= ~SCLK_reg;
              if (SCLK_reg ^ 0 ^ 0)
                begin
                  if (1)
                      shift_reg <= {shift_reg[6 : 0], MISO_reg};
                end
              else 
                MISO_reg <= ds_MISO;
            end
        end
    end


  assign ds_MISO = MISO;

endmodule

