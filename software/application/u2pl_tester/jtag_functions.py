import sys
import socket
import struct
import time
import math
from datetime import datetime

test_file = '../../../target/fpga/ecp5_tester/impl1/ecp5_tester_impl1.bit'
dut_fpga  = '../../../target/fpga/ecp5_dut/impl1/u2p_ecp5_dut_impl1.bit'
dut_appl  = '../../../target/software/riscv32_dut/result/dut.bin'
sound     = '../../../software/application/u2pl_tester/untitled.raw'
hello_world = 'hello_world.bin'

DAEMON_ID = 0xCC41
ECP_READ_ID = 0xCC23
ECP_LOADFPGA = 0xCC21
ECP_PROGFLASH = 0xCC22

USER_READ_ID = 0xCC01
USER_READ_MEMORY = 0xCC02
USER_WRITE_MEMORY = 0xCC03
USER_READ_IO_REGISTERS = 0xCC04
USER_WRITE_IO_REGISTERS = 0xCC05
USER_SET_IO = 0xCC06
USER_READ_CONSOLE = 0xCC07
USER_UPLOAD = 0xCC08
USER_RUN_APPL = 0xCC09

CODE_OKAY = 0x00
CODE_BAD_SYNC = 0xEE
CODE_UNKNOWN_COMMAND = 0xED
CODE_BAD_PARAMS = 0xEC
CODE_FILE_NOT_FOUND = 0xEB
CODE_VERIFY_ERROR = 0xEA

DUT_TO_TESTER   = 0x0094
TESTER_TO_DUT   = 0x0098
TEST_STATUS     = 0x009C
RTC_DATA        = 0x00B0 # b0-bf

class Rtc:
    @staticmethod
    def bin2bcd(val):
        if val < 0:
            return 0
        if val > 99:
            return 99
        return ((val // 10) * 16) + val % 10

    @staticmethod
    def from_current_time():
        now = datetime.now()
        rtc = bytearray(12)
        rtc[0] = 0x21
        rtc[1] = 0x07
        rtc[4] = Rtc.bin2bcd(now.second)
        rtc[5] = Rtc.bin2bcd(now.minute)
        rtc[6] = Rtc.bin2bcd(now.hour)
        rtc[7] = Rtc.bin2bcd(now.day)
        rtc[8] = (now.weekday() + 1) % 7
        rtc[9] = Rtc.bin2bcd(now.month)
        rtc[10] = Rtc.bin2bcd(now.year - 1980)
        return rtc
 
class JtagClient:
    def __init__(self, host, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))

    # TODO: Add proper error handling for these functions
    def check_daemon(self):
        self.sock.sendall(struct.pack(">H", DAEMON_ID))
        ret = self.sock.recv(4, socket.MSG_WAITALL) # Expect 4 bytes back
        if ret[0] != CODE_OKAY or ret[1] != (DAEMON_ID & 0xFF):
            print(ret.hex())
            return "Daemon fault."
        # Return None if everything is OK

    def ecp_read_id(self):
        self.sock.sendall(struct.pack(">H", ECP_READ_ID))
        ret = self.sock.recv(8, socket.MSG_WAITALL) # Expect 8 bytes back
        if ret[0] == 0:
            (_, id) = struct.unpack("<LL", ret)
            return id

    def ecp_load_fpga(self, name):
        self.sock.sendall(struct.pack(">HLB", ECP_LOADFPGA, 0, len(name)))
        self.sock.sendall(name.encode())
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        print(ret)

    def ecp_prog_flash(self, name, addr):
        self.sock.sendall(struct.pack(">H", ECP_PROGFLASH))
        self.sock.sendall(struct.pack("<LB", addr, len(name)))
        self.sock.sendall(name.encode())
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        print(ret)

    def user_read_id(self):
        self.sock.sendall(struct.pack(">H", USER_READ_ID))
        ret = self.sock.recv(8, socket.MSG_WAITALL) # Expect 8 bytes back
        if ret[0] == 0:
            (_, id) = struct.unpack("<LL", ret)
            return id

    def user_read_console(self):
        self.sock.sendall(struct.pack(">H", USER_READ_CONSOLE))
        ret = self.sock.recv(4, socket.MSG_WAITALL) # Expect 4 bytes back with length info
        (_, len) = struct.unpack("<HH", ret)
        #print(f"Expecting {len} bytes!")
        ret = self.sock.recv(len, socket.MSG_WAITALL)
        return ret

    def user_upload(self, name, addr):
        self.sock.sendall(struct.pack(">H", USER_UPLOAD))
        self.sock.sendall(struct.pack("<LB", addr, len(name)))
        self.sock.sendall(name.encode())
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        print(ret)

    def user_run_app(self, addr):
        self.sock.sendall(struct.pack(">H", USER_RUN_APPL))
        self.sock.sendall(struct.pack("<L", addr))
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        print(ret)

    def user_set_io(self, value):
        self.sock.sendall(struct.pack(">H", USER_SET_IO))
        self.sock.sendall(struct.pack("<L", value))
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        print(ret)

    def user_write_int32(self, addr, value):
        self.sock.sendall(struct.pack(">H", USER_WRITE_MEMORY))
        self.sock.sendall(struct.pack("<LLi", addr, 4, value))
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        #print(ret)

    def user_read_int32(self, addr):
        self.sock.sendall(struct.pack(">H", USER_READ_MEMORY))
        self.sock.sendall(struct.pack("<LL", addr, 1))
        ret = self.sock.recv(6, socket.MSG_WAITALL) # Expect 6 bytes back
        (val,) = struct.unpack("<i", ret[2:])
        #print (val)
        return val

    def user_write_memory(self, addr, bytes):
        self.sock.sendall(struct.pack(">H", USER_WRITE_MEMORY))
        self.sock.sendall(struct.pack("<LL", addr, len(bytes)))
        self.sock.sendall(bytes)
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        #print(ret)

    def user_read_memory(self, addr, len):
        words = len >> 2
        len = words << 2
        self.sock.sendall(struct.pack(">H", USER_READ_MEMORY))
        self.sock.sendall(struct.pack("<LL", addr, words))
        ret = self.sock.recv(2+len, socket.MSG_WAITALL)
        return ret[2:]

    def user_write_io(self, addr, bytes):
        self.sock.sendall(struct.pack(">H", USER_WRITE_IO_REGISTERS))
        self.sock.sendall(struct.pack("<LL", addr, len(bytes)))
        self.sock.sendall(bytes)
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        #print(ret)

    def user_read_io(self, addr, len):
        self.sock.sendall(struct.pack(">H", USER_READ_IO_REGISTERS))
        self.sock.sendall(struct.pack("<LL", addr, len))
        ret = self.sock.recv(2+len, socket.MSG_WAITALL)
        return ret[2:]

    def perform_test(self, test_id, max_time = 10):
        self.user_write_int32(TESTER_TO_DUT, test_id)
        while dut.user_read_int32(TESTER_TO_DUT) == test_id and max_time > 0:
            time.sleep(.2)
            max_time -= 1
        if dut.user_read_int32(TESTER_TO_DUT) == test_id:
            return (None, "Test did not complete in time.")
        text = self.user_read_console()
        result = dut.user_read_int32(TEST_STATUS)
        return (result, text.decode(encoding='cp1252'))

def startup():
    client = JtagClient("localhost", 5001)
    ret = client.check_daemon()
    if ret:
        return (None, None, ret)
    if client.ecp_read_id() != 0x41111043:
        return (None, None, "Tester FPGA ID fail")
    client.ecp_load_fpga(test_file)
    if client.user_read_id() != 0xdead1541:
        return (None, None, "Tester User JTAG fail!")
    client.user_set_io(0x10)  # Turn on DUT power
    time.sleep(1)

    #client.user_read_console()

    #client.user_upload(hello_world, 0x100)
    #client.user_run_app(0x100)
    #time.sleep(1)
    #client.user_read_console()
    #client.user_read_console()

    #time.sleep(1)

    dut = JtagClient("localhost", 5002)
    ret = dut.check_daemon()
    if ret:
        return (None, None, ret)

    #return (client, dut, None)

    if dut.ecp_read_id() != 0x41111043:
        return (client, None, "DUT FPGA ID fail")
    dut.ecp_load_fpga(dut_fpga)
    time.sleep(0.2)
    dut.user_read_console()
    #dut.ecp_prog_flash(dut_fpga, 0)
    if dut.user_read_id() != 0xdead1541:
        return (client, None, "DUT User JTAG fail!")
    return (client, dut, None)

if False:
    dut.user_upload(dut_appl, 0x100)
    dut.user_run_app(0x100)
    while len(dut.user_read_console()) != 0:
        time.sleep(.2)
        
    (result, console) = dut.perform_test(99)
    print(f"Result of test 99: {result}")
    print(f"Console Output:\n{console}")

    (result, console) = dut.perform_test(7)
    print(f"Result of test 7: {result}")
    print(f"Console Output:\n{console}")

    (result, console) = dut.perform_test(11)
    print(f"Result of test 11: {result}")
    print(f"Console Output:\n{console}")

    time.sleep(3)
    print(dut.user_read_console().decode(encoding='cp1252'))

    (result, console) = dut.perform_test(8)
    print(f"Result of test 8: {result}")
    print(f"Console Output:\n{console}")

    (result, console) = dut.perform_test(13) # Test RTC
    print(f"Result of test 13: {result}")
    print(f"Console Output:\n{console}")

    (result, console) = dut.perform_test(14) # Read RTC
    print(f"Result of test 14: {result}")
    print(f"Console Output:\n{console}")
    rtc = dut.user_read_memory(RTC_DATA, 16)
    print(rtc)

    rtc = Rtc.from_current_time()
    dut.user_write_memory(RTC_DATA, rtc)

    (result, console) = dut.perform_test(15) # Write RTC
    print(f"Result of test 15: {result}")
    print(f"Console Output:\n{console}")

    print("Generating sine wave")
    #scale = math.pow(2.0, 31) * 0.9
    #data = b''
    #for i in range(192):
    #    phase = i * (2. * math.pi / 48.)
    #    sampleL = scale * math.sin(phase)
    #    phase = i * (2. * math.pi / 64.)
    #    sampleR = scale * math.sin(phase)
    #    data += struct.pack("<ll", int(sampleL), int(sampleR))
    ## data should now have 192 samples. 500* 192 samples = 96000 samples = 2 seconds
    #data *= 500
    data = bytearray(48000*8*2) # zeros
    print("Uploading sound.")
    dut.user_write_memory(0x1100000, data)
#    dut.user_upload(sound, 0x1100000)

    (result, console) = dut.perform_test(2) # Do some audio stuff
    print(f"Result of test 2: {result}")
    print(f"Console Output:\n{console}")
    time.sleep(2) # Could be 1 probably as download of data takes some time

    # Download one second of data
    data = dut.user_read_memory(0x1000000, 48000 * 2 * 4)
    with open("audio.bin", 'wb') as fo:
        fo.write(data)

#    time.sleep(1)
#    dut.user_read_console()
#    dut.user_set_io(0x80)
#    dut.user_set_io(0x00)
#    time.sleep(1)
#    dut.user_read_console()
def find_ones(b):
    ret = []
    for idx,byte in enumerate(b):
        for i in range(8):
            if byte & (1 << i) != 0:
                ret.append(idx * 8 + i)
    return ret

def find_zeros(b):
    ret = []
    for idx,byte in enumerate(b):
        for i in range(8):
            if byte & (1 << i) == 0:
                ret.append(idx * 8 + i)
    return ret

def test_cart_io(tester, dut):
    # Set everything to input on dut
    # Bit 47 has to be set to 1, because that's BUFFER_EN
    zeros = bytearray(8)
    buf_en = bytearray(8)
    buf_en[5] = 0x80
    dut.user_write_io(0x100308, zeros)
    dut.user_write_io(0x100300, buf_en)
    print("Setting bits: ", find_ones(buf_en))
    rb = dut.user_read_io(0x100300, 8)
    print("Read: ", find_ones(rb))

    # Set everything to output on tester
    ones = b'\xff' * 8
    tester.user_write_io(0x100308, ones)

    slot_bits = [x for x in range(38)]
    cassette_bits = [x for x in range(40, 44)]
    test_bits = slot_bits + cassette_bits
    errors = 0

#    for i in test_bits:
#        pattern = bytearray(6)
#        pattern[i//8] |= 1 << (i %8)
#        print("Writing: ", pattern.hex())
#        tester.user_write_io(0x100300, pattern)
#        trb = tester.user_read_io(0x100300, 6)
#        #print("Tester:  ", trb.hex(), find_ones(trb))
#        if (trb != pattern):
#            print(f"BIT {i} is stuck hard!")
#        rb = bytearray(dut.user_read_io(0x100300, 6))
#        # Turn off bit 39
#        rb[4] &= 0x7F
#        print("Read:    ", rb.hex(), find_ones(rb))
#        if (rb != pattern):
#            errors += 1

    for i in range(32):
        pattern = bytearray(b'\xFF' * 4)
        pattern[i//8] &= ~(1 << (i %8))
        print("Writing: ", pattern.hex())
        tester.user_write_io(0x100300, pattern)
        trb = tester.user_read_io(0x100300, 4)
        print("Tester:  ", trb.hex(), find_zeros(trb))
        if (trb != pattern):
            print(f"BIT {i} is stuck hard!")
        rb = bytearray(dut.user_read_io(0x100300, 4))
        print("Read:    ", rb.hex(), find_zeros(rb))
        if (rb != pattern):
            errors += 1

    print(f"Errors: {errors}")

def test_iec_io(dut):
    # IEC is only available on the dut.
    # Can only toggle each output and observe the input
    # While the pins are active low, the signals in the
    # FPGA are active high.
    patterns = b'\x00\x01\x02\x04\x08\x10\x1F\x1E\x1D\x1B\x17\x0F'
    errors = 0
    for i in patterns:
        wr = bytearray(1)
        wr[0] = i
        dut.user_write_io(0x100306, wr)
        rb = dut.user_read_io(0x100306, 1)
        print("{0:02x} {1:02x}".format(i, rb[0]))
        if i != rb[0]:
            errors += 1
    # TODO: Report more accurately *which signal* is stuck at 0, stuck at 1, or shorted to another
    return errors

if __name__ == '__main__':
    (client, dut, err) = startup()
    if not err:
        print("I/O register test Tester")
        print(client.user_read_io(0x100000, 16).hex())
        print(client.user_read_io(0x100100, 16).hex())
        print(client.user_read_io(0x100200, 16).hex())
        print(client.user_read_io(0x100300, 16).hex())

        print("I/O register test DUT")
        print(dut.user_read_io(0x100000, 16).hex())
        print(dut.user_read_io(0x100100, 16).hex())
        print(dut.user_read_io(0x100200, 16).hex())
        print(dut.user_read_io(0x100300, 16).hex())

        test_iec_io(dut)
        #test_cart_io(client, dut)
    else:
        print (err)
    if client:
        client.user_set_io(0x00) # Turn off DUT power
