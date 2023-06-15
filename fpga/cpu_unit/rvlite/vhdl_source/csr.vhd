
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.core_pkg.all;

entity csr is
generic (
    g_hart_id   : natural := 0;
    g_version   : std_logic_vector(31 downto 0) := X"475A0001"
);
port
(
    int_i       : in  std_logic;
    csr_i       : in  t_csr_req;
    csr_o       : out t_csr_resp;
    ena_i       : in  std_logic;
    reset       : in  std_logic;
    clock       : in  std_logic
);
end entity;

architecture arch of csr is
    constant c_mstatus      : std_logic_vector(11 downto 0) := X"300";
    constant c_misa         : std_logic_vector(11 downto 0) := X"301";
    constant c_mie          : std_logic_vector(11 downto 0) := X"304";
    constant c_mtvec        : std_logic_vector(11 downto 0) := X"305";
    constant c_mscratch     : std_logic_vector(11 downto 0) := X"340";
    constant c_mepc         : std_logic_vector(11 downto 0) := X"341";
    constant c_mcause       : std_logic_vector(11 downto 0) := X"342";
    --constant c_mtval        : std_logic_vector(11 downto 0) := X"343";
    constant c_mip          : std_logic_vector(11 downto 0) := X"344";

    constant marchid    : std_logic_vector(31 downto 0) := X"00000013";
    constant mimpid     : std_logic_vector(31 downto 0) := g_version;
    constant mhartid    : std_logic_vector(31 downto 0) := std_logic_vector(to_unsigned(g_hart_id, 32));
    constant misa       : std_logic_vector(31 downto 0) := X"40000100"; -- bit 8=I, bit 30=MXL Lo
    signal mscratch     : std_logic_vector(31 downto 0) := (others => '0');
    signal mstatus      : std_logic_vector(31 downto 0) := (others => '0');
    signal mcause       : std_logic_vector(31 downto 0) := (others => '0');
    signal mepc         : std_logic_vector(31 downto 0) := (others => '0');
    signal mtvec        : std_logic_vector(31 downto 0) := (others => '0');
    signal mie          : std_logic_vector(31 downto 0) := (others => '0');
    signal mip          : std_logic_vector(31 downto 0) := (others => '0');
    
    alias mstat_mie     : std_logic is mstatus(3);
    alias mstat_mpie    : std_logic is mstatus(7);
    alias mie_mei       : std_logic is mie(11);
    alias mip_mei       : std_logic is mip(11);

    signal rdata        : std_logic_vector(31 downto 0) := (others => '0');
    signal wdata        : std_logic_vector(31 downto 0) := (others => '0');
begin
    with csr_i.address select rdata <=
        --          when X"F11", -- Vendor ID
        marchid     when X"F12", -- Architecture ID (R/O)
        mimpid      when X"F13", -- Implementation ID (R/O)
        mhartid     when X"F14", -- Hardware Thread ID (Hart) (R/O)
        --          when X"F15", -- Pointer to configuration data structure
        mstatus     when c_mstatus,  -- mstatus (R/W)
        misa        when c_misa,     -- misa (R/O)
        --          when X"302",     -- medeleg
        --          when X"303",     -- mideleg
        mie         when c_mie,      -- mie (R/W)
        mtvec       when c_mtvec,    -- mtvec (R/W)
        --          when X"306",     -- mcounteren
        --          when X"310",     -- mstatush (RV32)
        mscratch    when c_mscratch, -- mscratch (R/W)
        mepc        when c_mepc,     -- mepc
        mcause      when c_mcause,   -- mcause
        --          when c_mtval,    -- mtval
        mip         when c_mip,      -- mip
        X"00000000" when others;

    with csr_i.oper select wdata <=
        csr_i.wdata when CSR_WRITE,
        csr_i.wdata or rdata when CSR_SET,
        rdata and not csr_i.wdata when CSR_CLR,
        rdata when others;

    csr_o.rdata <= rdata;
    csr_o.mtvec <= mtvec;
    csr_o.mepc  <= mepc;

    -- Generate an interrupt to the core when the machine interrupt is enabled (mie.mei),
    -- the external interrupt pin is active (int_i), the machine status bit is set to allow
    -- interrupts (mstat_mie) and, the CSR is not currently being changed. The latter is
    -- important, because when decode_o has this bit set, the next instruction could already
    -- be in the decode stage. In the decode state the instructions are replaced with an
    -- interrupt dummy, which should not happen when the interrutps are getting disabled,
    -- otherwise the instruction after interrupt disable may be executed still.
    csr_o.irq   <= mie_mei and int_i and mstat_mie and not csr_i.inhibit_irq;

    -- report pending interrupt in mip
    mip_mei <= int_i;
    
    process(clock)
    begin
        if rising_edge(clock) then
            if ena_i = '1' then
                if csr_i.enable = '1' and csr_i.oper /= CSR_NONE then -- user writes
                    case csr_i.address is
                    when c_mstatus =>
                        mstat_mie <= wdata(3);
                        mstat_mpie <= wdata(7);
                    when c_mscratch =>
                        mscratch <= wdata;
                    when c_mtvec =>
                        mtvec <= wdata(31 downto 2) & "00";
                    when c_mepc =>
                        mepc <= wdata;
                    when c_mie => -- Interrupt Enable.. which interrupts do we have?
                        mie_mei <= wdata(11);
                    when others =>
                        null;
                    end case;
                end if;
                -- overwrite by traps
                if csr_i.trap.trap = '1' then
                    mcause(31) <= csr_i.trap.cause(csr_i.trap.cause'high);
                    mcause(csr_i.trap.cause'high-1 downto 0) <= csr_i.trap.cause(csr_i.trap.cause'high-1 downto 0);
                    mepc <= csr_i.trap.program_counter;
                    mstat_mie <= '0';
                    mstat_mpie <= mstat_mie;
                elsif csr_i.trap.mret = '1' then
                    mstat_mie <= mstat_mpie;
                    mstat_mpie <= '1'; -- see 3.1.7 from Volume II: Risc-V privileged architectures
                end if;
            end if;
            -- reset: disable interrupts
            if reset = '1' then
                mstat_mie <= '0';
                mstat_mpie <= '0';
                mie <= (others => '0');
            end if;
        end if;
    end process;

end architecture;
