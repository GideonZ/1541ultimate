import sys
import socket
from struct import *
from sock import mysocket

to = ""
if len(sys.argv) > 2:
    to = sys.argv[2]

s = mysocket()
leng = len(to) + 2
s.connect(sys.argv[1], 64)
s.mysend(pack("<H", 0xFF21))
#s.mysend(pack("<HH", leng, 1))
#s.mysend(to)
s.mysend(pack("<H", 0))
s.sock.shutdown(0)
s.sock.close()

