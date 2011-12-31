library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.slot_bus_pkg.all;

entity all_carts_v4 is
generic (
    g_rom_base      : std_logic_vector(27 downto 0) := X"0F00000"; -- multiple of 1M
    g_ram_base      : std_logic_vector(27 downto 0) := X"0EF0000" ); -- multiple of 64K
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    RST_in          : in  std_logic;
    c64_reset       : in  std_logic;
    
    ethernet_enable : in  std_logic := '1';
    freeze_trig     : in  std_logic; -- goes '1' when the button has been pressed and we're waiting to enter the freezer
    freeze_act      : in  std_logic; -- goes '1' when we need to switch in the cartridge for freeze mode
    unfreeze        : out std_logic; -- indicates the freeze logic to switch back to non-freeze mode.
    
    cart_kill       : in  std_logic;
    cart_logic      : in  std_logic_vector(3 downto 0);   -- 1 out of 16 logic emulations
    
    slot_req        : in  t_slot_req;
    slot_resp       : out t_slot_resp;

    epyx_timeout    : in  std_logic;
    serve_enable    : out std_logic; -- enables fetching bus address PHI2=1
    serve_vic       : out std_logic; -- enables doing so for PHI2=0
    serve_rom       : out std_logic; -- ROML or ROMH
    serve_io1       : out std_logic; -- IO1n
    serve_io2       : out std_logic; -- IO2n
    allow_write     : out std_logic;
    
    mem_addr        : out unsigned(25 downto 0);   
    
    irq_n           : out std_logic;
    nmi_n           : out std_logic;
    exrom_n         : out std_logic;
    game_n          : out std_logic;

    CART_LEDn       : out std_logic );

end all_carts_v4;    

architecture gideon of all_carts_v4 is
    signal reset_in     : std_logic;

    signal ext_bank     : std_logic_vector(18 downto 16);
    signal bank_bits    : std_logic_vector(15 downto 13);
    signal mode_bits    : std_logic_vector(2 downto 0);
    signal ram_select   : std_logic;
    
--    signal rom_enable   : std_logic;

    signal freeze_act_d : std_logic;

    signal cart_en      : std_logic;

    signal do_io2       : std_logic;
    signal allow_bank   : std_logic;
    signal hold_nmi     : std_logic;
    signal eth_addr     : boolean;
    signal cart_logic_d : std_logic_vector(cart_logic'range) := (others => '0');
    signal mem_addr_i   : std_logic_vector(27 downto 0);
        
    constant c_none         : std_logic_vector(3 downto 0) := "0000";
    constant c_8k           : std_logic_vector(3 downto 0) := "0001";
    constant c_16k          : std_logic_vector(3 downto 0) := "0010";
    constant c_16k_umax     : std_logic_vector(3 downto 0) := "0011";
    constant c_fc3          : std_logic_vector(3 downto 0) := "0100";
    constant c_ss5          : std_logic_vector(3 downto 0) := "0101";
    constant c_retro        : std_logic_vector(3 downto 0) := "0110";
    constant c_action       : std_logic_vector(3 downto 0) := "0111";
    constant c_system3      : std_logic_vector(3 downto 0) := "1000";
    constant c_ocean128     : std_logic_vector(3 downto 0) := "1001";
    constant c_epyx         : std_logic_vector(3 downto 0) := "1010";
    constant c_ocean256     : std_logic_vector(3 downto 0) := "1011";
    constant c_domark       : std_logic_vector(3 downto 0) := "1101";
    constant c_easy_flash   : std_logic_vector(3 downto 0) := "1100";

    constant c_serve_rom_rr : std_logic_vector(0 to 7) := "11011111";
    constant c_serve_io_rr  : std_logic_vector(0 to 7) := "10101111";
    
    -- alias
    signal slot_addr        : std_logic_vector(15 downto 0);
    signal io_read          : std_logic;
    signal io_write         : std_logic;
    signal io_addr          : std_logic_vector(8 downto 0);
    signal io_wdata         : std_logic_vector(7 downto 0);
begin
    serve_enable <= cart_en;

    slot_addr <= std_logic_vector(slot_req.bus_address);
    io_write  <= slot_req.io_write;
    io_read   <= slot_req.io_read;
    io_addr   <= std_logic_vector(slot_req.io_address(8 downto 0));
    io_wdata  <= slot_req.data;
    
    process(clock)
    begin
        if rising_edge(clock) then
            reset_in     <= reset or RST_in or c64_reset;
            freeze_act_d <= freeze_act;
			unfreeze     <= '0';
            
            -- control register
            if reset_in='1' then
                cart_logic_d <= cart_logic; -- activate change of mode!
                mode_bits    <= (others => '0');
                bank_bits    <= (others => '0');
                ext_bank     <= (others => '0');
                allow_bank   <= '0';
                ram_select   <= '0';
                do_io2       <= '1';
                cart_en      <= '1';
--                unfreeze     <= '0';
                hold_nmi     <= '0';
            elsif freeze_act='1' and freeze_act_d='0' then
                bank_bits  <= (others => '0');
                mode_bits  <= (others => '0');
                --allow_bank <= '0';
                ram_select <= '0';
                cart_en    <= '1';
--                unfreeze   <= '0';
                hold_nmi   <= '1';
            elsif cart_en = '0' then
                cart_logic_d <= cart_logic; -- activate change of mode!
            end if;

            serve_vic <= '0';
            
            case cart_logic_d is
            when c_fc3 =>
--                unfreeze <= '0';
                if io_write='1' and io_addr(8 downto 0) = "111111111" and cart_en='1' then -- DFFF
                    bank_bits <= io_wdata(1 downto 0) & '0';
                    mode_bits <= '0' & io_wdata(4) & io_wdata(5);
                    unfreeze  <= '1';
                    cart_en   <= not io_wdata(7);
                    hold_nmi  <= not io_wdata(6);
                end if;
                if freeze_act='1' then
                    game_n  <= '0';
                    exrom_n <= '1';
                else
                    game_n  <= mode_bits(0);
                    exrom_n <= mode_bits(1);
                end if;
                if mode_bits(1 downto 0)="10" then
                    serve_vic <= '1';
                end if;
                serve_rom <= '1';
                serve_io1 <= '1';
                serve_io2 <= '1';
                irq_n     <= '1';
                nmi_n     <= not(freeze_trig or freeze_act or hold_nmi);
                                    
            when c_retro | c_action =>
                if io_write='1' and io_addr(8 downto 1) = X"00" and cart_en='1' then -- DE00/DE01
                    if io_addr(0)='0' then
                        bank_bits <= io_wdata(7) & io_wdata(4 downto 3);
                        mode_bits <= io_wdata(5) & io_wdata(1 downto 0);
                        unfreeze  <= io_wdata(6);
                        cart_en   <= not io_wdata(2);
                    else
                        if io_wdata(6)='1' then
                            do_io2 <= '0';
                        end if;
                        if io_wdata(1)='1' then
                            allow_bank <= '1';
                        end if;
                    end if;
                end if;
                if freeze_act='1' then
                    game_n    <= '0';
                    exrom_n   <= '1';
                    serve_rom <= '1';
                    serve_io1 <= '0';
                    serve_io2 <= '0';
                else
                    game_n    <= not mode_bits(0);
                    exrom_n   <= mode_bits(1);
                    serve_io1 <= c_serve_io_rr(to_integer(unsigned(mode_bits)));
                    serve_io2 <= c_serve_io_rr(to_integer(unsigned(mode_bits))) and do_io2;
                    serve_rom <= c_serve_rom_rr(to_integer(unsigned(mode_bits)));
                end if;
                irq_n     <= not(freeze_trig or freeze_act);
                nmi_n     <= not(freeze_trig or freeze_act);

            when c_easy_flash =>
                if io_write='1' and io_addr(8)='0' and cart_en='1' then -- DExx
                    if io_addr(1)='0' then -- DE00
                        ext_bank  <= io_wdata(5 downto 3);
                        bank_bits <= io_wdata(2 downto 0);
                    else -- DE02
                        mode_bits <= io_wdata(2 downto 0); -- LED not implemented
                    end if;
                end if;
                game_n    <= not (mode_bits(0) or not mode_bits(2));
                exrom_n   <= not mode_bits(1);
                serve_rom <= '1';
                serve_io1 <= '0'; -- write registers only, no reads
                serve_io2 <= '1'; -- RAM
                irq_n     <= '1';
                nmi_n     <= '1';
                
            when c_ss5 =>
                if io_write='1' and io_addr(8) = '0' and cart_en='1' then -- DE00-DEFF
                    bank_bits <= io_wdata(4) & io_wdata(2) & '0';
                    mode_bits <= io_wdata(3) & io_wdata(1) & io_wdata(0);
                    unfreeze  <= not io_wdata(0);
                    cart_en   <= not io_wdata(3);
                end if;
                game_n    <= mode_bits(0);
                exrom_n   <= not mode_bits(1);
                serve_io1 <= cart_en;
                serve_io2 <= '0';
                serve_rom <= cart_en;
                irq_n     <= not(freeze_trig or freeze_act);
                nmi_n     <= not(freeze_trig or freeze_act);

            when c_8k =>
                if io_write='1' and io_addr(8 downto 0) = "111111111" and cart_en='1' then -- DFFF
                    cart_en <= '0'; -- permanent off
                end if;
                game_n    <= '1';
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '1'; -- for EPYX test
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_16k =>
                if io_write='1' and io_addr(8 downto 0) = "111111111" and cart_en='1' then -- DFFF
                    cart_en <= '0'; -- permanent off
                end if;
                game_n    <= '0';
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_16k_umax =>
                if io_write='1' and io_addr(8 downto 0) = "111111111" and cart_en='1' then -- DFFF
                    cart_en <= '0'; -- permanent off
                end if;
                game_n    <= '0';
                exrom_n   <= '1';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_ocean128 =>
                if io_write='1' and io_addr(8)='0' then -- DE00 range
                    bank_bits <= io_wdata(2 downto 0);
                    ext_bank  <= io_wdata(5 downto 3);
                end if;
                game_n    <= '1';
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';
            
            when c_domark =>
                if io_write='1' and io_addr(8)='0' then -- DE00 range
                    bank_bits <= io_wdata(2 downto 0);
                    ext_bank  <= '0' & io_wdata(4 downto 3);
                    mode_bits(0) <= io_wdata(7);
--                    if io_wdata(7 downto 5) /= "000" then -- permanent off
--                        cart_en <= '0';
--                    end if;
                    cart_en <= not (io_wdata(7) or io_wdata(6) or io_wdata(5));
                end if;
                game_n    <= '1';
                exrom_n   <= mode_bits(0);
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_ocean256 =>
                if io_write='1' and io_addr(8)='0' then -- DE00 range
                    bank_bits <= io_wdata(2 downto 0);
                    ext_bank  <= "00" & io_wdata(3);
                end if;
                game_n    <= '0';
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_system3 => -- 16K, only 8K used?
                if (io_write='1' or io_read='1') and io_addr(8)='0' then -- DE00 range
                    bank_bits <= io_addr(2 downto 0);
                    ext_bank  <= io_addr(5 downto 3);
                end if;
                game_n    <= '1';
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_epyx =>
                game_n    <= '1';
                exrom_n   <= epyx_timeout;
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '1'; -- rom visible df00-dfff
                irq_n     <= '1';
                nmi_n     <= '1';
            
            when others =>
                game_n    <= '1';
                exrom_n   <= '1';
                serve_rom <= '0';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            end case;
            
            if cart_kill='1' then
                cart_en  <= '0';
                hold_nmi <= '0';
            end if;
        end if;
    end process;
    
    CART_LEDn <= not cart_en;

    -- decode address DE02-DE0F
    eth_addr <= slot_addr(15 downto 4) = X"DE0" and slot_addr(3 downto 1) /= "000" and ethernet_enable='1';

    -- determine address
--  process(cart_logic_d, cart_base_d, slot_addr, mode_bits, bank_bits, do_io2, allow_bank, eth_addr)
    process(cart_logic_d, slot_addr, mode_bits, bank_bits, ext_bank, do_io2, allow_bank, eth_addr)
    begin
        mem_addr_i <= g_rom_base;

        -- defaults
        -- 64K, 8K banks, no writes
        mem_addr_i(15 downto 0)  <= bank_bits(15 downto 13) & slot_addr(12 downto 0);
        allow_write <= '0';

        case cart_logic_d is
        when c_retro =>
            -- 64K RAM
            if mode_bits(2)='1' then
                if slot_addr(13)='0' then
                    mem_addr_i <= g_ram_base(27 downto 16) & bank_bits(15 downto 13) & slot_addr(12 downto 0);
                    if allow_bank='0' and slot_addr(15 downto 13)="110" then -- io range exceptions
                        mem_addr_i <= g_ram_base(27 downto 16) & "000" & slot_addr(12 downto 0);
                    end if;
                end if;
                if slot_addr(15 downto 13)="100" then--and mode_bits(1 downto 0)/="10" then
                    allow_write <= '1';
                end if;
                if slot_addr(15 downto 8)=X"DE" and slot_addr(7 downto 1)/="0000000" then
                    allow_write <= '1';
                end if;
                if slot_addr(15 downto 8)=X"DF" and do_io2='1' then
                    allow_write <= '1';
                end if;
            end if;
        
        when c_action =>
            -- 32K RAM
            if mode_bits(2)='1' then
                if slot_addr(13)='0' then
                    mem_addr_i <= g_ram_base(27 downto 15) & bank_bits(14 downto 13) & slot_addr(12 downto 0);
                    if allow_bank='0' and slot_addr(15 downto 13)="110" then -- io range exceptions
                        mem_addr_i <= g_ram_base(27 downto 15) & "00" & slot_addr(12 downto 0);
                    end if;
                end if;
                if slot_addr(15 downto 13)="100" then -- and mode_bits(1 downto 0)="11" then
                    allow_write <= '1';
                end if;
                if slot_addr(15 downto 8)=X"DE" and slot_addr(7 downto 1)/="0000000" then
                    allow_write <= '1';
                end if;
                if slot_addr(15 downto 8)=X"DF" and do_io2='1' then
                    allow_write <= '1';
                end if;
            end if;

        when c_easy_flash =>
            -- Little RAM
            if slot_addr(15 downto 8)=X"DF" then
                mem_addr_i <= g_ram_base(27 downto 8) & slot_addr(7 downto 0);
                allow_write <= '1';
            else
                mem_addr_i <= g_rom_base(27 downto 20) & slot_addr(13) & ext_bank & bank_bits & slot_addr(12 downto 0);
            end if;

        when c_fc3 =>
            mem_addr_i(15 downto 0) <= bank_bits(15 downto 14) & slot_addr(13 downto 0); -- 16K banks
            
        when c_ss5 =>
            if mode_bits(1 downto 0)="00" then
                if slot_addr(15 downto 13)="100" then
                    allow_write <= '1';
                    mem_addr_i <= g_ram_base(27 downto 15) & bank_bits(15 downto 14) & slot_addr(12 downto 0);
                else
                    mem_addr_i <= g_rom_base(27 downto 16) & bank_bits(15 downto 14) & slot_addr(13 downto 0);
                end if;
            else
                mem_addr_i <= g_rom_base(27 downto 16) & bank_bits(15 downto 14) & slot_addr(13 downto 0);
            end if;

        when c_8k | c_epyx =>
            mem_addr_i(12 downto 0) <= slot_addr(12 downto 0);
            
        when c_16k | c_16k_umax =>
            mem_addr_i(13 downto 0) <= slot_addr(13 downto 0);
        
        when c_ocean128 | c_system3 | c_domark | c_ocean256 =>
            mem_addr_i <= g_rom_base(27 downto 20) & slot_addr(13) & ext_bank & bank_bits & slot_addr(12 downto 0);

--        when c_ocean256 =>
--            mem_addr_i(18 downto 0) <= ext_bank & bank_bits & slot_addr(12 downto 0);
--            mem_addr_i(19) <= slot_addr(13); -- map banks 16-31 to $A000. (second 128K)

        when others =>
            null;
        end case;

--        if eth_addr then
--            mem_addr_i(25 downto 21) <= eth_base(25 downto 21);
--            mem_addr_i(20)           <= '1'; -- indicate it is a slot access
--            allow_write            <= '1'; -- we should also be able to write to the ethernet chip
--            -- invert bit 3
--            mem_addr_i(3)            <= not slot_addr(3);
--            -- leave other bits in tact
--        end if;
    end process;

    mem_addr <= unsigned(mem_addr_i(mem_addr'range));

    slot_resp.data(7) <= bank_bits(15);
    slot_resp.data(6) <= '1';
    slot_resp.data(5) <= '0';
    slot_resp.data(4) <= bank_bits(14);
    slot_resp.data(3) <= bank_bits(13);
    slot_resp.data(2) <= '0'; -- freeze button pressed
    slot_resp.data(1) <= allow_bank; -- '1'; -- allow bank bit stuck at '1' for 1541U
    slot_resp.data(0) <= '0';
    
    slot_resp.reg_output <= '1' when (slot_addr(8 downto 1)="00000000") and (cart_logic_d = c_retro) else '0';
    slot_resp.irq <= '0';
end gideon;
