#!/usr/bin/env python3
import pefile, sys, shutil

src, dst = sys.argv[1], sys.argv[2]
shutil.copyfile(src, dst)
pe = pefile.PE(dst)
tls = pe.OPTIONAL_HEADER.DATA_DIRECTORY[9]
print(f"before: TLS RVA=0x{tls.VirtualAddress:x} Size=0x{tls.Size:x}")
tls.VirtualAddress = 0
tls.Size = 0
pe.write(dst)
print(f"after:  TLS RVA=0 Size=0")
