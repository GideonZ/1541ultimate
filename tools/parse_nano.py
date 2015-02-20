import sys
import logging

console = logging.StreamHandler()
logger = logging.getLogger()
logger.addHandler(console)
logger.setLevel(logging.ERROR)

labels = {}
program = []
imm_expr = []
imm_dict = {}
imm_values = []

nr = 0

def _get_value_imm(str):
    if str[0] == '#':
        if phase == 1:
            if not str[1:] in imm_expr: # phase 1
                imm_expr.append(str[1:])
            return 0
        else: # phase == 2
            return imm_dict[_get_value(str[1:])] # phase 2

    # not an immediate value
    return _get_value(str)
        
def _get_value(str):
    if str[0] == '#':
        raise ValueError('Not allowed to use immediate value. Line %d' % nr)
        
    if str[0] == '$':
        val = int(str[1:], 16)
    elif str[0:2] == '0x':
        val = int(str[2:], 16)
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
    return val & 0xFFFF

def _output_direct(data):
    global pc
    pc += 1
    if phase == 2:
        program.append("%04X" % data)

def add_label(line):
    logger.debug("add label '%s'. Pass %d. PC = %d" % (line, phase, pc))
    split = line.split('=')

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

def _addr(params, mnem, code):
    addr = _get_value(params)
    if addr > 0x3FF:
        print "Error, address too large: %03x: %s $%03x" % (pc, mnem, addr)
        exit() 
    code |= addr
    logger.info("PC: %03x: %04x | %s $%03x" % (pc, code, mnem, addr))
    _output_direct(code)
    return code

def _addr_imm(params, mnem, code):
    addr = _get_value_imm(params)
    if addr > 0x3FF:
        print "Error, address too large: %03x: %s $%03x" % (pc, mnem, addr)
        exit() 
    code |= addr
    logger.info("PC: %03x: %04x | %s $%03x" % (pc, code, mnem, addr))
    _output_direct(code)
    return code

def _data(params, mnem, code):
    data = _get_value(params)
    logger.info("PC: %03x: %s $%04x" % (pc, mnem, data))
    _output_direct(data)
    return data
    
def _block(params, mnem, code):
    length = _get_value(params)
    logger.info("PC: %03x: %s $%04x" % (pc, mnem, length))
    for i in range(length):
        _output_direct(0)
    return 0
    
def _addr_io(params, mnem, code):
    addr = _get_value(params)
    if addr > 0xFF:
        print "Error, address too large: %03x: %s $%03x" % (pc, mnem, addr)
        exit() 
    code |= addr
    logger.info("PC: %03x: %04x | %s $%03x" % (pc, code, mnem, addr))
    _output_direct(code)
    return code
    
def _no_addr(params, mnem, code):
    logger.info("PC: %03x: %04x | %s" % (pc, code, mnem))
    _output_direct(code)
    return code
    
def unknown_mnem(params):
    print "Unknown mnemonic: '%s'" % params

def dump_bram_init():
    bram = [0]*2048
    for i in range(len(program)):
        inst = int(program[i], 16)
        bram[2*i+0] = inst & 0xFF
        bram[2*i+1] = (inst >> 8) & 0xFF
    
    for i in range(64):
        if (i*16) >= len(program):
            break
        hx = ''
        for j in range(31,-1,-1):
            hx = hx + "%02X" % bram[i*32+j]
        print "        INIT_%02X => X\"%s\"," % (i, hx)
                     
def dump_nan_file(filename):
    f = open(filename, "wb")
    for i in range(len(program)):
        inst = int(program[i], 16)
        b1 = inst & 0xFF
        b0 = (inst >> 8) & 0xFF
        f.write("%c%c" % (b0, b1))
    
    f.close()
        

mnemonics = {
    'LOAD'  : ( _addr_imm, 0x0800 ),
    'STORE' : ( _addr,     0x8000 ),
    'LOADI' : ( _addr,     0x8800 ),
    'STORI' : ( _addr,     0x9000 ),
    'OR'    : ( _addr_imm, 0x1800 ),
    'AND'   : ( _addr_imm, 0x2800 ),
    'XOR'   : ( _addr_imm, 0x3800 ),
    'ADD'   : ( _addr_imm, 0x4800 ),
    'SUB'   : ( _addr_imm, 0x5800 ),
    'CMP'   : ( _addr_imm, 0x5000 ),
    'ADDC'  : ( _addr_imm, 0x6800 ),
    'INP'   : ( _addr_io,  0x7800 ),
    'OUTP'  : ( _addr_io,  0xA000 ),
    'RET'   : ( _no_addr,  0xB800 ),
    'BEQ'   : ( _addr,     0xC000 ),
    'BNE'   : ( _addr,     0xC800 ),
    'BMI'   : ( _addr,     0xD000 ),
    'BPL'   : ( _addr,     0xD800 ),
    'BRA'   : ( _addr,     0XE000 ),
    'CALL'  : ( _addr,     0xE800 ),
    'BCS'   : ( _addr,     0xF000 ),
    'BCC'   : ( _addr,     0xF800 ),
    '.dw'   : ( _data,     0x0000 ),
    '.blk'  : ( _block,    0x0000 )
    }

def parse_lines(lines):
    global nr
    nr = 0
    for line in lines:
        nr = nr + 1
        line = line.rstrip()
        comm = line.split(';', 1)
        line = comm[0]
        if(line.strip() == ''):
            continue
        line_strip = line.strip()
        if line[0] != ' ':
            add_label(line.rstrip())
            if (phase == 2):
                print "            ", line
            continue
        #print "Line: '%s'" % line_strip
        line_split = line_strip.split(" ", 1)
        if len(line_split) == 1:
            line_split.append("")
        mnem = line_split[0]; 
        try:
            (f, code) = mnemonics[mnem]
        except KeyError,e:
            raise NameError("Unknown Mnemonic %s in line %d" % (mnem, nr))
        code = f(line_split[1].strip(), mnem, code)
        if (phase == 2):
            print "%03X: %04X | " % (pc-1, code),line
    
def resolve_immediates():
    global pc
    for imm in imm_expr:
        imm_dict[_get_value(imm)] = 0;
    for imm in imm_dict:
        imm_dict[imm] = pc
        imm_values.append(imm)
        pc += 1
    #print imm_expr
    #print imm_dict
    #print imm_values

if __name__ == "__main__":
    inputfile = 'nano_code.nan'
    outputfile = 'nano_code.b'

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
    # print labels
    resolve_immediates()
    pc = 0
    phase = 2
    logger.info("Pass 2...")
    logger.setLevel(logging.WARN)
    parse_lines(lines)
    for imm in imm_values:
        logger.info("PC: %03x: .dw $%04x" % (pc, imm))
        _output_direct(imm)
        
    dump_bram_init()
    dump_nan_file(outputfile)
