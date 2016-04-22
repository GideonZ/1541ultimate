	component nios is
		port (
			clk_clk              : in std_logic                     := 'X';             -- clk
			reset_reset_n        : in std_logic                     := 'X';             -- reset_n
			nios2_gen2_0_irq_irq : in std_logic_vector(31 downto 0) := (others => 'X')  -- irq
		);
	end component nios;

	u0 : component nios
		port map (
			clk_clk              => CONNECTED_TO_clk_clk,              --              clk.clk
			reset_reset_n        => CONNECTED_TO_reset_reset_n,        --            reset.reset_n
			nios2_gen2_0_irq_irq => CONNECTED_TO_nios2_gen2_0_irq_irq  -- nios2_gen2_0_irq.irq
		);

