import time
import socket
from struct import *

class mysocket:
    def __init__(self):
        self.sock = socket.socket(
            socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)

    def bind(self, port):
        self.sock.bind(('', port))

    def broadcast(self, msg, port):
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.sock.sendto(msg, ("255.255.255.255", port))

    def receive(self):
        return self.sock.recvfrom(256)

if __name__ == "__main__":
    s = mysocket()
    s.bind(10000)
    s.broadcast(b"Any Ultimates around?!", 64)
    time.sleep(0.2) 
    # Better to use a timeout on the socket than just waiting, but I was lazy
    # With a timeout, you can find more than just one ultimate.
    print(s.receive())
