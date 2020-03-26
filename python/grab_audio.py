import sys
import socket
import struct

multicast_group = '239.0.1.64'
server_address = ('', 11001)

seconds = 10
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

sock.sendto(b"Hey, open the port!", ("192.168.0.121", 11001))


previous = None
total_length = 0

with open("audio.wav", "wb") as outfile:
    for frame in range(250 * seconds):
        data, address = sock.recvfrom(1024)
        (number, ) = struct.unpack("<H", data[0:2])
        if previous and (previous + 1) != number:
            print ('X', end = '')
        else:
            print ('.', end = '')

        sys.stdout.flush()
        outfile.write(data[4:])
        total_length += len(data) - 4
        previous = number
    
    header  = struct.pack(">L", 0x52494646) # "RIFF"
    header += struct.pack("<L", total_length - 8) # Remaining Chunk Size
    header += struct.pack(">L", 0x57415645) # "WAVE"
    
    header += struct.pack(">L", 0x666d7420) # "fmt "
    header += struct.pack("<LHHLLHH", 16, 1, 2, 48000, 48000 * 4, 4, 16)  # LPCM, 16 bits 48 kHz stereo
    
    header += struct.pack(">L", 0x64617461) # "data"
    header += struct.pack("<L", total_length - 44) # Remaining Chunk Size
    
    outfile.seek(0)
    outfile.write(header)

print ("Done.")
