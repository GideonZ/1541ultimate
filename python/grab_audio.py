import sys
import socket
import struct

multicast_group = '239.0.1.64'
server_address = ('', 11001)

seconds = 30
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
sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

sock.sendto(b"Hey, open the port!", ("192.168.0.88", 11001))


with open("audio.raw", "wb") as outfile:
    for frame in range(250 * seconds):
        data, address = sock.recvfrom(1024)
        print ('.', end = '')
        sys.stdout.flush()
        outfile.write(data[2:])

print ("Done.")
