#!/usr/bin/env python

#
# Simple syslog server showing incoming messages on stdout.
#

import socket
import sys
import datetime

# The default syslog port 514 requires root access, so we have 64514 as a fallback
try_ports = [514, 64514]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

bound = False
for port in try_ports:
    try:
        sock.bind(('0.0.0.0', port))
        print('Listening for incoming syslog packets on UDP port ' +  str(port))
        bound = True
        break
    except PermissionError as e:
        print('NOTE: Not allowed to bind to UDP port ' + str(port) + ' (root access needed)\n')
    except Exception as e:
        pass

if not bound:
    print('Unable to bind to port, aborting.')
    sys.exit(1)

print()
while True:
    data, address = sock.recvfrom(65536, 0)
    ts = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')
    ip = address[0]
    msg = data.decode('iso-8859-1').rstrip()
    print(ts + ' [' + ip + '] ' + msg)
