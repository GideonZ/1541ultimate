import sys
import socket
import struct
import time

test_file = '../../../target/fpga/ecp5_tester/impl1/ecp5_tester_impl1.bit'
dut_fpga  = '../../../target/fpga/ecp5_dut/impl1/u2p_ecp5_dut_impl1.bit'
dut_appl  = '../../../target/software/riscv32_dut/result/dut.bin'
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
TIME            = 0x00B0 # b0-bf

class JtagClient:
    def __init__(self, host, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))

    def check_daemon(self):
        self.sock.sendall(struct.pack(">H", DAEMON_ID))
        ret = self.sock.recv(4, socket.MSG_WAITALL) # Expect 4 bytes back
        print(ret)

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
        print(f"Expecting {len} bytes!")
        ret = self.sock.recv(len, socket.MSG_WAITALL)
        #print(ret.decode())
        print(ret)

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

    def user_write_uint32(self, addr, value):
        self.sock.sendall(struct.pack(">H", USER_WRITE_MEMORY))
        self.sock.sendall(struct.pack("<LLL", addr, 4, value))
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        print(ret)

if __name__ == '__main__':
    client = JtagClient("localhost", 5000)
    client.check_daemon()
    client.ecp_load_fpga(test_file)
    print("{0:8x}".format(client.user_read_id()))
    client.user_set_io(0x10)
    time.sleep(1)
    #client.user_read_console()

    #client.user_upload(hello_world, 0x100)
    #client.user_run_app(0x100)
    #time.sleep(1)
    #client.user_read_console()
    #client.user_read_console()

    #time.sleep(1)

    dut = JtagClient("localhost", 5002)
    dut.check_daemon()
    print("{0:08x}".format(dut.ecp_read_id()))
    dut.ecp_load_fpga(dut_fpga)
    time.sleep(0.2)
    dut.user_read_console()
    #dut.ecp_prog_flash(dut_fpga, 0)
    print("{0:8x}".format(client.user_read_id()))
    dut.user_upload(dut_appl, 0x100)
    dut.user_run_app(0x100)
    for i in range(5):
        time.sleep(.2)
        dut.user_read_console()
    dut.user_write_uint32(TESTER_TO_DUT, 99);
    time.sleep(.2)
    dut.user_read_console()


#    time.sleep(1)
#    dut.user_read_console()
#    dut.user_set_io(0x80)
#    dut.user_set_io(0x00)
#    time.sleep(1)
#    dut.user_read_console()
