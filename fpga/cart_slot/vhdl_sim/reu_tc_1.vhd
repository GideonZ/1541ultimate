
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.dma_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.slot_bus_pkg.all;
use work.slot_bus_master_bfm_pkg.all;
use work.tl_string_util_pkg.all;
use work.tl_flat_memory_model_pkg.all;
use work.reu_pkg.all;

entity reu_tc_1 is

end reu_tc_1;

architecture testcase of reu_tc_1 is
	shared variable errors : integer := 0;
	type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);
	constant c_reu_base			: unsigned := X"DF00";

	constant c_read_after_reset : t_byte_array(0 to 15) := (
		X"10", -- status: version 0, no irq pending, done flag not set, no verify error, 256K or bigger.
		X"10", -- command: only ff00 flag set
		X"00", X"00", 		 -- c64 base / addr
		X"00", X"00", X"F8", -- reu base / addr (19 bits; upper 5 bits unused and thus 1)
		X"FF", X"FF", 		 -- trans length
		X"1F", -- irq mask
		X"3F", -- control
		X"FF", X"FF", X"FF", X"FF", X"FF" ); -- no register

	constant c_read_after_verify_1 : t_byte_array(0 to 15) := (
		X"D0", -- status: version 0, irq pending, done flag set, no verify error, 256K or bigger.
		X"13", -- command: ff00 flag set, mode is verify
		X"10", X"30", 		 -- c64 base / addr
		X"55", X"23", X"F9", -- reu base / addr (19 bits; upper 5 bits unused and thus 1)
		X"01", X"00", 		 -- trans length = 1
		X"FF", -- irq mask (all 3 bits set, other bits unused, thus 1)
		X"3F", -- control
		X"FF", X"FF", X"FF", X"FF", X"FF" ); -- no register

	constant c_read_after_verify_2 : t_byte_array(0 to 15) := (
		-- IRQ | DONE | ERR | SIZE | VERSION
		X"B0", -- status: version 0, irq pending, done flag NOT set, verify error, 256K or bigger.
		X"13", -- command: ff00 flag set, mode is verify
		X"10", X"30", 		 -- c64 base / addr
		X"55", X"23", X"F9", -- reu base / addr (19 bits; upper 5 bits unused and thus 1)
		X"10", X"00", 		 -- trans length = 0x10 (error after 16 bytes, 16 to go)
		X"FF", -- irq mask
		X"3F", -- control
		X"FF", X"FF", X"FF", X"FF", X"FF" ); -- no register

	constant c_read_after_swap : t_byte_array(0 to 15) := (
		-- IRQ | DONE | ERR | SIZE | VERSION
		X"D0", -- status: version 0, irq pending, done flag set, no verify error, 256K or bigger.
		X"12", -- command: ff00 flag set, mode is swap
		X"A0", X"30", 		 -- c64 base / addr 3080+20
		X"20", X"00", X"F8", -- reu base / addr (19 bits; upper 5 bits unused and thus 1)
		X"01", X"00", 		 -- trans length = 1
		X"FF", -- irq mask
		X"3F", -- control
		X"FF", X"FF", X"FF", X"FF", X"FF" ); -- no register

	procedure check(a,b : std_logic_vector; d: unsigned; s : string) is
	begin
		if a /= b then
			print("ERROR: " & s & ": " & hstr(a) & "/=" & hstr(b) & " on addr " & hstr(d));
			errors := errors + 1;
		end if;
--		assert a = b report s severity error;
	end procedure;

begin
	i_harness: entity work.harness_reu
		;
		
	p_test: process
		variable slot	: p_slot_bus_master_bfm_object;
		variable data   : std_logic_vector(7 downto 0);
		variable addr	: unsigned(15 downto 0);
		variable c64_mem	: h_mem_object;
		variable reu_mem	: h_mem_object;
		--variable datas  : t_byte_array(0 to 15);

		procedure reu_operation(op 			: std_logic_vector;
								c64_addr	: unsigned(15 downto 0);
								reu_addr	: unsigned(23 downto 0);
								len			: unsigned(15 downto 0) ) is
			variable cmd	: std_logic_vector(7 downto 0);
		begin
			cmd := X"90";
			cmd(op'length-1 downto 0) := op;
			slot_io_write(slot, c_reu_base + c_c64base_l, std_logic_vector(c64_addr( 7 downto 0)));
			slot_io_write(slot, c_reu_base + c_c64base_h, std_logic_vector(c64_addr(15 downto 8)));
			slot_io_write(slot, c_reu_base + c_reubase_l, std_logic_vector(reu_addr( 7 downto 0)));
			slot_io_write(slot, c_reu_base + c_reubase_m, std_logic_vector(reu_addr(15 downto 8)));
			slot_io_write(slot, c_reu_base + c_reubase_h, std_logic_vector(reu_addr(23 downto 16)));
			slot_io_write(slot, c_reu_base + c_translen_l, std_logic_vector(len( 7 downto 0)));
			slot_io_write(slot, c_reu_base + c_translen_h, std_logic_vector(len(15 downto 8)));
			slot_io_write(slot, c_reu_base + c_command, cmd);
		end procedure;
	begin
		wait for 150 ns;
		bind_slot_bus_master_bfm("slot master", slot);
		bind_mem_model("c64_memory", c64_mem);
		bind_mem_model("reu_memory", reu_mem);
		for i in c_read_after_reset'range loop
			addr := c_reu_base + i;
			slot_io_read(slot, addr, data);
			check(data, c_read_after_reset(i), addr, "Register read after reset not as expected.");
		end loop;

		for i in 0 to 255 loop
			write_memory_8(c64_mem, std_logic_vector(to_unsigned(16#3000# + i, 32)),
									std_logic_vector(to_unsigned(99+i*37, 8))); 
		end loop;
		
		-- enable IRQ on done (and verify error for later), so that we can wait for it
		slot_io_write(slot, c_reu_base + c_irqmask, X"E0");
			
		-- try to copy something (16 bytes) from c64 to reu
		reu_operation(c_mode_toreu, X"3000", X"012345", X"0010");	
		slot_wait_irq(slot);
		slot_io_read(slot, c_reu_base + c_status, data);

		-- Verify the copied data
		reu_operation(c_mode_verify, X"3000", X"012345", X"0010");	
		slot_wait_irq(slot);

		for i in c_read_after_verify_1'range loop
			addr := c_reu_base + i;
			slot_io_read(slot, addr, data);
			check(data, c_read_after_verify_1(i), addr, "Register read after verify 1 not as expected.");
		end loop;
											
		-- Verify operation 2: verify 32 bytes, of course this will fail, since we only copied 16 bytes
		reu_operation(c_mode_verify, X"3000", X"012345", X"0020");	
		slot_wait_irq(slot);

		for i in c_read_after_verify_2'range loop
			addr := c_reu_base + i;
			slot_io_read(slot, addr, data);
			check(data, c_read_after_verify_2(i), addr, "Register read after verify 2 not as expected.");
		end loop;

		-- Swap operation
		reu_operation(c_mode_swap, X"3080", X"000000", X"0020");	
		slot_wait_irq(slot);

		for i in c_read_after_swap'range loop
			addr := c_reu_base + i;
			slot_io_read(slot, addr, data);
			check(data, c_read_after_swap(i), addr, "Register read after swap not as expected.");
		end loop;


		assert errors = 0 report "Errors encounted" severity failure;
		wait;    
	end process;
	
end testcase;
