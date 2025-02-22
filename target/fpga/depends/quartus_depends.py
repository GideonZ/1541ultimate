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
filetypes = ["VHDL_FILE", "VERILOG_FILE", "SDC_FILE", "QIP_FILE"]

with open(sys.argv[1], 'r') as file:
    for line in file:
        tokens = line.split()
        if len(tokens) > 3 and tokens[0] == "set_global_assignment" and tokens[2] in filetypes:
            fns.append(tokens[-1].strip("\""))

# Print the extracted filenames
fns.sort()
for fn in fns:
    print(f"{fn}: {get_hash(fn)}")
