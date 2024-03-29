-- VHDL netlist generated by SCUBA Diamond (64-bit) 3.12.0.240.2
-- Module  Version: 5.7
--/usr/local/diamond/3.12/ispfpga/bin/lin64/scuba -w -n pll1 -lang vhdl -synth synplify -bus_exp 7 -bb -arch sa5p00 -type pll -fin 50 -fclkop 200 -fclkop_tol 2.0 -fclkos 12.288 -fclkos_tol 2.0 -phases 0 -fclkos2 50 -fclkos2_tol 2.0 -phases2 0 -fclkos3 24 -fclkos3_tol 2.0 -phases3 0 -phase_cntl DYNAMIC -enable_s -enable_s2 -enable_s3 -rst -lock -fb_mode 5 

-- Fri Sep  2 10:59:52 2022

library IEEE;
use IEEE.std_logic_1164.all;
library ECP5U;
use ECP5U.components.all;

entity pll1 is
    port (
        CLKI: in  std_logic; 
        PHASESEL: in  std_logic_vector(1 downto 0); 
        PHASEDIR: in  std_logic; 
        PHASESTEP: in  std_logic; 
        PHASELOADREG: in  std_logic; 
        RST: in  std_logic; 
        ENCLKOS: in  std_logic; 
        ENCLKOS2: in  std_logic; 
        ENCLKOS3: in  std_logic; 
        CLKOP: out  std_logic; 
        CLKOS: out  std_logic; 
        CLKOS2: out  std_logic; 
        CLKOS3: out  std_logic; 
        LOCK: out  std_logic);
end pll1;

architecture Structure of pll1 is

    -- internal signal declarations
    signal REFCLK: std_logic;
    signal CLKOS3_t: std_logic;
    signal CLKOS2_t: std_logic;
    signal CLKOS_t: std_logic;
    signal CLKOP_t: std_logic;
    signal CLKFB_t: std_logic;
    signal scuba_vhi: std_logic;
    signal scuba_vlo: std_logic;

    attribute FREQUENCY_PIN_CLKOS3 : string; 
    attribute FREQUENCY_PIN_CLKOS2 : string; 
    attribute FREQUENCY_PIN_CLKOS : string; 
    attribute FREQUENCY_PIN_CLKOP : string; 
    attribute FREQUENCY_PIN_CLKI : string; 
    attribute ICP_CURRENT : string; 
    attribute LPF_RESISTOR : string; 
    attribute FREQUENCY_PIN_CLKOS3 of PLLInst_0 : label is "24.000000";
    attribute FREQUENCY_PIN_CLKOS2 of PLLInst_0 : label is "50.000000";
    attribute FREQUENCY_PIN_CLKOS of PLLInst_0 : label is "12.244898";
    attribute FREQUENCY_PIN_CLKOP of PLLInst_0 : label is "200.000000";
    attribute FREQUENCY_PIN_CLKI of PLLInst_0 : label is "50.000000";
    attribute ICP_CURRENT of PLLInst_0 : label is "12";
    attribute LPF_RESISTOR of PLLInst_0 : label is "8";
    attribute syn_keep : boolean;
    attribute NGD_DRC_MASK : integer;
    attribute NGD_DRC_MASK of Structure : architecture is 1;

begin
    -- component instantiation statements
    scuba_vhi_inst: VHI
        port map (Z=>scuba_vhi);

    scuba_vlo_inst: VLO
        port map (Z=>scuba_vlo);

    PLLInst_0: EHXPLLL
        generic map (PLLRST_ENA=> "ENABLED", INTFB_WAKE=> "DISABLED", 
        STDBY_ENABLE=> "DISABLED", DPHASE_SOURCE=> "ENABLED", 
        CLKOS3_FPHASE=>  0, CLKOS3_CPHASE=>  24, CLKOS2_FPHASE=>  0, 
        CLKOS2_CPHASE=>  11, CLKOS_FPHASE=>  0, CLKOS_CPHASE=>  48, 
        CLKOP_FPHASE=>  0, CLKOP_CPHASE=>  2, PLL_LOCK_MODE=>  0, 
        CLKOS_TRIM_DELAY=>  0, CLKOS_TRIM_POL=> "FALLING", 
        CLKOP_TRIM_DELAY=>  0, CLKOP_TRIM_POL=> "FALLING", 
        OUTDIVIDER_MUXD=> "DIVD", CLKOS3_ENABLE=> "DISABLED", 
        OUTDIVIDER_MUXC=> "DIVC", CLKOS2_ENABLE=> "DISABLED", 
        OUTDIVIDER_MUXB=> "DIVB", CLKOS_ENABLE=> "DISABLED", 
        OUTDIVIDER_MUXA=> "DIVA", CLKOP_ENABLE=> "ENABLED", CLKOS3_DIV=>  25, 
        CLKOS2_DIV=>  12, CLKOS_DIV=>  49, CLKOP_DIV=>  3, CLKFB_DIV=>  4, 
        CLKI_DIV=>  1, FEEDBK_PATH=> "INT_OP")
        port map (CLKI=>CLKI, CLKFB=>CLKFB_t, PHASESEL1=>PHASESEL(1), 
            PHASESEL0=>PHASESEL(0), PHASEDIR=>PHASEDIR, 
            PHASESTEP=>PHASESTEP, PHASELOADREG=>PHASELOADREG, 
            STDBY=>scuba_vlo, PLLWAKESYNC=>scuba_vlo, RST=>RST, 
            ENCLKOP=>scuba_vlo, ENCLKOS=>ENCLKOS, ENCLKOS2=>ENCLKOS2, 
            ENCLKOS3=>ENCLKOS3, CLKOP=>CLKOP_t, CLKOS=>CLKOS_t, 
            CLKOS2=>CLKOS2_t, CLKOS3=>CLKOS3_t, LOCK=>LOCK, 
            INTLOCK=>open, REFCLK=>REFCLK, CLKINTFB=>CLKFB_t);

    CLKOS3 <= CLKOS3_t;
    CLKOS2 <= CLKOS2_t;
    CLKOS <= CLKOS_t;
    CLKOP <= CLKOP_t;
end Structure;
