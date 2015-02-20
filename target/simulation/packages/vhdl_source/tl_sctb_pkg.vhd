-------------------------------------------------------------------------------
--
-- (C) COPYRIGHT 2006 TECHNOLUTION BV, GOUDA NL
-- | =======          I                   ==          I    =
-- |    I             I                    I          I
-- |    I   ===   === I ===  I ===   ===   I  I    I ====  I   ===  I ===
-- |    I  /   \ I    I/   I I/   I I   I  I  I    I  I    I  I   I I/   I
-- |    I  ===== I    I    I I    I I   I  I  I    I  I    I  I   I I    I
-- |    I  \     I    I    I I    I I   I  I  I   /I  \    I  I   I I    I
-- |    I   ===   === I    I I    I  ===  ===  === I   ==  I   ===  I    I
-- |                 +---------------------------------------------------+
-- +----+            |  +++++++++++++++++++++++++++++++++++++++++++++++++|
--      |            |             ++++++++++++++++++++++++++++++++++++++|
--      +------------+                          +++++++++++++++++++++++++|
--                                                         ++++++++++++++|
--              A U T O M A T I O N     T E C H N O L O G Y         +++++|
--
-------------------------------------------------------------------------------
-- Title      : Support package for self-checking testbenches
-- Author     : Jonathan Hofman (jonathan.hofman@technolution.nl)
-- Author     : Ard Wiersma     (ard.wiersma@technolution.nl)
-- Author     : Edwin Hakkennes (edwin.hakkennes@technolution.nl)
-------------------------------------------------------------------------------
-- Description: Support package for self-checking testbenches 
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library std;
use std.textio.all;

library work;
use work.tl_string_util_pkg.all;

package tl_sctb_pkg is
    ---------------------------------------------------------------------------
    -- constants for the log-level
    ---------------------------------------------------------------------------
    constant c_log_level_error   : integer := 0;
    constant c_log_level_warning : integer := 1;
    constant c_log_level_trace   : integer := 2;

    ---------------------------------------------------------------------------
    -- procedures to open and close the simulation
    ---------------------------------------------------------------------------
    procedure sctb_open_simulation(testcase_name    : string;
                                   output_file_name : string;
                                   log_level        : integer := c_log_level_error);
    procedure sctb_close_simulation(force: boolean := false);

    ---------------------------------------------------------------------------
    -- procedures to open and close a region within a simulation
    ---------------------------------------------------------------------------
    procedure sctb_open_region(region_name     : string;
                               expected_errors : integer := 0);
    procedure sctb_open_region(region_name     : string;
                               msg             : string;
                               expected_errors : integer := 0);
    procedure sctb_close_region;

    ---------------------------------------------------------------------------
    -- procedures to test conditions and report strings
    ---------------------------------------------------------------------------
    procedure sctb_assert(condition : boolean;
                          msg       : string);
    procedure sctb_error(msg   : string);
    procedure sctb_warning(msg : string);
    procedure sctb_trace(msg   : string);

    ---------------------------------------------------------------------------
    -- procedures to check correctness
    ---------------------------------------------------------------------------
    procedure sctb_check (
        data_a : in std_logic_vector;
        data_b : in std_logic_vector;
        msg    : in string);

    procedure sctb_check (
        data_a : in unsigned;
        data_b : in unsigned;
        msg    : in string);
    
    procedure sctb_check (
        data_a : in signed;
        data_b : in signed;
        msg    : in string);

    procedure sctb_check (
        data_a : in std_logic;
        data_b : in std_logic;
        msg    : in string);

    procedure sctb_check (
        data_a : in integer;
        data_b : in integer;
        msg    : in string);

    procedure sctb_check (
        data_a : in boolean;
        data_b : in boolean;
        msg    : in string);

    ---------------------------------------------------------------------------
    -- procedures to expect expected value correctness
    ---------------------------------------------------------------------------
    procedure sctb_expect (
        expect  : in std_logic_vector;
        got     : in std_logic_vector;
        msg     : in string);

    procedure sctb_expect (
        expect  : in unsigned;
        got     : in unsigned;
        msg     : in string);
        
    procedure sctb_expect (
        expect  : in signed;
        got     : in signed;
        msg     : in string);

    procedure sctb_expect (
        expect  : in std_logic;
        got     : in std_logic;
        msg     : in string);

    procedure sctb_expect (
        expect  : in integer;
        got     : in integer;
        msg     : in string);

    procedure sctb_expect (
        expect  : in boolean;
        got     : in boolean;
        msg     : in string);

    ---------------------------------------------------------------------------
    -- procedures to control the output
    ---------------------------------------------------------------------------
    procedure sctb_set_log_level(log_level : integer);
    procedure sctb_log_to_console(input    : boolean := true);
    
    ---------------------------------------------------------------------------
    -- signals
    ---------------------------------------------------------------------------
     shared variable v_current_region: string(1 to 32);
    
end package;

package body tl_sctb_pkg is
    ---------------------------------------------------------------------------
    -- private types, variables and constants
    ---------------------------------------------------------------------------
    type t_string_ptr is access string;
    type t_error_simulation is
    record
        output_file            : t_string_ptr;
        testcase_name          : t_string_ptr;
        region_active          : boolean;
        nr_of_regions          : integer;
        nr_of_failed_regions   : integer;
        nr_of_succeded_regions : integer;
        log_level              : integer;
    end record;

    shared variable c_error_simulation_rst : t_error_simulation := (
        output_file            => null,
        testcase_name          => null,
        region_active          => false,
        nr_of_regions          => 0,
        nr_of_failed_regions   => 0,
        nr_of_succeded_regions => 0,
        log_level              => c_log_level_error);
    shared variable error_simulation : t_error_simulation := c_error_simulation_rst;

    type t_error_region is
    record
        name            : t_string_ptr;
        error_count     : integer;
        expected_errors : integer;
    end record;

    shared variable c_error_region_rst : t_error_region := (
        name            => null,
        error_count     => 0,
        expected_errors => 0);
    shared variable error_region : t_error_region;

    file output_file : text;

    shared variable v_log_to_console : boolean := true;

    ---------------------------------------------------------------------------
    -- private procedures
    ---------------------------------------------------------------------------
    -- purpose: write a line to the error log only, intended for pretty-printing
    procedure write_line_log(str : string) is
        variable v_line : line;
    begin
        if error_simulation.output_file = null then
            print("[SCTB]" & str);
        else
            write(v_line, str);
            writeline(output_file, v_line);
        end if;
    end procedure;

    -- purpose: write a line to the error log and the console, conditionally
    procedure write_line_cond (
        str : string) is
    begin  -- write_line
        if v_log_to_console then
            print("[SCTB]"& str);
        end if;
        write_line_log(str);
    end write_line_cond;

    -- purpose: write a line to the error log and the console, unconditionally
    procedure write_line (
        str : string) is
    begin  -- write_line
        print("[SCTB] " & time'image(now) & ": " & str);
        write_line_log(str);
    end write_line;

    procedure write_line_of_stars is
    begin  -- write_line_of_stars
        write_line_cond("*******************************************************************************");
    end write_line_of_stars;

    procedure print_header is
    begin  -- print_header
        write_line_log("*******************************************************************************");
        write_line_log("**                                                                           **");
        write_line_log("**  (C) COPYRIGHT 2006 TECHNOLUTION BV, GOUDA NL                             **");
        write_line_log("**  | =======          I                   ==          I    =                **");
        write_line_log("**  |    I             I                    I          I                     **");
        write_line_log("**  |    I   ===   === I ===  I ===   ===   I  I    I ====  I   ===  I ===   **");
        write_line_log("**  |    I  /   \ I    I/   I I/   I I   I  I  I    I  I    I  I   I I/   I  **");
        write_line_log("**  |    I  ===== I    I    I I    I I   I  I  I    I  I    I  I   I I    I  **");
        write_line_log("**  |    I  \     I    I    I I    I I   I  I  I   /I  \    I  I   I I    I  **");
        write_line_log("**  |    I   ===   === I    I I    I  ===  ===  === I   ==  I   ===  I    I  **");
        write_line_log("**  |                 +---------------------------------------------------+  **");
        write_line_log("**  +----+            |  +++++++++++++++++++++++++++++++++++++++++++++++++|  **");
        write_line_log("**       |            |             ++++++++++++++++++++++++++++++++++++++|  **");
        write_line_log("**       +------------+                          +++++++++++++++++++++++++|  **");
        write_line_log("**                                                          ++++++++++++++|  **");
        write_line_log("**               A U T O M A T I O N     T E C H N O L O G Y         +++++|  **");
        write_line_log("**                                                                           **");
        write_line_log("*******************************************************************************");
    end print_header;

    ---------------------------------------------------------------------------
    -- procedures to open and close the simulation
    ---------------------------------------------------------------------------
    procedure sctb_open_simulation(
        testcase_name    : string;
        output_file_name : string;
        log_level        : integer := c_log_level_error) is
        variable testcase_name_int    : string(1 to 80);
        variable testcase_name_length : natural := 0;
        variable v_stat               : file_open_status;
    begin
        l_name_format : for i in 1 to 80 loop
            exit l_name_format when testcase_name(i+1) = ':';
            testcase_name_int(i) := testcase_name(i+1);
            testcase_name_length := i;
        end loop l_name_format;  -- i

        error_simulation             := c_error_simulation_rst;
        error_simulation.output_file := new string'(output_file_name);
        error_simulation.testcase_name :=
            new string'(testcase_name_int(1 to testcase_name_length));

        file_open(v_stat, output_file, output_file_name, write_mode);
        assert (v_stat = open_ok)
            report "tl_sctb_pkg: Could not open file " & output_file_name & " for writing."
            severity failure;
        print_header;
        write_line_log("");
        write_line_of_stars;
        write_line_cond("** Opening simulation : " &
                        error_simulation.testcase_name.all);
        write_line_of_stars;
        sctb_set_log_level(log_level);
    end procedure;

    procedure sctb_close_simulation(force: boolean := false) is
    begin
        assert(error_simulation.region_active = false)
            report "tl_sctb_pkg: close active region before closing simulation!!"&
            "Please correct your testbench and run the test again."
            severity failure;
        write_line_log("");
        write_line_of_stars;
        if error_simulation.nr_of_failed_regions > 0 then
            write_line_cond("** Closing simulation : "&
                            error_simulation.testcase_name.all);
            write_line("** Simulation verdict : FAILURE - " &
                       integer'image(error_simulation.nr_of_failed_regions)
                       & " region(s) failed!");
            write_line_of_stars;
            file_close(output_file);
        else
            write_line_cond("** Closing simulation : "&
                            error_simulation.testcase_name.all);
            write_line_cond("** Simulation verdict : SUCCESSFUL");
            write_line_of_stars;
            file_close(output_file);
        end if;

        if force then
            report "simulation end forced using report failure (this does not imply a simulation error). " &
                   "See simulation verdict for actual simulation result!"
            severity failure;
        end if;
    end procedure;

    ---------------------------------------------------------------------------
    -- procedures to open and close a region within a simulation
    ---------------------------------------------------------------------------
    procedure sctb_open_region(
        region_name     : string;
        msg             : string;
        expected_errors : integer := 0) is
    begin
        assert(error_simulation.region_active = false)
            report "tl_sctb_pkg: testbench failure - opening region """ & region_name
            & """ without closing the last region first." &
            "Please correct your testbench and run the test again."
            severity failure;
        write_line_log("");
        write_line_of_stars;
        if msg = "" then
            write_line_cond("** Opening region     : " & region_name);
        else
            write_line_cond("** Opening region     : " & region_name & " - " & msg);
        end if;
        write_line_cond("** Expected errors    : " & integer'image(expected_errors));
        write_line_log("**");
        if error_region.name /= null then
            deallocate(error_region.name);
        end if;
        error_region                   := c_error_region_rst;
        error_region.name              := new string'(region_name);
        v_current_region               := resize(region_name, v_current_region'length);
        error_region.expected_errors   := expected_errors;
        error_simulation.region_active := true;
    end procedure;
    
    procedure sctb_open_region(
        region_name     : string;
        expected_errors : integer := 0) is
    begin
        sctb_open_region(region_name, "", expected_errors);
    end procedure;

    procedure sctb_close_region is
    begin
        assert(error_simulation.region_active = true)
            report "tl_sctb_pkg: testbench failure - closing an inactive region." &
            "Please correct your testbench and run the test again."
            severity failure;
        write_line_log("**");
        write_line_cond("** Closing region     : " & error_region.name.all);
        write_line_cond("** Expected errors    : " & integer'image(error_region.expected_errors));
        write_line_cond("** Errors             : " & integer'image(error_region.error_count));
        if error_region.error_count /= error_region.expected_errors then
            write_line_cond("** Test verdict       : FAILED");
            error_simulation.nr_of_failed_regions := error_simulation.nr_of_failed_regions + 1;
        else
            write_line_cond("** Test verdict       : SUCCEEDED");
            error_simulation.nr_of_succeded_regions := error_simulation.nr_of_succeded_regions + 1;
        end if;
        write_line_of_stars;
        error_simulation.region_active := false;
        v_current_region               := resize("none", v_current_region'length);
    end procedure;

    ---------------------------------------------------------------------------
    -- procedures to test conditions and report strings
    ---------------------------------------------------------------------------
    procedure sctb_assert(condition : boolean;
                          msg       : string) is
    begin
        if not condition then
            sctb_error(msg);
        end if;
    end procedure;

    procedure sctb_error(msg : string) is
    begin
        assert (error_simulation.region_active)
            report "tl_sctb_pkg: testbench failure - Reporting an error while no error region is active." &
            "Please correct your testbench and run the test again."
            severity failure;
        error_region.error_count    := error_region.error_count + 1;
        if error_region.error_count <= error_region.expected_errors then
            write_line("(ERROR):   " & msg);
        else
            write_line("[ERROR]:   " & msg);
        end if;
    end procedure;

    procedure sctb_warning(msg : string) is
    begin
        if error_simulation.log_level >= c_log_level_warning then
            write_line("(WARNING): " & msg);
        end if;
    end procedure;

    procedure sctb_trace(msg : string) is
    begin
        if error_simulation.log_level >= c_log_level_trace then
            write_line("(TRACE):   " & msg);
        end if;
    end procedure;

    ---------------------------------------------------------------------------
    -- procedures to check expected value 
    ---------------------------------------------------------------------------
    procedure sctb_expect (
        expect  : in std_logic_vector;
        got     : in std_logic_vector;
        msg     : in string) is
    begin
        sctb_assert (
            condition => (expect = got),
            msg       => msg & " : Expected: 0x" & hstr(expect)
            &" Got : 0x" & hstr(got));       
    end;
    
    procedure sctb_expect (
        expect  : in unsigned;
        got     : in unsigned;
        msg     : in string)is
    begin
        sctb_assert (
            condition => (expect = got),
            msg       => msg & " : Expected: 0x" & hstr(expect)
            &" Got : 0x" & hstr(got));     
    end;
    
    procedure sctb_expect (
        expect  : in signed;
        got     : in signed;
        msg     : in string)is
    begin
        sctb_expect(to_integer(expect),to_integer(got),msg);
    end;

    procedure sctb_expect (
        expect  : in std_logic;
        got     : in std_logic;
        msg     : in string)is
    begin
        sctb_assert (
            condition => (expect = got),
            msg       => msg & " : Expected: " & std_logic'image(expect)
            &" Got : " & std_logic'image(got));     
    end;

    procedure sctb_expect (
        expect  : in integer;
        got     : in integer;
        msg     : in string)is
    begin
        sctb_assert (
            condition => (expect = got),
            msg       => msg & " : Expected: " & integer'image(expect)
            &" Got : " & integer'image(got));     
    end;

    procedure sctb_expect (
        expect  : in boolean;
        got     : in boolean;
        msg     : in string)is
    begin
        sctb_assert (
            condition => (expect = got),
            msg       => msg & " : Expected: " & boolean'image(expect)
            &" Got : " & boolean'image(got));     
    end;

    
    ---------------------------------------------------------------------------
    -- procedures to check correctness
    ---------------------------------------------------------------------------
    procedure sctb_check (
        data_a : in std_logic_vector;
        data_b : in std_logic_vector;
        msg    : in string) is
    begin
        sctb_assert (
            condition => (data_a = data_b),
            msg       => msg & " data_a : 0x" & hstr(data_a)
            &" not identical to data_b : 0x" & hstr(data_b));       
    end;

    procedure sctb_check (
        data_a : in unsigned;
        data_b : in unsigned;
        msg    : in string)is
    begin
        sctb_assert (
            condition => (data_a = data_b),
            msg       => msg & " data_a : 0x" & hstr(data_a)
            &" not identical to data_b : 0x" & hstr(data_b));
    end;
    
    procedure sctb_check (
        data_a : in signed;
        data_b : in signed;
        msg    : in string)is
    begin
        if data_a'length <= 32 then
            sctb_assert (
                condition => (data_a = data_b),
                msg       => msg & " data_a : " & str(to_integer(data_a)) & " (0x" & hstr(std_logic_vector(data_a))
                &") not identical to data_b : " & str(to_integer(data_b)) & " (0x" & hstr(std_logic_vector(data_b)) & ")");
        else
            sctb_assert (
                condition => (data_a = data_b),
                msg       => msg & " data_a : 0x" & hstr(std_logic_vector(data_a))
                &" not identical to data_b : 0x" & hstr(std_logic_vector(data_b)));
        end if;
    end;

    procedure sctb_check (
        data_a : in std_logic;
        data_b : in std_logic;
        msg    : in string)is
    begin
        sctb_assert (
            condition => (data_a = data_b),
            msg       => msg & " data_a : "&std_logic'image(data_a)
            &" not identical to data_b : "&std_logic'image(data_b));       
    end;

    procedure sctb_check (
        data_a : in integer;
        data_b : in integer;
        msg    : in string)is
    begin
        sctb_assert (
            condition => (data_a = data_b),
            msg       => msg & " data_a : "&integer'image(data_a)
            &" not identical to data_b : "&integer'image(data_b));       
    end;

    procedure sctb_check (
        data_a : in boolean;
        data_b : in boolean;
        msg    : in string)is
    begin
        sctb_assert (
            condition => (data_a = data_b),
            msg       => msg & " data_a : "&boolean'image(data_a)
            &" not identical to data_b : "&boolean'image(data_b));
    end;

    ---------------------------------------------------------------------------
    -- procedures to control the output
    ---------------------------------------------------------------------------
    procedure sctb_set_log_level(log_level : integer) is
    begin
        error_simulation.log_level := log_level;
    end procedure;

    procedure sctb_log_to_console(input : boolean := true) is
    begin
        v_log_to_console := input;
    end sctb_log_to_console;
    
end;

