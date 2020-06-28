library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.slot_bus_pkg.all;

entity all_carts_v4 is
generic (
    g_kernal_base   : std_logic_vector(27 downto 0) := X"0EC8000"; -- multiple of 32K
    g_rom_base      : std_logic_vector(27 downto 0) := X"0F00000"; -- multiple of 1M
    g_georam_base   : std_logic_vector(27 downto 0) := X"1000000"; -- Shared with reu
    g_ram_base      : std_logic_vector(27 downto 0) := X"0EF0000" ); -- multiple of 64K

port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    
    RST_in          : in  std_logic;
    c64_reset       : in  std_logic;
    
    kernal_enable   : in  std_logic;
    kernal_16k      : in  std_logic;
    kernal_area     : in  std_logic;
    freeze_trig     : in  std_logic; -- goes '1' when the button has been pressed and we're waiting to enter the freezer
    freeze_act      : in  std_logic; -- goes '1' when we need to switch in the cartridge for freeze mode
    unfreeze        : out std_logic; -- indicates the freeze logic to switch back to non-freeze mode.
    cart_active     : out std_logic; -- indicates that the cartridge is active

    cart_kill       : in  std_logic;
    cart_logic      : in  std_logic_vector(4 downto 0);   -- 1 out of 32 logic emulations
    cart_force      : in  std_logic;

    slot_req        : in  t_slot_req;
    slot_resp       : out t_slot_resp := c_slot_resp_init;

    epyx_timeout    : in  std_logic;
    serve_enable    : out std_logic; -- enables fetching bus address PHI2=1
    serve_vic       : out std_logic; -- enables doing so for PHI2=0
    serve_rom       : out std_logic; -- ROML or ROMH
    serve_io1       : out std_logic; -- IO1n
    serve_io2       : out std_logic; -- IO2n
    allow_write     : out std_logic;
    kernal_bank     : out  std_logic_vector(1 downto 0);

    mem_addr        : out unsigned(25 downto 0);   

    irq_n           : out std_logic;
    nmi_n           : out std_logic;
    exrom_n         : out std_logic;
    game_n          : out std_logic;
    sense           : in  std_logic;

    CART_LEDn       : out std_logic;
    size_ctrl       : in  std_logic_vector(2 downto 0) := "001" );

end all_carts_v4;    

architecture gideon of all_carts_v4 is
    signal reset_in     : std_logic;

    signal ext_bank     : std_logic_vector(18 downto 16);
    signal bank_bits    : std_logic_vector(15 downto 13);
    signal mode_bits    : std_logic_vector(2 downto 0);
    signal ef_write     : std_logic_vector(2 downto 0);
    signal ef_write_addr : std_logic_vector(21 downto 0);
    signal georam_bank  : std_logic_vector(15 downto 0);
    
--    signal rom_enable   : std_logic;

    signal freeze_act_d : std_logic;

    signal cart_en      : std_logic;

    signal do_io2       : std_logic;
    signal allow_bank   : std_logic;
    signal hold_nmi     : std_logic;
    signal cart_logic_d : std_logic_vector(cart_logic'range) := (others => '0');
    signal mem_addr_i   : std_logic_vector(27 downto 0);
        
    constant c_none         : std_logic_vector(4 downto 0) := "00000";
    constant c_8k           : std_logic_vector(4 downto 0) := "00001";
    constant c_16k          : std_logic_vector(4 downto 0) := "00010";
    constant c_16k_umax     : std_logic_vector(4 downto 0) := "00011";
    constant c_fc3          : std_logic_vector(4 downto 0) := "00100";
    constant c_ss5          : std_logic_vector(4 downto 0) := "00101";
    constant c_retro        : std_logic_vector(4 downto 0) := "00110";
    constant c_action       : std_logic_vector(4 downto 0) := "00111";
    constant c_system3      : std_logic_vector(4 downto 0) := "01000";
    constant c_domark       : std_logic_vector(4 downto 0) := "01001";
    constant c_ocean128     : std_logic_vector(4 downto 0) := "01010";
    constant c_ocean256     : std_logic_vector(4 downto 0) := "01011";
    constant c_easy_flash   : std_logic_vector(4 downto 0) := "01100";
    constant c_epyx         : std_logic_vector(4 downto 0) := "01110";
    constant c_kcs          : std_logic_vector(4 downto 0) := "10000";
    constant c_fc           : std_logic_vector(4 downto 0) := "10001";
    constant c_comal80      : std_logic_vector(4 downto 0) := "10010";
    constant c_sbasic       : std_logic_vector(4 downto 0) := "10011";
    constant c_westermann   : std_logic_vector(4 downto 0) := "10100";
    constant c_georam       : std_logic_vector(4 downto 0) := "10101";
    constant c_bbasic       : std_logic_vector(4 downto 0) := "10110";
    constant c_pagefox      : std_logic_vector(4 downto 0) := "10111";
    constant c_128          : std_logic_vector(4 downto 0) := "11000";
    constant c_fc3plus      : std_logic_vector(4 downto 0) := "11001";
    constant c_comal80pakma : std_logic_vector(4 downto 0) := "11010";
    constant c_supergames   : std_logic_vector(4 downto 0) := "11011";
    constant c_nordic       : std_logic_vector(4 downto 0) := "11100";
    
    constant c_serve_rom_rr : std_logic_vector(0 to 7) := "11011111";
    constant c_serve_io_rr  : std_logic_vector(0 to 7) := "10101111";
    
    -- alias
    signal slot_addr        : std_logic_vector(15 downto 0);
    signal slot_rwn         : std_logic;
    signal io_read          : std_logic;
    signal io_write         : std_logic;
    signal io_addr          : std_logic_vector(8 downto 0);
    signal io_wdata         : std_logic_vector(7 downto 0);
    signal georam_mask      : std_logic_vector(15 downto 0);

begin
    with size_ctrl select georam_mask <=
        "0000000111111111" when "000",
        "0000001111111111" when "001",
        "0000011111111111" when "010",
        "0000111111111111" when "011",
        "0001111111111111" when "100",
        "0011111111111111" when "101",
        "0111111111111111" when "110",
        "1111111111111111" when others;


    serve_enable <= cart_en or kernal_enable;
    cart_active  <= cart_en;

    slot_addr <= std_logic_vector(slot_req.bus_address);
    slot_rwn  <= slot_req.bus_rwn;
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
                georam_bank  <= (others => '0');
                ef_write     <= (others => '0');
                ef_write_addr <= (others => '0');
                allow_bank   <= '0';
                do_io2       <= '1';
                cart_en      <= '1';
--                unfreeze     <= '0';
                hold_nmi     <= '0';
            elsif freeze_act='1' and freeze_act_d='0' then
                bank_bits  <= (others => '0');
                mode_bits  <= (others => '0');
                --allow_bank <= '0';
                cart_en    <= '1';
--                unfreeze   <= '0';
                hold_nmi   <= '1';
            elsif cart_en = '0' then 
                cart_logic_d <= cart_logic; -- activate change of mode!
            end if;

            if cart_force = '1' then
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
                                    
            when c_fc3plus =>
                if io_write='1' and io_addr(8 downto 0) = "111111111" and cart_en='1' then -- DFFF
                    bank_bits <= io_wdata(1 downto 0) & '0';
                    ext_bank  <= '0' & io_wdata(3 downto 2);
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
                                    
            when c_action =>
                if io_write='1' and io_addr(8) = '0' and cart_en='1' then
                    bank_bits <= io_wdata(7) & io_wdata(4 downto 3);
                    mode_bits <= io_wdata(5) & io_wdata(1 downto 0);
                    unfreeze  <= io_wdata(6);
                    cart_en   <= not io_wdata(2);
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

            when c_retro =>
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

            when c_nordic =>
                if io_write='1' and io_addr(8) = '0' and cart_en='1' then
                    bank_bits <= io_wdata(7) & io_wdata(4 downto 3);
                    mode_bits <= io_wdata(5) & io_wdata(1 downto 0);
                    unfreeze  <= io_wdata(6);
                    cart_en   <= not io_wdata(2);
                end if;
                if freeze_act='1' then
                    game_n    <= '0';
                    exrom_n   <= '1';
                    serve_rom <= '1';
                    serve_io1 <= '0';
                    serve_io2 <= '0';
                else
                    if mode_bits(2 downto 0)="110" then
                       game_n    <= '0';
                       -- Switch to Ultimax mode for writes to address A000-BFFF (disable C64 RAM write)
                       exrom_n   <= slot_addr(15) and not slot_addr(14) and slot_addr(13) and not slot_rwn;
                    else 
                       game_n    <= not mode_bits(0);
                       exrom_n   <= mode_bits(1);
                    end if;
                    serve_io1 <= c_serve_io_rr(to_integer(unsigned(mode_bits)));
                    serve_io2 <= c_serve_io_rr(to_integer(unsigned(mode_bits))) and do_io2;
                    serve_rom <= c_serve_rom_rr(to_integer(unsigned(mode_bits)));
                end if;
                irq_n     <= not(freeze_trig or freeze_act);
                nmi_n     <= not(freeze_trig or freeze_act);

            when c_easy_flash =>
                if io_write='1' and io_addr(8)='0' and cart_en='1' then -- DExx
                    if io_addr(3 downto 0)="0000" then -- DE00
                        ext_bank  <= io_wdata(5 downto 3);
                        bank_bits <= io_wdata(2 downto 0);
                    end if;
                    if io_addr(3 downto 0)="0010" then -- DE02
                        mode_bits <= io_wdata(2 downto 0); -- LED not implemented
                    end if;
                    if io_addr(3 downto 0)="1001" then -- DE09
                        ef_write <= "000";
                    end if;
                    if io_addr(3 downto 0)="1000" then -- DE08
                        case ef_write is
                           when "000" =>
                              if io_wdata(7 downto 0) = X"65" then
                                 ef_write <= "001";
                              end if;
                           when "001" =>
                              if io_wdata(7 downto 0) = X"66" then
                                 ef_write <= "010";
                              else
                                 ef_write <= "000";
                              end if;
                           when "010" =>
                              if io_wdata(7 downto 0) = X"77" then
                                 ef_write <= "011";
                              else
                                 ef_write <= "000";
                              end if;
                           when "011" =>
                                  ef_write_addr(7 downto 0) <= io_wdata(7 downto 0);
                                  ef_write <= "100";
                           when "100" =>
                                  ef_write_addr(12 downto 8) <= io_wdata(4 downto 0);
                                  ef_write_addr(19) <= io_wdata(5);
                                  ef_write <= "101";
                           when "101" =>
                                  ef_write_addr(18 downto 13) <= io_wdata(5 downto 0);
                                  ef_write <= "110";
                           when others =>
                                 ef_write <= "000";
                        end case;
                    end if;
                end if;
                game_n    <= not (mode_bits(0) or not mode_bits(2));
                exrom_n   <= not mode_bits(1);
                serve_rom <= '1';
                serve_io1 <= '1'; -- write registers only, no reads
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
                if io_write='1' and io_addr(8 downto 0) = "111111111" then -- DFFF
                    if cart_en='1' and io_wdata(7 downto 6) = "01" then
                        cart_en <= '0'; -- permanent off
                    end if;
                end if;
                game_n    <= '1';
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '1'; -- for EPYX test
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_16k =>
                if io_write='1' and io_addr(8 downto 0) = "111111111" then -- DFFF
                    if cart_en='1' and io_wdata(7 downto 6) = "01" then
                        cart_en <= '0'; -- permanent off
                    end if;
                end if;
                game_n    <= '0';
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_16k_umax =>
                if io_write='1' and io_addr(8 downto 0) = "111111111" and cart_en='1' and io_wdata(7 downto 6) = "01" then -- DFFF
                    cart_en <= '0'; -- permanent off
                end if;
                game_n    <= '0';
                exrom_n   <= '1';
                serve_rom <= '1';
                serve_vic <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_128 =>
                game_n    <= '1';
                exrom_n   <= '1';
                serve_rom <= '1';
                serve_io1 <= '1';
                serve_io2 <= '1';
                irq_n     <= '1';
                nmi_n     <= '1';
                serve_vic <= '1';

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

            when c_comal80 => -- 64K, 4x16K banks
                if io_write='1' and io_addr(8)='0' then -- DE00-DEFF
                    bank_bits <= io_wdata(1 downto 0) & '0';
                end if;
                game_n    <= '0';
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';


            when c_comal80pakma =>
                if io_write='1' and io_addr(8)='0' then -- DE00-DEFF
                    bank_bits <= io_wdata(1 downto 0) & '0';
                    ext_bank <= "00" & io_wdata(2);
                end if;
                game_n    <= '0';
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_supergames =>
                if io_write='1' and io_addr(8)='1' and mode_bits(1) = '0' then -- DF00-DFFF
                    bank_bits <= io_wdata(1 downto 0) & '0';
                    mode_bits(1 downto 0) <= io_wdata(3 downto 2);                
                end if;
                if mode_bits(1 downto 0) = "11" then -- Mostly to visualize
                    cart_en <= '0';
                end if;
                game_n    <= mode_bits(0);
                exrom_n   <= mode_bits(0);
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_sbasic => -- 16K, upper 8k enabled by writing to DExx
                             -- and disabled by reading
                if io_write='1' and io_addr(8)='0' then
                    mode_bits(0) <= '1';
                elsif io_read='1' and io_addr(8)='0' then
                    mode_bits(0) <= '0';
                end if;
                game_n    <= not mode_bits(0);
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_westermann => -- 16K, upper 8k disabled by reading to DFxx
                             -- and disabled by reading
                if io_read='1' and io_addr(8)='1' then
                    mode_bits(0) <= '1';
                end if;
                game_n    <= mode_bits(0);
                exrom_n   <= '0';
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_pagefox => -- 16K, upper 8k disabled by reading to DFxx
                             -- and disabled by reading
                if io_write='1' and io_addr(8 downto 7) = "01"  then
                   mode_bits(0) <= io_wdata(4);
                   bank_bits <= io_wdata(3 downto 1);
                end if;
                game_n    <= mode_bits(0);
                exrom_n   <= mode_bits(0);
                serve_rom <= '1';
                serve_io1 <= '0';
                serve_io2 <= '0';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_georam =>
                if io_write='1' and io_addr(8 downto 7) = "11" then
                    if io_addr(0) = '0' then
                        georam_bank(5 downto 0) <= io_wdata(5 downto 0) and georam_mask(5 downto 0);
                            georam_bank(15 downto 14) <= io_wdata(7 downto 6) and georam_mask(15 downto 14);
                    else
                        georam_bank(13 downto 6) <= io_wdata(7 downto 0) and georam_mask(13 downto 6);
                    end if; 
                end if;
                game_n    <= '1';
                exrom_n   <= '1';
                serve_rom <= '1';
                serve_io1 <= '1';
                serve_io2 <= '1';
                irq_n     <= '1';
                nmi_n     <= '1';

            when c_bbasic =>
                if io_write='1' and io_addr(8)='0' then
                    mode_bits(0) <= '0';
                elsif io_read='1' and io_addr(8)='0' then
                    mode_bits(0) <= '1';
                end if;
                        if mode_bits(0)='1' then
                   game_n    <= '0';
                   exrom_n   <= '0';
                elsif slot_addr(15)='1' and not(slot_addr(14 downto 13) = "10") then
                   game_n    <= '0';
                   exrom_n   <= '1';
                else
                   game_n    <= '1';
                   exrom_n   <= '1';
                end if;
                serve_rom <= '1';
                serve_io1 <= '1';
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
            
            when c_kcs =>
-- M1 M0 Ga Act      | M1 M0 Ex Ga              | M2 M1 M0 Recoded
---------------------+--------------------------+-----------------
--  x  x  x Reset    |  0  0  0  0  (reset:16K) |  0  0  0  16K
--  x  x  x Freeze   |  1  1  1  0  (freeze)    |  0  1  0  UmaxF  
--  x  x  x R:DE00   |  1  0  0  1  (8K mode)   |  0  0  1   8K
--  x  x  x R:DE02   |  1  0  1  1  (off1)      |  0  1  1  Off1
--  x  x  x W:DE80   |  0  0  0  0  (reset:16K) |  0  0  0  16K
--  0  x  0 W:DE0x   |  0  1  1  0  (Ultimax)   |  1  1  0  UmaxS
--  0  x  1 W:DE0x   |  0  1  1  1  (Off2)      |  1  1  1  Off2
--  1  1  x W:DE00   |  0  0  0  0  (reset:16K) |  0  0  0  16K
--  1  1  x W:DE02   |  1  0  0  1  (8K mode)   |  0  0  1   8K
--   
-- 0 0 0 0 16K
-- 0 1 0 0 ?
-- 0 0 1 0 ?
-- 0 1 1 0 Ultimax
-- 
-- 0 0 0 1 ?
-- 0 1 0 1 ?
-- 0 0 1 1 ?
-- 0 1 1 1 Off2

                -- mode_bit(0) -> ULTIMAX
                -- mode_bit(1) -> 16K Mode
                -- io1 read
                if io_read='1' and io_addr(8) = '0' then -- DE00-DEFF
                    mode_bits(0) <= '1';            -- When read and addr bit 1=0 : 8k GAME mode            
                    mode_bits(1) <= io_addr(1);     -- When read and addr bit 1=1 : Cartridge disabled mode 
                    mode_bits(2) <= '0';
                end if;

                -- io1 write
                if io_write='1' and io_addr(8 downto 7) = "01" then -- DE80-DEFF
                    mode_bits <= "000"; -- 16K mode
                end if;
                if io_write='1' and io_addr(8 downto 7) = "00" then -- DE00-DE7F
                    -- if in 16K 000 / UmaxS 110 / Off2 111
                    if mode_bits = "000" then -- 16K
                        mode_bits <= "110";
                    elsif mode_bits = "010" or mode_bits = "111" then -- Freeze of Off2
                        mode_bits <= "000";          -- When addr bit 1=0 : 16k GAME mode
                        mode_bits(0) <= io_addr(1);  -- When addr bit 1=1 : 8k GAME mode 
                    end if;                    
                end if;
                -- io2 read
                if io_read='1' and io_addr(8 downto 7) = "11" then -- DF80-DFFF
                   unfreeze     <= '1';    -- When read : release freeze
                end if;
                -- on freeze
                if freeze_act='1' then
                    mode_bits <= "010";
                end if;

                game_n    <= mode_bits(0);
                exrom_n   <= mode_bits(1);
                serve_io1 <= '1';
                serve_io2 <= '1';
                serve_rom <= '1';
                serve_vic <= mode_bits(1);
                nmi_n     <= not(freeze_trig or freeze_act);

            when c_fc =>
                -- io1 access
                if io_read='1' and io_addr(8) = '0' then -- DE00-DEFF
                    game_n    <= '1';      -- Cartridge disabled mode
                    exrom_n   <= '1';
                    unfreeze  <= '1';
                end if;
                if io_write='1' and io_addr(8) = '0' then -- DE00-DEFF
                    game_n    <= '1';      -- Cartridge disabled mode
                    exrom_n   <= '1';
                    unfreeze  <= '1';
                end if;
                -- io2 access
                if io_read='1' and io_addr(8) = '1' then -- DF00-DFFF
                    game_n    <= '0';      -- 16K GAME mode
                    exrom_n   <= '0';
                    unfreeze  <= '1';
                end if;
                if io_write='1' and io_addr(8) = '1' then -- DF00-DFFF
                    game_n    <= '0';      -- 16K GAME mode
                    exrom_n   <= '0';
                    unfreeze  <= '1';
                end if;
                -- on freeze
                if freeze_trig='1' then
                    game_n       <= '0';   -- ULTIMAX mode
                    exrom_n      <= '1';
                end if;
                -- on reset/init
                if reset_in='1' then
                    game_n       <= '0';   -- 16K GAME mode
                    exrom_n      <= '0';
                    unfreeze     <= '1';
                end if;
                serve_io1 <= '1';
                serve_io2 <= '1';
                serve_rom <= '1';
                nmi_n     <= not(freeze_trig or freeze_act);

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

    -- determine address
    process(cart_logic_d, slot_addr, mode_bits, bank_bits, ext_bank, do_io2, allow_bank, 
            kernal_area, kernal_16k, georam_bank, sense, ef_write, ef_write_addr)
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
            -- 8K RAM
            if mode_bits(2)='1' then
                if slot_addr(13)='0' then
                    mem_addr_i <= g_ram_base(27 downto 15) & "00" & slot_addr(12 downto 0);
                end if;
                if slot_addr(15 downto 13)="100" then -- and mode_bits(1 downto 0)="11" then
                    allow_write <= '1';
                end if;
                if slot_addr(15 downto 8)=X"DF" and do_io2='1' then
                    allow_write <= '1';
                end if;
            end if;

        when c_nordic =>
            -- 8K RAM
            if mode_bits(2)='1' then
                if slot_addr(13)='0' then
                    mem_addr_i <= g_ram_base(27 downto 15) & "00" & slot_addr(12 downto 0);
                end if;
                if slot_addr(15 downto 13)="100" then -- and mode_bits(1 downto 0)="11" then
                    allow_write <= '1';
                end if;
                if slot_addr(15 downto 8)=X"DF" and do_io2='1' then
                    allow_write <= '1';
                end if;
            end if;
            if mode_bits(2 downto 0) ="110" then
                if slot_addr(15 downto 13)="100" then
                    mem_addr_i <= g_rom_base(27 downto 15) & bank_bits(14 downto 13) & slot_addr(12 downto 0);
                    allow_write <= '0';
                end if;
                if slot_addr(15 downto 13)="101" then
                    mem_addr_i <= g_ram_base(27 downto 15) & "00" & slot_addr(12 downto 0);
                    allow_write <= '1';
                end if;
                if slot_addr(15 downto 8)=X"DF" and do_io2='1' then
                    mem_addr_i <= g_ram_base(27 downto 15) & "00" & slot_addr(12 downto 0);
                end if;
            end if;

        when c_easy_flash =>
            -- Little RAM
            if slot_addr(15 downto 8)=X"DF" then
                mem_addr_i <= g_ram_base(27 downto 8) & slot_addr(7 downto 0);
                allow_write <= '1';
            else 
                if slot_addr(15 downto 0)=X"DE07" and ef_write = "110" then
                   mem_addr_i <= g_rom_base(27 downto 20) & ef_write_addr(19 downto 0);
                   allow_write <= '1';
               else
                   mem_addr_i <= g_rom_base(27 downto 20) & slot_addr(13) & ext_bank & bank_bits & slot_addr(12 downto 0);
               end if;
            end if;

        when c_fc3 | c_comal80 | c_fc3plus | c_comal80pakma | c_supergames =>
            mem_addr_i(17 downto 0) <= ext_bank(17 downto 16) & bank_bits(15 downto 14) & slot_addr(13 downto 0); -- 16K banks
            
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
            mem_addr_i(27 downto 13) <= g_rom_base(27 downto 13);
            mem_addr_i(12 downto 0)  <= slot_addr(12 downto 0);
            
        when c_16k | c_16k_umax =>
            mem_addr_i(27 downto 14) <= g_rom_base(27 downto 14);
            mem_addr_i(13 downto 0)  <= slot_addr(13 downto 0);
        
        when c_128 =>
            mem_addr_i(27 downto 15) <= g_rom_base(27 downto 15);
            mem_addr_i(14 downto 0)  <= slot_addr(14 downto 0);
        
        when c_ocean128 | c_system3 | c_domark | c_ocean256 =>
            mem_addr_i <= g_rom_base(27 downto 20) & slot_addr(13) & ext_bank & bank_bits & slot_addr(12 downto 0);

--        when c_ocean256 =>
--            mem_addr_i(18 downto 0) <= ext_bank & bank_bits & slot_addr(12 downto 0);
--            mem_addr_i(19) <= slot_addr(13); -- map banks 16-31 to $A000. (second 128K)

        when c_kcs =>
            -- io2 ram access
            if slot_addr(15 downto 8) = X"DF" then
                mem_addr_i <= g_ram_base(27 downto 7) & slot_addr(6 downto 0);
                allow_write <= '1';
            else
            -- rom access
               mem_addr_i <= g_rom_base(27 downto 14) & slot_addr(13 downto 0);
            end if;

        when c_fc | c_westermann =>
            -- rom access
            mem_addr_i <= g_rom_base(27 downto 14) & slot_addr(13 downto 0);

        when c_sbasic =>
            -- rom access
            mem_addr_i <= g_rom_base(27 downto 13) & slot_addr(12 downto 0);
            mem_addr_i(19) <= slot_addr(13);

        when c_bbasic =>
            -- rom access
            if slot_addr(15 downto 13)="100" then
               mem_addr_i <= g_rom_base(27 downto 15) & "00" & slot_addr(12 downto 0);
            elsif slot_addr(15 downto 13)="101" then
               mem_addr_i <= g_rom_base(27 downto 15) & "01" & slot_addr(12 downto 0);
            elsif slot_addr(15 downto 13)="111" then
               mem_addr_i <= g_rom_base(27 downto 15) & "10" & slot_addr(12 downto 0);
            end if;

        when c_georam =>
           if slot_addr(15 downto 8)=X"DE" then
              mem_addr_i <= g_georam_base(27 downto 24) & georam_bank(15 downto 0) & slot_addr(7 downto 0); 
              allow_write <= '1';
           end if;

        when c_pagefox =>
           if bank_bits(15) = '0' then
              mem_addr_i <= g_rom_base(27 downto 16) & bank_bits(14) & bank_bits(13) & slot_addr(13 downto 0); 
           elsif bank_bits(14) = '0' then
              mem_addr_i <= g_ram_base(27 downto 15) & bank_bits(13) & slot_addr(13 downto 0);
              if slot_addr(15 downto 14)="10" then
                 allow_write <= '1';
              end if;
           end if;


        when others =>
            null;
        end case;

        if kernal_area='1' then
            if kernal_16k='0' then
                mem_addr_i <= g_kernal_base(27 downto 14) & slot_addr(12 downto 0) & '0';
                kernal_bank <= "00";
            else
                mem_addr_i <= g_rom_base(27 downto 15) & slot_addr(12 downto 0) & '0' & '0';
                kernal_bank <= '0' & (not sense);
            end if;
        else
            kernal_bank <= "00";
        end if;

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

end gideon;
