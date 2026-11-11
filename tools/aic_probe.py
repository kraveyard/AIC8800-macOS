#!/usr/bin/env python3
"""Userspace probe for AIC8800D80 run mode (368b:8d81), Wi-Fi interface 2.

Validates the command framing from docs/PROTOCOL.md before it goes into the kext.
Run as root on Linux. Detaches aic8800_fdrv from interface 2 and reattaches it on exit.
"""
import struct
import sys
import time

import usb.core
import usb.util

VID, PID, IFACE = 0x368B, 0x8D81, 2
EP_CMD_OUT, EP_CMD_IN = 0x02, 0x82

TASK_MM, DRV_TASK_ID = 0, 100
MM_GET_MAC_ADDR_REQ, MM_GET_MAC_ADDR_CFM = 0x73, 0x74
MM_GET_FW_VERSION_REQ, MM_GET_FW_VERSION_CFM = 0x80, 0x81

USB_TYPE_CFG_CMD_RSP, USB_TYPE_CFG_DATA_CFM, USB_TYPE_CFG_PRINT = 0x11, 0x12, 0x13


def build_cmd(msg_id, params, dest=TASK_MM, src=DRV_TASK_ID):
    """aicwf_set_cmd_tx: 4-byte USB hdr + dummy word + lmac_msg."""
    length = 8 + len(params)  # sizeof(lmac_msg) + param_len
    hdr = struct.pack("<HBB", (length + 4) & 0x0FFF, USB_TYPE_CFG_CMD_RSP, 0)
    return hdr + b"\0" * 4 + struct.pack("<HHHH", msg_id, dest, src, len(params)) + params


def parse_frame(buf):
    """Return (type, msg_id, params) or (type, None, raw) for non-message frames."""
    pkt_len = buf[0] | ((buf[1] & 0x0F) << 8)
    ftype = buf[2]
    body = bytes(buf[4:4 + pkt_len])
    if ftype == USB_TYPE_CFG_CMD_RSP and len(body) >= 12:
        msg_id, _dest, _src, plen, _pattern = struct.unpack_from("<HHHHI", body)
        return ftype, msg_id, body[12:12 + plen]
    return ftype, None, body


def transact(dev, msg_id, params, want_id, timeout_s=2.0):
    dev.write(EP_CMD_OUT, build_cmd(msg_id, params), timeout=1000)
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            buf = dev.read(EP_CMD_IN, 2048, timeout=300)
        except usb.core.USBTimeoutError:
            continue
        ftype, rid, payload = parse_frame(buf)
        if ftype == USB_TYPE_CFG_PRINT:
            print("  fw log:", payload.split(b"\0")[0].decode(errors="replace").strip())
        elif rid == want_id:
            return payload
        else:
            print(f"  other frame type=0x{ftype:02x} id={rid} len={len(payload)}")
    raise TimeoutError(f"no 0x{want_id:x} within {timeout_s}s")


def main():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("dongle 368b:8d81 not found (is it passed to the VM?)")
    reattach = dev.is_kernel_driver_active(IFACE)
    if reattach:
        dev.detach_kernel_driver(IFACE)
    usb.util.claim_interface(dev, IFACE)
    try:
        ver = transact(dev, MM_GET_FW_VERSION_REQ, b"\0", MM_GET_FW_VERSION_CFM)
        n = ver[0]
        print("fw version:", ver[1:1 + n].split(b"\0")[0].decode(errors="replace"))

        mac = transact(dev, MM_GET_MAC_ADDR_REQ, struct.pack("<I", 1), MM_GET_MAC_ADDR_CFM)
        print("mac addr  :", ":".join(f"{b:02x}" for b in mac[:6]))
    finally:
        usb.util.release_interface(dev, IFACE)
        if reattach:
            try:
                dev.attach_kernel_driver(IFACE)
            except usb.core.USBError as e:
                print("reattach failed:", e)


if __name__ == "__main__":
    main()
