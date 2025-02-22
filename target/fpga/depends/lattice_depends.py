import xml.etree.ElementTree as ET
import sys

import os, hashlib
def get_hash(path="", hash_type='md5'):
    func = getattr(hashlib, hash_type)()
    f = os.open(path, os.O_RDONLY)
    for block in iter(lambda: os.read(f, 2048*func.block_size), b''):
        func.update(block)
    os.close(f)
    return func.hexdigest()

# Load the XML file
tree = ET.parse(sys.argv[1])
root = tree.getroot()

# Find all 'Source' elements and extract the 'name' attribute
fns = [source.get("name") for source in root.findall(".//Source")]
fns += [source.get("file") for source in root.findall(".//Strategy")]

# Print the extracted filenames
#fns = [filename.replace("../../../fpga", ".") for filename in fns]
fns.sort()
for fn in fns:
    print(f"{fn}: {get_hash(fn)}")
