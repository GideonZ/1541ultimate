import sys
from glob import glob

import os, hashlib
def get_hash(path="", hash_type='md5'):
    func = getattr(hashlib, hash_type)()
    f = os.open(path, os.O_RDONLY)
    for block in iter(lambda: os.read(f, 2048*func.block_size), b''):
        func.update(block)
    os.close(f)
    return func.hexdigest()

# Find dependencies
dirs = [ 'wifi/raw_c3/main', 'wifi/raw_c3', 'wifi/raw_c64/main', 'wifi/raw_c64', 'u64ctrl', 'u64ctrl/main' ]
ptrns = [ '*.c', '*.h', '*.mk', '*.txt', 'sdkconfig']
fns = [ ]
for d in dirs:
    for ptrn in ptrns:
        fns += glob(f"{d}/{ptrn}")

# Print the extracted filenames
fns.sort()
for fn in fns:
    print(f"{fn}: {get_hash(fn)}")
