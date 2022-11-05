import os
import socket
import struct
import logging

# create logger
logger = logging.getLogger('JTAG')
logger.setLevel(logging.DEBUG)
ch = logging.StreamHandler()
ch.setFormatter(logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s'))
logger.addHandler(ch)

DAEMON_ID = 0xCC41
ECP_LOADFPGA = 0xCC21
ECP_PROGFLASH = 0xCC22
ECP_READ_ID = 0xCC23
ECP_UNIQUE_ID = 0xCC24
ECP_CLEARFPGA = 0xCC25

USER_READ_ID = 0xCC01
USER_READ_MEMORY = 0xCC02
USER_WRITE_MEMORY = 0xCC03
USER_READ_IO_REGISTERS = 0xCC04
USER_WRITE_IO_REGISTERS = 0xCC05
USER_SET_IO = 0xCC06
USER_READ_CONSOLE = 0xCC07
USER_UPLOAD = 0xCC08
USER_RUN_APPL = 0xCC09
USER_READ_DEBUG = 0xCC0A

CODE_OKAY = 0x00
CODE_BAD_SYNC = 0xEE
CODE_UNKNOWN_COMMAND = 0xED
CODE_BAD_PARAMS = 0xEC
CODE_FILE_NOT_FOUND = 0xEB
CODE_VERIFY_ERROR = 0xEA
CODE_FIFO_ERROR = 0xE9
CODE_PROGRESS = 0xBF

class JtagClientException(Exception):
    pass

class JtagClient:
    def __init__(self, host, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.port = port
        self.flash_callback = None
        for i in range(20):
            try:
                logger.info(f"Trying to connect to port {self.port}")
                self.sock.connect((host, self.port))
                break
            except ConnectionRefusedError:
                self.port += 1

    def errorstring(self, b):
        errors = { 0x00: 'OKAY',
                   0xEE: 'Communication with daemon out of sync',
                   0xED: 'Unknown daemon command',
                   0xEC: 'Bad command parameters',
                   0xEB: 'File Not Found',
                   0xEA: 'Verify Error',
                   0xE9: 'JTAG FIFO Error' }
        if b in errors:
            return errors[b]
        return 'Unknown Error {0:02x}'.format(b)

    def check_daemon(self):
        try:
            self.sock.sendall(struct.pack(">H", DAEMON_ID))
        except BrokenPipeError as e:
            raise JtagClientException(str(e))

        ret = self.sock.recv(4, socket.MSG_WAITALL) # Expect 4 bytes back
        if ret[0] != CODE_OKAY or ret[1] != (DAEMON_ID & 0xFF):
            raise JtagClientException("Daemon fault! " + str(ret.hex()) + self.errorstring(ret[0]))

    def ecp_read_id(self):
        self.sock.sendall(struct.pack(">H", ECP_READ_ID))
        ret = self.sock.recv(8, socket.MSG_WAITALL) # Expect 8 bytes back
        if ret[0] == 0:
            (_, id) = struct.unpack("<LL", ret)
            return id
        raise JtagClientException("Failed to read FPGA ID: " + self.errorstring(ret[0]))

    def ecp_read_unique_id(self):
        self.sock.sendall(struct.pack(">H", ECP_UNIQUE_ID))
        ret = self.sock.recv(12, socket.MSG_WAITALL) # Expect 12 bytes back
        if ret[0] == 0:
            (_, code) = struct.unpack("<LQ", ret)
            lot = (code >> 24) & 0xFFFFFFFF
            wafer = (code >> 19) & 31
            x = (code >> 12) & 127
            y = (code >> 5) & 127
            e = (code & 31)
            logger.info(f"  Whole code: {code:016x}")
            logger.info(f"  Wafer Lot#: {lot:08x}")
            logger.info(f"  Wafer #: {wafer}")
            logger.info(f"  Wafer X/Y/E: {x}, {y}, {e}")

            return (code, lot, wafer, x, y, e)
        raise JtagClientException("Failed to read FPGA Unique Identity: " + self.errorstring(ret[0]))

    def ecp_clear_fpga(self):
        self.sock.sendall(struct.pack(">H", ECP_CLEARFPGA))
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        if ret[0] != 0:
            raise JtagClientException("Failed to clear FPGA: " + self.errorstring(ret[0]))

    def ecp_load_fpga(self, name):
        self.sock.sendall(struct.pack(">HLB", ECP_LOADFPGA, 0, len(name)))
        self.sock.sendall(name.encode())
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        if ret[0] != 0:
            raise JtagClientException("Failed to configure FPGA: " + self.errorstring(ret[0]))

    def ecp_prog_flash(self, name, addr):
        file_size = os.stat(name)
        logger.info(f"Size of file: {file_size.st_size} bytes")
        pages = (file_size.st_size + 1023) // 1024 #Callback for every 1 KB
        self.sock.sendall(struct.pack(">H", ECP_PROGFLASH))
        self.sock.sendall(struct.pack("<LB", addr, len(name)))
        self.sock.sendall(name.encode())
        prog = 0
        while(True):
            ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
            if ret[0] == CODE_PROGRESS:
                prog += 100
                if self.flash_callback:
                    self.flash_callback(prog / pages)
                else:
                    print(f"{(prog / pages):.1f}%", end='\r', flush=True)
            else:
                break

        if ret[0] != 0:
            raise JtagClientException("Failed to program Flash: " + self.errorstring(ret[0]))

    def user_read_id(self):
        self.sock.sendall(struct.pack(">H", USER_READ_ID))
        ret = self.sock.recv(8, socket.MSG_WAITALL) # Expect 8 bytes back
        if ret[0] == 0:
            (_, id) = struct.unpack("<LL", ret)
            return id
        raise JtagClientException("Failed to read User JTAG ID: " + self.errorstring(ret[0]))

    def user_read_debug(self):
        self.sock.sendall(struct.pack(">H", USER_READ_DEBUG))
        ret = self.sock.recv(8, socket.MSG_WAITALL) # Expect 8 bytes back
        if ret[0] == 0:
            (_, dbg) = struct.unpack("<LL", ret)
            logger.debug("Debug: {0:08x}".format(dbg))
            return dbg
        raise JtagClientException("Failed to read User Debug Register: " + self.errorstring(ret[0]))

    def user_read_console(self, do_print = False):
        self.sock.sendall(struct.pack(">H", USER_READ_CONSOLE))
        ret = self.sock.recv(4, socket.MSG_WAITALL) # Expect 4 bytes back with length info
        (err, _, len) = struct.unpack("<BBH", ret)
        ret = self.sock.recv(len, socket.MSG_WAITALL)
        if err != 0:
            raise JtagClientException("Failed to read Console Text: " + self.errorstring(ret[0]))

        if do_print:
            print(ret.decode(encoding='cp1252'))
        return ret.decode(encoding='cp1252')

    def user_upload(self, name, addr):
        self.sock.sendall(struct.pack(">H", USER_UPLOAD))
        self.sock.sendall(struct.pack("<LB", addr, len(name)))
        self.sock.sendall(name.encode())
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        if ret[0] != 0:
            raise JtagClientException("User file upload failed: " + self.errorstring(ret[0]))

    def user_run_app(self, addr):
        self.sock.sendall(struct.pack(">H", USER_RUN_APPL))
        self.sock.sendall(struct.pack("<L", addr))
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        if ret[0] != 0:
            raise JtagClientException("User Run Application failed: " + self.errorstring(ret[0]))

    def user_set_io(self, value):
        self.sock.sendall(struct.pack(">H", USER_SET_IO))
        self.sock.sendall(struct.pack("<L", value))
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        if ret[0] != 0:
            raise JtagClientException("User Set JTAG I/O failed: " + self.errorstring(ret[0]))

    def user_write_int32(self, addr, value):
        self.sock.sendall(struct.pack(">H", USER_WRITE_MEMORY))
        self.sock.sendall(struct.pack("<LLi", addr, 4, value))
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        if ret[0] != 0:
            raise JtagClientException("JTAG Write memory failed: " + self.errorstring(ret[0]))

    def user_read_int32(self, addr):
        self.sock.sendall(struct.pack(">H", USER_READ_MEMORY))
        self.sock.sendall(struct.pack("<LL", addr, 1))
        ret = self.sock.recv(6, socket.MSG_WAITALL) # Expect 6 bytes back
        (val,) = struct.unpack("<i", ret[2:])
        if ret[0] != 0:
            raise JtagClientException("JTAG Read memory failed: " + self.errorstring(ret[0]))
        return val

    def user_write_memory(self, addr, bytes):
        self.sock.sendall(struct.pack(">H", USER_WRITE_MEMORY))
        self.sock.sendall(struct.pack("<LL", addr, len(bytes)))
        self.sock.sendall(bytes)
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        if ret[0] != 0:
            raise JtagClientException("JTAG Write memory failed: " + self.errorstring(ret[0]))

    def user_read_memory(self, addr, len):
        words = len >> 2
        len = words << 2
        self.sock.sendall(struct.pack(">H", USER_READ_MEMORY))
        self.sock.sendall(struct.pack("<LL", addr, words))
        ret = self.sock.recv(2+len, socket.MSG_WAITALL)
        if ret[0] != 0:
            raise JtagClientException("JTAG Read memory failed: " + self.errorstring(ret[0]))
        return ret[2:]

    def user_write_io(self, addr, bytes):
        self.sock.sendall(struct.pack(">H", USER_WRITE_IO_REGISTERS))
        self.sock.sendall(struct.pack("<LL", addr, len(bytes)))
        self.sock.sendall(bytes)
        ret = self.sock.recv(2, socket.MSG_WAITALL) # Expect 2 bytes back
        if ret[0] != 0:
            raise JtagClientException("JTAG Write I/O register failed: " + self.errorstring(ret[0]))

    def user_read_io(self, addr, len):
        self.sock.sendall(struct.pack(">H", USER_READ_IO_REGISTERS))
        self.sock.sendall(struct.pack("<LL", addr, len))
        ret = self.sock.recv(2+len, socket.MSG_WAITALL)
        if ret[0] != 0:
            raise JtagClientException("JTAG Read I/O register failed: " + self.errorstring(ret[0]))
        return ret[2:]

if __name__ == '__main__':
    j = JtagClient('localhost', 4999)
    j.check_daemon()