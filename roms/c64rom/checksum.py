import sys

def calc_checksum(data, old):
	a = 0
	c = 0
	for b in data:
		a = a + b + c
		c = 0
		if a > 255:
			a = a - 256
			c = 1
	if not old:
		a += c
	return a

def usage():
	print "Usage: python checksum.py {--old|--new} <file.bin> [<checksum> <offset>]"
	exit(1)

# main

if len(sys.argv) != 3 and len(sys.argv) != 5:
	usage()

if sys.argv[1] == "--old":
	old = True
elif sys.argv[1] == "--new":
	old = False
else:
	usage()

filename = sys.argv[2]

data = bytearray(open(filename, 'rb').read())

checksum = calc_checksum(data, old)

if len(sys.argv) != 5:
	print filename + ": " + hex(checksum)
	exit(0)

desired_checksum = int(sys.argv[3], 16)
offset = int(sys.argv[4], 16)

if checksum < desired_checksum:
	data[offset] = desired_checksum - checksum
else:
	data[offset] = 0xff - (checksum - desired_checksum)

file = open(filename, "wb")
file.write(data)
