library ieee;
use ieee.std_logic_1164.all;
library ECP5U;
use ECP5U.components.all;

-- ODDRX1F
-- Description
-- This primitive is used for Generic X1 ODDR implementation.
-- The following table gives the port description.
--
-- Signal I/O Description
-- D0     I   Data input ODDR (first to be sent out)
-- D1     I   Data input ODDR (second to be sent out)
-- SCLK   I   SCLK input
-- RST    I   Reset input
-- Q      O   DDR data output on both edges of SCLK

entity ODDR2 is
  generic (
     DDR_ALIGNMENT : string := "NONE";
     INIT : bit := '0';
     SRTYPE : string := "SYNC"
  );
  port (
     Q : out std_ulogic;
     C0 : in std_ulogic;
     C1 : in std_ulogic;
     CE : in std_ulogic := 'H';
     D0 : in std_ulogic;
     D1 : in std_ulogic;
     R : in std_ulogic := 'L';
     S : in std_ulogic := 'L'
  );
end entity;

architecture lattice of ODDR2 is
begin
    assert DDR_ALIGNMENT /= "NONE"
        report "ODDR2 behavior may differ from what you expect."
        severity warning;
    
    i_cell: ODDRX1F
    port map (
        D0   => D0,
        D1   => D1,
        SCLK => C0,
        RST  => R,
        Q    => Q
    );    
    -- S, CE and C1 are not used
    
end architecture;

library ieee;
use ieee.std_logic_1164.all;
library ECP5U;
use ECP5U.components.all;

entity FDDRRSE is
  port (
     Q : out std_ulogic;
     C0 : in std_ulogic;
     C1 : in std_ulogic;
     CE : in std_ulogic;
     D0 : in std_ulogic;
     D1 : in std_ulogic;
     R : in std_ulogic;
     S : in std_ulogic
  );
end entity;

architecture lattice of FDDRRSE is
begin
    i_cell: ODDRX1F
    port map (
        D0   => D0,
        D1   => D1,
        SCLK => C0,
        RST  => R,
        Q    => Q
    );    
    -- S, CE and C1 are not used
end architecture;

library ieee;
use ieee.std_logic_1164.all;
library ECP5U;
use ECP5U.components.all;

entity IDDR2 is
  generic (
     DDR_ALIGNMENT : string := "NONE";
     INIT_Q0 : bit := '0';
     INIT_Q1 : bit := '0';
     SRTYPE : string := "SYNC"
  );
  port (
     Q0 : out std_ulogic;
     Q1 : out std_ulogic;
     C0 : in std_ulogic;
     C1 : in std_ulogic;
     CE : in std_ulogic := 'H';
     D : in std_ulogic;
     R : in std_ulogic := 'L';
     S : in std_ulogic := 'L'
  );
end entity;

architecture lattice of IDDR2 is
begin
    i_cell: IDDRX1F
    port map (
        D    => D,
        SCLK => C0,
        RST  => R,
        Q0   => Q0,
        Q1   => Q1
    );    
    -- S, CE and C1 are not used
end architecture;
