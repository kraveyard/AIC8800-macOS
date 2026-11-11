#!/usr/bin/env python3
"""Replay the Linux driver's captured init + scan command frames and print the scan list.

Usage: sudo python3 -I aic_replay_scan.py captures/bulk.tsv
Proves the exact command sequence the kext must send (stage 2 checkpoint).
Needs interface 2 free (aic8800_fdrv not bound).
"""
import struct
import sys
import time

import usb.core
import usb.util

VID, PID, IFACE = 0x368B, 0x8D81, 2
EP_CMD_OUT, EP_CMD_IN = 0x02, 0x82
SCANU_START_REQ, SCANU_START_CFM, SCANU_RESULT_IND = 0x1000, 0x1001, 0x1004


def captured_tx(tsv):
    """Command frames sent to EP 0x02, up to and including the first SCANU_START_REQ."""
    out = []
    for line in open(tsv):
        _fr, ep, _urb, hx = line.rstrip("\n").split("\t")
        if ep != "0x02":
            continue
        b = bytes.fromhex(hx.replace(":", ""))
        out.append(b)
        if struct.unpack_from("<H", b, 8)[0] == SCANU_START_REQ:
            break
    return out


def frames(dev, timeout_ms):
    try:
        buf = bytes(dev.read(EP_CMD_IN, 4096, timeout=timeout_ms))
    except usb.core.USBTimeoutError:
        return []
    res, off = [], 0
    while off + 4 <= len(buf):  # several messages may share one transfer
        plen = buf[off] | ((buf[off + 1] & 0x0F) << 8)
        ftype = buf[off + 2]
        body = buf[off + 4: off + 4 + plen]
        if (ftype & 0x7F) == 0x11 and len(body) >= 12:
            mid, _d, _s, n, _pat = struct.unpack_from("<HHHHI", body)
            res.append((mid, body[12:12 + n]))
        off += (plen + 4 + 3) & ~3
        if plen == 0:
            break
    return res


def ssid_of(frame):
    ies = frame[36:]  # 24-byte mgmt header + 12 fixed beacon/probe-resp fields
    i = 0
    while i + 2 <= len(ies):
        eid, ln = ies[i], ies[i + 1]
        if eid == 0:
            return ies[i + 2:i + 2 + ln].decode(errors="replace") or "<hidden>"
        i += 2 + ln
    return "<no ssid ie>"


def main():
    seq = captured_tx(sys.argv[1])
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("dongle not found")
    if dev.is_kernel_driver_active(IFACE):
        sys.exit("interface 2 is bound to a kernel driver; unload aic8800_fdrv first")
    usb.util.claim_interface(dev, IFACE)
    bss = {}
    try:
        for f in seq:
            mid = struct.unpack_from("<H", f, 8)[0]
            dev.write(EP_CMD_OUT, f, timeout=1000)
            deadline = time.time() + (12 if mid == SCANU_START_REQ else 2)
            got = False
            while time.time() < deadline and not got:
                for rid, p in frames(dev, 300):
                    if rid == SCANU_RESULT_IND and len(p) >= 12:
                        flen, _fc, freq, _band, _sta, _inst, rssi = struct.unpack_from("<HHHBBBb", p)
                        fr = p[12:12 + flen]
                        bssid = fr[16:22].hex(":")
                        bss[bssid] = (ssid_of(fr), freq, rssi)
                    elif mid == SCANU_START_REQ and rid == SCANU_START_CFM:
                        got = True
                    elif mid != SCANU_START_REQ and rid == mid + 1:
                        got = True
            print(f"0x{mid:04x} -> {'ok' if got else 'NO CFM'}")
            if not got:
                break
    finally:
        usb.util.release_interface(dev, IFACE)
    print(f"\n{len(bss)} BSS found:")
    for bssid, (ssid, freq, rssi) in sorted(bss.items(), key=lambda kv: -kv[1][2]):
        print(f"  {rssi:4d} dBm  {freq:5d} MHz  {bssid}  {ssid}")


if __name__ == "__main__":
    main()
