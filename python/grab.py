import sys
import socket
import struct
from PIL import Image as pimg

multicast_group = '239.0.1.64'
server_address = ('', 11000)

numFrames = 50
if len(sys.argv) > 1:
    numFrames = int(sys.argv[1])

colors = [
    ( 0x00, 0x00, 0x00 ),
    ( 0xEF, 0xEF, 0xEF ),
    ( 0x8D, 0x2F, 0x34 ),
    ( 0x6A, 0xD4, 0xCD ),
    ( 0x98, 0x35, 0xA4 ),
    ( 0x4C, 0xB4, 0x42 ),
    ( 0x2C, 0x29, 0xB1 ),
    ( 0xEF, 0xEF, 0x5D ),
    ( 0x98, 0x4E, 0x20 ),
    ( 0x5B, 0x38, 0x00 ),
    ( 0xD1, 0x67, 0x6D ),
    ( 0x4A, 0x4A, 0x4A ),
    ( 0x7B, 0x7B, 0x7B ),
    ( 0x9F, 0xEF, 0x93 ),
    ( 0x6D, 0x6A, 0xEF ),
    ( 0xB2, 0xB2, 0xB2 ),
]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# Set a timeout so the socket does not block indefinitely when trying
# to receive data.
sock.settimeout(0.2)

# Bind to the server address
sock.bind(server_address)

# Tell the operating system to add the socket to the multicast group
# on all interfaces.
group = socket.inet_aton(multicast_group)
mreq = struct.pack('4sL', group, socket.INADDR_ANY)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

# Receive/respond loop
while True:
    data, address = sock.recvfrom(1024)
    #print ('received %s bytes from %s' % (len(data), address))
    seq, frame, lin, width = struct.unpack("<HHHH", data[0:8])
    if lin & 0x8000:
        break


frames = []
for frame in range(numFrames):
    raw = b""
    while True:
        data, address = sock.recvfrom(1024)
        #print ('received %s bytes from %s' % (len(data), address))
        seq, frame, lin, width = struct.unpack("<HHHH", data[0:8])
        raw += data[12:]
        if lin & 0x8000:
            break
    frames.append(raw)

print ("{0:d} frames grabbed. Now converting...".format(len(frames)))

for fr, raw in enumerate(frames):
    lines = len(raw) // 192
    newImg1 = pimg.new('RGB', (384, lines))
    i = 0
    for y in range(lines):
        for x in range(192):
            b = raw[i]
            newImg1.putpixel((2*x,y), colors[b & 0xF])
            newImg1.putpixel((2*x+1,y), colors[b >> 4])
            i += 1
    
    newImg1.save("grab{0:02d}.png".format(fr))

