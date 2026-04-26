import sys
import os, hashlib
def get_hash(path="", hash_type='md5'):
    func = getattr(hashlib, hash_type)()
    f = os.open(path, os.O_RDONLY)
    for block in iter(lambda: os.read(f, 2048*func.block_size), b''):
        func.update(block)
    os.close(f)
    return func.hexdigest()

# Load file
fns = []
filetypes = ["vhdl"]

for n in sys.argv[1:]:
    with open(n, 'r') as file:
        for line in file:
            line = line.replace('\\', '').strip()
            line = line.replace('$(ULTIMATE_FPGA)', '../../../fpga')
            tokens = line.split(',')
            if tokens[0] in filetypes:
                fn = tokens[-1].strip("\"")
                if fn not in fns:
                    fns.append(fn)

# Print the extracted filenames
fns.sort()
for fn in fns:
    print(f"{fn}: {get_hash(fn)}")
