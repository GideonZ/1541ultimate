from pyftdi.jtag import *
from pyftdi.ftdi import *
import time
import logging
import struct

# create logger
logger = logging.getLogger('JTAG')
logger.setLevel(logging.DEBUG)
ch = logging.StreamHandler()
ch.setFormatter(logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s'))

XILINX_USER1    = 0x02
XILINX_USER2    = 0x03
XILINX_USER3    = 0x22
XILINX_USER4    = 0x23
XILINX_IDCODE   = 0x09
XILINX_USERCODE = 0x08
XILINX_PROGRAM  = 0x0B
XILINX_START    = 0x0C
XILINX_SHUTDOWN = 0x0D
XILINX_EXTEST   = 0x26
XILINX_CFG_IN   = 0x05
XILINX_CFG_OUT  = 0x04

class JtagClientException(Exception):
    pass

class JtagClient:
    def __init__(self, url = 'ftdi://ftdi:232h/0'):
        self.url = url
        self.jtag = JtagEngine(trst=False, frequency=3e6)
        self.tool = JtagTool(self.jtag)
        self.jtag.configure(url)
        self.jtag.reset()
        self._reverse = None

    def jtag_clocks(self, clocks):
        cmd = bytearray(3)
        cnt = (clocks // 8) - 1
        if cnt:
            cmd[0] = 0x8F
            cmd[1] = cnt & 0xFF
            cmd[2] = cnt >> 8
            self.jtag._ctrl._stack_cmd(cmd)
        cnt = clocks % 8
        if cnt:
            cmd[0] = 0x8E
            cmd[1] = cnt
            self.jtag._ctrl._stack_cmd(cmd[0:2])
        
    def xilinx_read_id(self):
        self.jtag.reset()
        idcode = self.jtag.read_dr(32)
        self.jtag.go_idle()
        logger.info(f"IDCODE (reset): {int(idcode):08x}")
        return int(idcode)

    def xilinx_load_fpga(self, filename):
	    # Reset
        logger.info("reset..")
        self.jtag.reset()

        self.jtag.write_ir(BitSequence(XILINX_PROGRAM, False, 6))
        self.jtag.reset()
        self.jtag.go_idle()
        self.jtag_clocks(10000)

        # Program
        logger.info("programming..");
        self.jtag.write_ir(BitSequence(XILINX_CFG_IN, False, 6))
        with open(filename, "rb") as f:
            self.jtag.change_state('shift_dr')
            while(True):
                buffer = f.read(16384)
                if len(buffer) <= 0:
                    break

                olen = len(buffer)-1
                cmd = bytearray((Ftdi.WRITE_BYTES_NVE_MSB, olen & 0xff,
                          (olen >> 8) & 0xff))
                cmd.extend(buffer)
                self.jtag._ctrl._stack_cmd(cmd)

        self.jtag.change_state('update_dr')
        self.jtag.go_idle()
        self.jtag.write_ir(BitSequence(XILINX_START, False, 6))
        self.jtag_clocks(32)

    def set_user_ir(self, ir):
        self.jtag.write_ir(BitSequence(XILINX_USER4, False, 6))
        
        self.jtag.write_dr(BitSequence(ir << 1 | 1, False, 5))

        self.jtag.write_ir(BitSequence(XILINX_USER4, False, 6))
        self.jtag.change_state('shift_dr')
        # Writing the first zero selects the data registers (a '1' selects the IR register)
        self.jtag.shift_register(BitSequence(0, length = 1))
        #logger.info(ir, rb)

    def read_user_data(self, bits) -> BitSequence:
        inp = BitSequence(0, length = bits)
        return self.jtag.shift_register(inp)

    def user_read_id(self):
        self.set_user_ir(0)
        user_id = int(self.read_user_data(32))
        self.jtag.go_idle()
        logger.info(f"UserID: {user_id:08x}")
        return user_id

    def user_set_outputs(self, value):
        self.set_user_ir(2)
        self.jtag.shift_and_update_register(BitSequence(value, False, 8))
        self.jtag.go_idle()

    def user_upload(self, name, addr):
        bytes_read = 0
        with open(name, "rb") as fi:
            logger.info(f"Uploading {name} to address {addr:08x}")
            while(True):
                buffer = fi.read(16384)
                if len(buffer) <= 0:
                    break
                bytes_read += len(buffer)
                self.user_write_memory(addr, buffer + b'\x00\x00\x00\x00\x00\x00\x00\x00')
                addr += 16384
            logger.info(f"Uploaded {bytes_read:06x} bytes.")

        if bytes_read == 0:
            logger.error(f"Reading file {name} failed -> Can't upload to board.")
            raise JtagClientException("Failed to upload applictation")

        return bytes_read

    def user_write_memory(self, addr, buffer):
        addrbytes = struct.pack("<L", addr)
        command = bytearray([ addrbytes[0], 4, addrbytes[1], 5, addrbytes[2], 6, addrbytes[3], 7, 0x80, 0x01])
        self.set_user_ir(5)
        self.jtag.shift_and_update_register(BitSequence(bytes_ = command))
        self.set_user_ir(6)
        olen = len(buffer)-1
        cmd = bytearray((Ftdi.WRITE_BYTES_NVE_LSB, olen & 0xff,
                    (olen >> 8) & 0xff))
        cmd.extend(buffer)
        self.jtag._ctrl._stack_cmd(cmd)
        self.jtag.go_idle()
    
def prog_fpga(logger, j, name = "./u64_mk2_artix.bit"):
    start_time = time.perf_counter()
    j.xilinx_load_fpga(name)
    end_time = time.perf_counter()
    execution_time = end_time - start_time
    logger.info(f"Execution time: {execution_time} seconds")
    time.sleep(1)


if __name__ == '__main__':
    logger.addHandler(ch)
    j = JtagClient()
    j.xilinx_read_id()
    prog_fpga(logger, j)
    j.user_set_outputs(0x80) # Reset 
    j.user_upload('ultimate.bin', 0x30000)
    magic = struct.pack("<LL", 0x30000, 0x1571babe)
    j.user_write_memory(0xFFF8, magic)
    j.user_set_outputs(0x00) # Unreset to bootloader
    j.user_read_id()
