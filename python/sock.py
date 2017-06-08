import sys
import socket
from struct import *

class mysocket:
    '''demonstration class only
      - coded for clarity, not efficiency
    '''

    def __init__(self, sock=None):
        if sock is None:
            self.sock = socket.socket(
                socket.AF_INET, socket.SOCK_STREAM)
        else:
            self.sock = sock

    def connect(self, host, port):
        self.sock.connect((host, port))

    def mysend(self, msg):
        totalsent = 0
        while totalsent < len(msg):
            sent = self.sock.send(msg[totalsent:])
            if sent == 0:
                raise RuntimeError("socket connection broken")
            totalsent = totalsent + sent
        
    def myreceive(self, maxlen):
        chunks = []
        bytes_recd = 0
        while bytes_recd < maxlen:
            chunk = self.conn.recv(min(maxlen - bytes_recd, 2048))
            bytes_recd = bytes_recd + len(chunk)
            print bytes_recd
            if chunk == '':
                print "Number of chunks received so far:", len(chunks), bytes_recd
                break            
                #raise RuntimeError("socket connection broken")
            chunks.append(chunk)
        return ''.join(chunks)

    def serve(self, port):
        self.sock.bind(('', port))
        self.sock.listen(2)
        conn, addr = self.sock.accept()
        print conn, addr
        self.conn = conn
        
if __name__ == "__main__":
    if (sys.argv[1] == 'c'):
        s = mysocket()
        
        message = (("abcdefghijklmnopqrstuvwxyz" * 5) + ("0123456789" * 10)) * 10
        #print message
        message = message * 2000
        
        if (len(sys.argv) == 3):
            s.connect(sys.argv[2], 5001)
        else:
            s.connect("localhost", 5001)
        s.mysend(message)
        s.sock.shutdown(0)
        s.sock.close()

    elif (sys.argv[1] == 't'):
        s = mysocket()
        
        if (len(sys.argv) == 3):
            s.connect(sys.argv[2], 5002)
        else:
            s.connect("localhost", 5002)
        s.conn = s.sock
        msg = s.myreceive(2*1024*1024)
        text_file = open("trace.log", "wb")
        text_file.write(msg)
        text_file.close()        
        s.sock.shutdown(0)
        s.sock.close()

    elif (sys.argv[1] == 'u'):
        with open(sys.argv[2], "rb") as f:
            bytes = f.read(2*1024*1024) # max 2 Meg
            if bytes != "":
                s = mysocket()
                s.connect(sys.argv[3], 12345)
                s.mysend('\0\1\0\0')
                s.mysend(bytes)
                s.sock.shutdown(0)
                s.sock.close()
                
    elif (sys.argv[1] == 'p'):
        #define SOCKET_CMD_DMA    0xFF01
        #define SOCKET_CMD_DMARUN 0xFF02
        #define SOCKET_CMD_KEYB   0xFF03
        #define SOCKET_CMD_RESET  0xFF04
        #define SOCKET_CMD_WAIT      0xFF05
        #define SOCKET_CMD_DMAWRITE  0xFF06
        
        with open(sys.argv[2], "rb") as f:
            bytes = f.read(65536) # max 64K 
            if bytes != "":
                s = mysocket()
                s.connect(sys.argv[3], 64)
                # reset 
                s.mysend(pack("<H", 0xFF04))
                s.mysend(pack("<H", 0))
                # wait 3 seconds
                s.mysend(pack("<H", 0xFF05))
                s.mysend(pack("<H", 600))
                # write hello
                s.mysend(pack("<H", 0xFF03))
                s.mysend(pack("<H", 5))
                s.mysend("HELLO")
                # wait 3 seconds
                s.mysend(pack("<H", 0xFF05))
                s.mysend(pack("<H", 600))
                # execute load/run command
                s.mysend(pack("<H", 0xFF02))
                s.mysend(pack("<H", len(bytes)))
                s.mysend(bytes)
                
                s.sock.shutdown(0)
                s.sock.close()

    elif (sys.argv[1] == 'U'):
        with open(sys.argv[2], "rb") as f:
            bytes = f.read(2*1024*1024) # max 2 Meg
            if bytes != "":
                s = mysocket()
                s.connect(sys.argv[3], 12345)
                s.mysend('\1\0\0\0')
                s.mysend(bytes)
                s.sock.shutdown(0)
                s.sock.close()
                
        
    else: # server
        s = mysocket()
        s.serve(5001)
        msg = s.myreceive(1024*1024)
        print "Received: ", len(msg), "bytes"
        s.sock.shutdown(0)
        s.sock.close()
        