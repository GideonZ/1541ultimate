import sys
import logging

console = logging.StreamHandler()
logger = logging.getLogger()
logger.addHandler(console)
logger.setLevel(logging.ERROR)

labels = {}

mask_signals = { 'CLK' : 0, 'DATA' : 1, 'ATN' : 2, 'SRQ' : 3 }
mask_names = [ 'CLK', 'DATA', 'ATN', 'SRQ' ]

input_signals = {
    'REGBIT0'       : 0,
    'REGBIT1'       : 1,
    'REGBIT2'       : 2,
    'REGBIT3'       : 3,
    'REGBIT4'       : 4,
    'REGBIT5'       : 5,
    'REGBIT6'       : 6,
    'REGBIT7'       : 7,
    'IRQ_EN'        : 8,
    'TALKER'        : 9,
    'LISTENER'      : 10,
    'ADDRESSED'     : 11,
    'EOI'           : 12,
    'USER_BIT0'     : 13,
    'USER_BIT1'     : 14,
    'USER_BIT2'     : 15,
    'CLK_O'         : 16,
    'DATA_O'        : 17,
    'ATN_O'         : 18,
    'SRQ_O'         : 19,
    'CLK'           : 20,
    'DATA'          : 21,
    'ATN'           : 22,
    'SRQ'           : 23,
    '_data_eq'      : 24,
    '_mask_eq'      : 25,
    'UPFIFOFULL'    : 26,
    'UP_FIFO_FULL'  : 26,
    'TIMEOUT'       : 27,
    'VALID'         : 28,
    'CTRL'          : 29,
    '0'             : 30,
    '1'             : 31 }
     
#print input_signals['UPFIFOFULL']

input_names = ['reserved']*32
for key in input_signals:
    input_names[input_signals[key]] = key
    
output_signals = {
    'DATABIT0'      : 0,
    'DATABIT1'      : 1,
    'DATABIT2'      : 2,
    'DATABIT3'      : 3,
    'DATABIT4'      : 4,
    'DATABIT5'      : 5,
    'DATABIT6'      : 6,
    'DATABIT7'      : 7,
    'REGBIT0'       : 0,
    'REGBIT1'       : 1,
    'REGBIT2'       : 2,
    'REGBIT3'       : 3,
    'REGBIT4'       : 4,
    'REGBIT5'       : 5,
    'REGBIT6'       : 6,
    'REGBIT7'       : 7,
    'IRQ_EN'        : 8,
    'TALKER'        : 9,
    'LISTENER'      : 10,
    'ADDRESSED'     : 11,
    'EOI'           : 12,
    'USER_BIT0'     : 13,
    'USER_BIT1'     : 14,
    'USER_BIT2'     : 15,
    'CLK'           : 16,
    'DATA'          : 17,
    'ATN'           : 18,
    'SRQ'           : 19 }

output_names = ['reserved']*32
for key in output_signals:
    output_names[output_signals[key]] = key


program = []
nr = 0

def _get_value(str):
    if str[0] == '$':
        val = int(str[1:], 16)
    else:
        try:
            val = int(str)
        except ValueError:
            try:
                val = labels[str]
            except KeyError:
                if phase == 2:
                    raise NameError('Unknown indentifier ' + str)
                val = 0
    return val

def _get_mask_signals(str):
    split = str.split(',')
    mask = 0
    data = 0
    for n in split:
        sig = n.split('=')
        try:
            sig_index = mask_signals[sig[0]]
        except KeyError:
            raise NameError("Invalid signal name %s on line %d" % (sig[0], nr)) 
        mask |= (1 << sig_index)
        val = _get_value(sig[1])
        if (val > 0):
            data |= (1 << sig_index)
    return (mask, data)

    
def _get_set_signals(str):
    split = str.split('=')
    try:
        out_index = output_signals[split[0]]
#        assert (reg_index < 8)
    except (KeyError,AssertionError):
        raise NameError("Invalid destination signal name %s on line %d" % (split[0], nr)) 
    
    inv = 0;
    sig = split[1]
    if (sig[0] == '-') or (sig[0] == '!'):
        inv = 1
        sig = sig[1:]
    try:
        in_index = input_signals[split[1]]
    except KeyError:
        raise NameError("Invalid source signal %s on line %d" % (split[1], nr)) 
        
    return (out_index, inv, in_index)

def _create_mask_value_expression(mask, value):
    d = ''
    for i in range(4):
        if(mask & (1<<i)) != 0:
            if d != '':
                d += " AND "
            d += "(%s = %d)" % (mask_names[i], (value >> i) & 1)
    return d

def _output_direct(data):
    global pc
    pc += 1
    if phase == 2:
        program.append("%08X" % data)

def _output_8_12(opcode, value, param):
    data = (opcode << 20) + (param << 8) + value
    _output_direct(data)

def _output_opc(opcode):
    data = (opcode << 20)
    _output_direct(data)

def add_label(line):
    logger.debug("add label '%s'. Pass %d. PC = %d" % (line, phase, pc))
    split = line.split('=')
    # print split
    for i in range(len(split)):
        split[i] = split[i].strip()

    if (phase == 1) and (split[0] in labels):
        raise NameError("Label '%s' already exists." % split[0])

    if len(split) > 1:
        labels[split[0]] = _get_value(split[1])
    else:
        labels[split[0]] = pc

##########################################################################
##
## PARSE rules for each opcode
##
##########################################################################

def _load(params):
#    constant c_opc_load     : std_logic_vector(3 downto 0) := X"0";
    val = _get_value(params)
    logger.info("PC: %03x: LOAD #%d." % (pc, val))
    _output_8_12(0, val, 0)  # 12 bit operand remains unused
    
def _pushc(params):
#    constant c_opc_pushc    : std_logic_vector(3 downto 0) := X"1";
    logger.info("PC: %03x: PUSHC" % pc)
    _output_opc(2)  # PushC does not take parameters (silly, because it could push <value>)
    
def _pushd(params):
#    constant c_opc_pushd    : std_logic_vector(3 downto 0) := X"2";
    logger.info("PC: %03x: PUSHD" % pc)
    _output_opc(3)  # PushD does not take parameters

def _pop(params):
#    constant c_opc_pop      : std_logic_vector(3 downto 0) := X"3";
    logger.info("PC: %03x: POP" % pc)
    _output_opc(1)  # Pop does not take parameters

def _sub(params):
#    constant c_opc_sub      : std_logic_vector(3 downto 0) := X"4";
    addr = _get_value(params)
    logger.info("PC: %03x: SUB $%03x" % (pc, addr))
    _output_8_12(4, 0, addr)  

def _ret(params):
#    constant c_opc_ret      : std_logic_vector(3 downto 0) := X"7";
    logger.info("PC: %03x: RET" % pc)
    _output_opc(7)  # Return does not take parameters

def _out(params):
#    constant c_opc_out      : std_logic_vector(3 downto 0) := X"5";
    (dest, inv, src) = _get_set_signals(params)
    data = (5 << 20) + (src << 24) + (inv << 29) + (dest << 0)
    dbg = ''
    dbg = output_names[dest] + " = ";
    if inv > 0:
        dbg += '!'
    dbg += input_names[src];
            
    logger.info("PC: %03x: COPY %s" % (pc, dbg))
    _output_direct(data)

    
def _jump(params):
    addr = _get_value(params)
    i = input_signals['1'] 
    logger.info("PC: %03x: JUMP $%03x" % (pc, addr))
    data = (i << 24) + (0x08 << 20) + (addr << 8)
    _output_direct(data)  # IF TRUE THEN addr
    
def _if(params):
#    constant c_opc_if       : std_logic_vector(3 downto 0) := X"8";

    spl = params.split()
    inv = 0
    if spl[0] == 'NOT':
        inv = 1
        spl = spl[1:]
    
    try:
        bit = input_signals[spl[0]]
        if (len(spl) < 2) or (spl[1] != 'THEN'):
            raise NameError("Expected 'THEN' on line %d" % nr)
        if (len(spl) < 3):
            raise NameError("Expected something after 'THEN'")
                
        addr = _get_value(spl[2])

        dbg = ''
        if inv > 0:
            dbg = "NOT "
        dbg += input_names[bit] + " THEN $%03x" % addr
        logger.info("PC: %03x: IFCTRL %s" % (pc, dbg))
        data = (0x08 << 20) + (bit << 24) + (inv << 29) + (addr << 8)

        _output_direct(data)

    except KeyError:
        # Ok, it was not a valid control bit
        if (spl[0] == 'DATABYTE'):
            if (spl[1] != 'IS'):
                raise NameError("Expected 'IS' or '=' after keyword DATABYTE")            
            if (spl[3] != 'THEN'):
                raise NameError("Expected 'THEN' after DATABYTE expression")
            value = _get_value(spl[2])
            addr = _get_value(spl[4])

            dbg = ''
            if inv > 0:
                dbg = "NOT "
            dbg += "DATAREG = $%02x THEN $%03x" % (value, addr)
            logger.info("PC: %03x: IF %s" % (pc, dbg))
            bit = input_signals['_data_eq']
            data = (0x08 << 20) + (bit << 24) + (inv << 29) + (addr << 8) + (value << 0)
            _output_direct(data )
               
        else:
            raise NameError("Unexpected check word %s on line %d." % (spl[0], nr))

    
def _wait(params):
#    constant c_opc_wait     : std_logic_vector(3 downto 0) := X"C";
#    constant c_opc_wait_not : std_logic_vector(3 downto 0) := X"D";
    spl = params.split()
    inv = 0
    next = 0
    time = 0
    sig = ( 0, 1 )  # will evaluate to false
    if len(spl) > next:
        if spl[0] == 'UNTIL':
            if spl[1] == 'NOT':
                inv = 1
                sig = _get_mask_signals(spl[2])
                next = 3
            else:
                sig = _get_mask_signals(spl[1])
                next = 2

    if len(spl) > next:
        if spl[next] == 'FOR':
            time = _get_value(spl[next+1])
            next += 2
                            
    (mask,value) = sig

    dbg = ''
    if mask>0:
        dbg = _create_mask_value_expression(mask, value)
        if inv > 0:
            dbg = "NOT (%s)" % dbg
        dbg = "UNTIL " + dbg
        
    if time>0:
        dbg += " FOR %d us" % (time)
    
    logger.info("PC: %03x: WAIT %s" % (pc, dbg))
#    print "wait",sig,inv,time,spl[next:]
    data = (inv << 29) + (input_signals['_mask_eq'] << 24) + (0x0C << 20) + (time << 8) + (mask << 4) + value
    _output_direct(data)

def unknown_mnem(params):
#    constant c_opc_irq_conf : std_logic_vector(3 downto 0) := X"E";
    print "Unknown mnemonic: '%s'" % params

def dump_bram_init():
    bram = [0]*2048
    for i in range(len(program)):
        inst = int(program[i], 16)
        bram[4*i+0] = inst & 0xFF
        bram[4*i+1] = (inst >> 8) & 0xFF
        bram[4*i+2] = (inst >> 16) & 0xFF
        bram[4*i+3] = (inst >> 24) & 0xFF
    
    for i in range(64):
        hx = ''
        for j in range(31,-1,-1):
            hx = hx + "%02X" % bram[i*32+j]
        print "        INIT_%02X => X\"%s\"," % (i, hx)
         
def dump_iec_file(filename):
    f = open(filename, "wb")
    for i in range(len(program)):
        inst = int(program[i], 16)
        b0 = inst & 0xFF
        b1 = (inst >> 8) & 0xFF
        b2 = (inst >> 16) & 0xFF
        b3 = (inst >> 24) & 0xFF
        f.write("%c%c%c%c" % (b0, b1, b2, b3))
    
    f.close()
        
mnemonics = {
    'LOAD'  : _load,
    'PUSHC' : _pushc,
    'PUSHD' : _pushd,
    'POP'   : _pop,
    'OUT'   : _out,
    'SET'   : _out,
    'IN'    : _out,
    'IF'    : _if,
    'WAIT'  : _wait,
    'RET'   : _ret,
    'SUB'   : _sub,
    'JUMP'  : _jump
    }

def parse_lines(lines):
    global nr
    nr = 0
    for line in lines:
        nr = nr + 1
        line = line.rstrip()
        comm = line.split('#', 1)
        line = comm[0]
        if(line.strip() == ''):
            continue
        line_strip = line.strip()
        if line[0] != ' ':
            add_label(line.rstrip())
            continue
        #print "Line: '%s'" % line_strip
        line_split = line_strip.split(" ", 1)
        if len(line_split) == 1:
            line_split.append("")
        try:
            f = mnemonics[line_split[0]]
        except KeyError,e:
            raise NameError("Unknown Mnemonic %s in line %d" % (line_split[0], nr))
        f(line_split[1].strip())
    
#    print labels
    

if __name__ == "__main__":
    inputfile = 'iec_code.txt'
    outputfile = 'iec_code.iec'
    
    if len(sys.argv)>1:
        inputfile = sys.argv[1]
    if len(sys.argv)>2:
        outputfile = sys.argv[2]
    f = open(inputfile, 'r')
    lines = f.readlines()
    pc = 0
    phase = 1
    logger.info("Pass 1...")
    parse_lines(lines)
    pc = 0
    phase = 2
    logger.info("Pass 2...")
    logger.setLevel(logging.WARNING)
    parse_lines(lines)

#    print program
    
    dump_bram_init()
    dump_iec_file(outputfile)
    