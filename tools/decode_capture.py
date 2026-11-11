#!/usr/bin/env python3
"""Decode tshark bulk.tsv (frame, ep, urb_type, hex) into AIC lmac messages."""
import struct, sys
for line in open(sys.argv[1]):
    fr, ep, urb, hx = line.rstrip("\n").split("\t")
    b = bytes.fromhex(hx.replace(":", ""))
    if ep == "0x02":
        mid, dst, src, plen = struct.unpack_from("<HHHH", b, 8)
        print(f"{fr:>5} TX  id=0x{mid:04x} dst={dst} src={src} plen={plen:<4} {b[16:16+min(plen,24)].hex()}")
    elif ep in ("0x82", "0x81") and len(b) >= 16 and (b[2] & 0x7f) == 0x11:
        mid, dst, src, plen = struct.unpack_from("<HHHH", b, 4)
        print(f"{fr:>5} RX{ep[-2:]} id=0x{mid:04x} plen={plen:<4} {b[16:16+min(plen,24)].hex()}")
