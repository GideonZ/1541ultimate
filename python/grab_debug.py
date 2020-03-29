import sys
import socket
import struct

multicast_group = '239.0.1.64'
server_address = ('', 11002)

seconds = 5
if len(sys.argv) > 1:
    seconds = int(sys.argv[1])


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# Set a timeout so the socket does not block indefinitely when trying
# to receive data.
sock.settimeout(2.0)

# Bind to the server address
sock.bind(server_address)

# Tell the operating system to add the socket to the multicast group
# on all interfaces.
group = socket.inet_aton(multicast_group)
mreq = struct.pack('4sL', group, socket.INADDR_ANY)
#sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

sock.sendto(b"Hey, open the port!", ("192.168.0.121", 11002))


previous = None
with open("debug.raw", "wb") as outfile:
    for frame in range(2737 * seconds):
        data, address = sock.recvfrom(1536)
        (number, ) = struct.unpack("<H", data[0:2])
        if previous and (previous + 1) != number:
            print ('X', end = '')
        else:
            print ('.', end = '')
        previous = number
        sys.stdout.flush()
        outfile.write(data[4:])

print ("Done.")
