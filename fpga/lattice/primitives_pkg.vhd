library ieee;
use ieee.std_logic_1164.all;

package primitives_pkg is

    component FDDRRSE
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
    end component;

    component IDDR2
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
    end component;

    component ODDR2
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
    end component;

end package;
