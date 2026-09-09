#!/usr/bin/env python3
"""Independent SHA256 oracle, including the former AVB 32-bit bit-count boundary."""
import base64
import hashlib
import subprocess
pattern = bytes(range(251))
for offset, length in [(0,0),(1,1),(3,55),(4095,56),(7,63),(0,64),(17,65),
                       (8191,131073),(61,17000123),(31,536870929)]:
    expected = hashlib.sha256()
    at = offset
    left = length
    while left:
        take = min(left, 1024*1024)
        block = (pattern * ((take+501)//251))[at%251:at%251+take]
        expected.update(block)
        at += take
        left -= take
    result = subprocess.check_output(['./test_hash',str(offset),str(length)],text=True).strip()
    assert base64.urlsafe_b64decode(result+'=') == expected.digest(), (offset,length,result)
print('hash: independent SHA256, unaligned reads, 512MiB boundary and failure cleanup passed')
