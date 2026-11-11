# AIC8800D80 USB protocol notes (from Linux aic8800 v6.4.3.0, /usr/src/aic8800-1.0.0)

Wi-Fi only. Bluetooth interfaces (0, 1) are ignored.

## USB personalities (one physical dongle, 3 enumerations)

| Stage | VID:PID   | Product    | Who handles it on Linux            |
|-------|-----------|------------|------------------------------------|
| 1     | a69c:57xx / 1111:1111 | "Aic MSC" (fake CD) | udev `eject` / usb_modeswitch (CBW with CDB `FD 00..00 F2`) |
| 2     | a69c:8d80 | "AIC Wlan" (ROM bootloader) | `aic_load_fw`: uploads patch_table, adid, patch(+ext0), fmacfw, then chip reboots |
| 3     | 368b:8d81 | "AIC 8800D80" (run mode) | `aic8800_fdrv` (chipmatch = AIC8800D81) |

In stage 3 the Wi-Fi function is **interface 2** (class ff/ff/ff, 4 bulk EPs, 512 B, high speed).

Firmware files used for D80 u02 (from /lib/firmware/aic8800D80):
fw_patch_table_8800d80_u02.bin, fw_adid_8800d80_u02.bin, fw_patch_8800d80_u02.bin,
fw_patch_8800d80_u02_ext0.bin (ext patch @ 0x20b43c), fmacfw_8800d80_u02.bin,
aic_userconfig_8800d80.txt (TX power tables).

## Endpoints (interface 2, assigned in descriptor order)

| EP   | Dir | Role (Linux name) |
|------|-----|-------------------|
| 0x01 | OUT | data TX (`bulk_out_pipe`) |
| 0x81 | IN  | data RX (`bulk_in_pipe`) |
| 0x02 | OUT | command TX (`msg_out_pipe`, CONFIG_USB_MSG_OUT_EP=y) |
| 0x82 | IN  | command response / event RX (`msg_in_pipe`, CONFIG_USB_MSG_IN_EP=y) |

## Command frame (host -> chip, EP 0x02) — `aicwf_set_cmd_tx`, rwnx_cmds.c

All little-endian. `len = 8 + param_len` (sizeof lmac_msg header + params).

```
off  size
0    2    (len + 4) & 0x0fff
2    1    0x11  (USB_TYPE_CFG_CMD_RSP)
3    1    0x00
4    4    dummy word, 0
8    2    msg id
10   2    dest task id
12   2    src task id
14   2    param_len
16   n    params
```
Total transfer = len + 8. Optional ZLP if CONFIG_USE_USB_ZERO_PACKET.

## Response / event frame (chip -> host, EP 0x82; also seen on 0x81)

```
0    2    pkt_len (low 12 bits), total on wire = ALIGN(pkt_len + 4, 4)
2    1    type: 0x11 CMD_RSP (lmac msg), 0x12 DATA_CFM (tx confirm, pkt_len 8), 0x13 PRINT (fw log); data frames have type & 0x10 == 0
3    1    -
4..       struct ipc_e2a_msg { u16 id; u16 dest; u16 src; u16 param_len; u32 pattern; u32 param[] }
```
Messages larger than AICWF_USB_MSG_MAX_PKT_SIZE arrive in fragments; reassemble until ALIGN(pkt_len+4,4) bytes.

## Init sequence for D81 (rwnx_cfg80211_init, rwnx_main.c ~8880-9260)

1. `rwnx_platform_on`: patch config, userconfig TX power (txpwr_lvl_v3, txpwr_lvl_adj, txpwr_ofst2x), RF calib (aicwf_compat_8800d80.c)
2. SET_STACK_START_REQ (1, 0, BIT(5), fwlog_en)
3. GET_FW_VERSION_REQ
4. GET_MAC_ADDR_REQ (efuse MAC)
5. MM_RESET_REQ
6. set_temp_comp_req
7. MM_VERSION_REQ
8. ME_CONFIG_REQ
9. ME_CHAN_CONFIG_REQ (country code)
10. on interface up: MM_START_REQ, MM_ADD_IF_REQ (STA)
11. scan: SCANU_START_REQ -> SCANU_RESULT_IND (one per BSS, carries beacon/probe-resp) ... SCANU_START_CFM

### Proven sequence (usbmon capture, not distributed; replayable with tools/aic_replay_scan.py — 2026-11-07)

Each request gets a CFM with id+1, except SCANU_START which ends with SCANU_START_CFM (0x1001).
D81 does NOT send the txpwr lvl_v3/adj/ofst2x set; it sends a single SET_TXPWR_IDX_LVL.

| # | id     | name |
|---|--------|------|
| 1 | 0x0400 | DBG_MEM_READ_REQ (chip rev) |
| 2 | 0x007b | MM_SET_STACK_START_REQ |
| 3 | 0x0080 | MM_GET_FW_VERSION_REQ |
| 4 | 0x0077 | MM_SET_TXPWR_IDX_LVL_REQ |
| 5 | 0x0069 | MM_SET_RF_CALIB_REQ |
| 6 | 0x0073 | MM_GET_MAC_ADDR_REQ |
| 7 | 0x0000 | MM_RESET_REQ |
| 8 | 0x0004 | MM_VERSION_REQ |
| 9 | 0x1400 | ME_CONFIG_REQ |
| 10| 0x1402 | ME_CHAN_CONFIG_REQ (x2) |
| 11| 0x0002 | MM_START_REQ |
| 12| 0x0065 | MM_SET_COEX_REQ |
| 13| 0x0006 | MM_ADD_IF_REQ (STA, contains MAC) |
| 14| 0x1007 | SCANU_VENDOR_IE_REQ |
| 15| 0x1000 | SCANU_START_REQ -> 0x1009 START_CFM_ADDTIONAL, 0x1004 RESULT_IND xN, 0x004f CHANNEL_SURVEY_IND, 0x1001 START_CFM |

Several messages can share one bulk-IN transfer; each is padded to 4 bytes.
SCANU_RESULT_IND params: u16 length, u16 framectrl, u16 center_freq, u8 band, u8 sta_idx, u8 inst_nbr, s8 rssi, then the raw 802.11 beacon/probe-resp (12-byte header).

Message ID names: tools/hdr/msg_names.txt (generated from lmac_msg.h with FULLMAC flags).

## Connect / keys / data (local capture, not distributed; WPA2-PSK CCMP, 5240 MHz — 2026-11-07)

Struct layouts (compiled from lmac_msg.h): tools/hdr/layouts.txt.

Connect flow: ADD_IF (STA) -> SM_CONNECT_REQ (0x1800) -> SM_CONNECT_CFM (0x1801, status u8)
-> MM_CHANNEL_SWITCH_IND (0x0044) -> SM_CONNECT_IND (0x1802) -> EAPOL 4-way over data EPs
-> MM_KEY_ADD_REQ pairwise -> MM_KEY_ADD_REQ group -> ME_SET_CONTROL_PORT_REQ(sta 0, open 1)
-> (Linux extras: MM_CFG_RSSI, MM_GET_STA_INFO, MM_SET_ARPOFFLOAD; ME_TX_CREDITS_UPDATE_IND 0x140b arrives).
Disconnect: SM_DISCONNECT_REQ (0x1803) -> SM_DISCONNECT_CFM (0x1804) + SM_DISCONNECT_IND (0x1805) -> MM_KEY_DEL_REQ (0x0026) per hw key.

sm_connect_req (320 B): ssid{u8 len; char[32]}@0, bssid@34, chan{u16 freq; u8 band; u8 flags; s8 txpwr}@40,
flags u32@48 (=0x9 observed), ctrl_port_ethertype u16@52 (0x888e network order -> bytes 88 8e),
ie_len u16@54, listen_interval@56, dont_wait_bcmc@58, auth_type@59 (0 open), uapsd_queues@60, vif_idx@61, ie_buf[256]@64.
Observed IEs: RSN(48) CCMP/CCMP/PSK caps 0x008c, ExtCap(127), SuppOpClasses(59), 244(RSNXE) 0x20.

sm_connect_ind (852 B): status_code u16@0 (0 ok), bssid@2, roamed@8, vif_idx@9, ap_idx@10 (sta idx of AP), ch_idx@11, qos@12,
acm@13, assoc_req_ie_len@14, assoc_rsp_ie_len@16, assoc_ie_buf@20 (req IEs then rsp IEs), aid@820, band@822, center_freq@824, width@826.

mm_key_add_req (44 B): key_idx@0, sta_idx@1 (255 = group/broadcast), key{u8 len; pad3; u32[8]}@4, cipher_suite@40 (2 = CCMP),
inst_nbr@41 (vif), spp@42, pairwise@43. Observed: PTK -> key_idx 0, sta 0, pairwise 1; GTK -> key_idx 2 (= GTK keyid), sta 255, pairwise 0.
me_set_control_port_req: sta_idx@0, open@1.  mm_key_del_req: hw_key_idx@0.

### Data TX (host -> EP 0x01)
```
0  u16 total_len (incl. this 4-byte hdr? no: = 4 + 28 + payload_len, low 12 bits)  2 u8 0x01 (data)  3 u8 0
4  hostdesc (28 B): u16 packet_len (payload, no eth hdr); u16 flags_ext; u32 status_desc_addr (cookie, 0x80000000+n);
   u8 eth_dest[6]; u8 eth_src[6]; u16 ethertype (network order); u8 ac; u8 tid; u8 vif_idx; u8 staid; u16 flags
32 payload (frame after the 14-byte Ethernet header), then pad to 4 bytes
```
TX confirm on EP 0x81: 12 bytes, type 0x12 (DATA_CFM): `08 00 12 00 | u32 ? (0x9) | u32 cookie-low`.

### Data RX (chip -> EP 0x81), type byte 2 == 0x00
60-byte hw_rxhdr, then a raw 802.11 frame of hwvect.len bytes:
- @0 u16 hwvect.len (= 802.11 frame length), @2 u8 type (0x00 data; 0x1x = msg), @4 tsf_lo, @8 tsf_hi
- @12 rx_vect1: @14 s8 rssi_leg (dBm)
- @36 u32 status word: bits 2-4 decr_status (3 = decrypted CCMP)
- @40 phy_info: u8 band (0 2.4G, 1 5G), u8 chan_type, u16 prim20_freq @42, u16 center1 @44, u16 center2 @46
- @48 u32 flags: bit1 is_80211_mpdu (mgmt), bit6 upload
Encrypted data arrives HW-decrypted: Protected bit still set, 8-byte CCMP header kept, plaintext LLC payload, MIC stripped
-> feed to net80211 with IEEE80211_RXI_HWDEC.

Notes added 2026-11-08 while bringing up the macOS driver (`hal_aic/ItlAic.cpp`):
- **RX transfer parsing** (Linux `aicwf_process_rxframes`, `aicwf_usb_rx_aggr = true` for D80): one bulk-IN transfer can hold several
  elements. Byte 2 bit 4 (`USB_TYPE_CFG` 0x10) tells them apart:
  - data (`type & 0x10 == 0`): u16 frame length at @0, 60-byte rx header, then the frame. The element is exactly `len + 60` bytes,
    **no padding**.
  - config (`type & 0x10`): u16 length (low 12 bits) at @0, 4-byte header, then `len` bytes padded to 4. Type 0x11 = lmac message,
    0x12 = TX confirm, 0x13 = firmware print.
  - Data-pipe transfers can be up to 20 KiB (Linux `AICWF_USB_AGGR_MAX_PKT_SIZE`); the driver reads 32 KiB.
- **hw_rxhdr flags @48**: bit 1 `is_80211_mpdu` (management frame), bit 5 `need_reord`, bit 6 `upload`. Linux only forwards frames
  with `upload` set. Beacons from the joined AP arrive on the data pipe too (flags 0x42).
- **Protected frames**: the Protected bit and the 8-byte CCMP header are kept, the MIC is stripped. net80211 does the PN replay check
  itself when given `IEEE80211_RXI_HWDEC` (`ieee80211_input_hwdecrypt`).
- **TX**: the 4-byte header length is `4 + 28 + payload` (12 bits). Frames without a TX confirm are not padded and use
  `status_desc_addr` 0; Linux pads to 4 and sets `0x80000000 + n` only when it wants a confirm (EAPOL).
- **sm_connect_ind**: `assoc_ie_buf` holds IEs only (no capability/status/AID fixed fields): request IEs, then response IEs
  (the response starts with Supported Rates). AID at @820.
- **sm_connect_req** as Linux sends it for WPA2: flags 0x9 (`CONTROL_PORT_HOST | WPA_WPA2_IN_USE`), `uapsd_queues` @60 = 1, IEs = RSN
  (+ ExtCap, op classes, RSNXE; the macOS driver sends only RSN/WPA from net80211).
- **MAC changes**: the firmware vif keeps the MAC from `MM_ADD_IF`. macOS uses a private Wi-Fi MAC per network, so the driver sends
  `MM_REMOVE_IF` (u8 vif) + `MM_ADD_IF` with the new address before connecting.
- **Scan**: a second `SCANU_START_REQ` while a scan runs gets no `SCANU_START_CFM_ADDTIONAL` (0x1009). Wait for `SCANU_START_CFM`.
- **Rekeys**: an EAPOL-Key Request from the station (no Pairwise bit = GTK, Pairwise bit = PTK) makes a hostapd-style AP rekey at once.
  A new GTK uses the other key id (1/2) and its own HW slot; a new PTK reuses `sta_idx`/key 0.
- Message names used: 0x0056 MM_CFG_RSSI_REQ, 0x0075 MM_GET_STA_INFO_REQ, 0x140b ME_TX_CREDITS_UPDATE_IND (not needed so far).

## Cold-start bring-up (local capture, not distributed — 2026-11-08)

Linux on plug-in: udev `aic.rules` runs `eject /dev/sdX` on the fake CD, then `aic_load_fw` uploads to the bootloader.
The full sequence took 4.6 s on Linux and ~3.5 s in macOS (`hal_aic/AicLoader.cpp`).

### a69c:5721 "Aic MSC" (mass storage, interface 0, bulk 0x02 OUT / 0x82 IN, 512 B)
- Standard bulk-only transport. The switch is triggered by eject: ALLOW MEDIUM REMOVAL (`1e 00 00 00 00 00`) then
  START STOP UNIT with LoEj (`1b 00 00 00 02 00`), each in a 31-byte CBW (data length 0, LUN 0) followed by a 13-byte CSW read.
- Linux sent eject many times before the device dropped off; macOS got it on the first round. The chip then re-enumerates as 8d80.
- Clone dongles with VID:PID 1111:1111 use a vendor CDB instead (`FD 00 … 00 F2`, `usb_modeswitch/1111_1111`), not implemented.

### a69c:8d80 "AIC Wlan" (ROM bootloader, interface 0, class ff, bulk 0x01 OUT / 0x82 IN, 512 B)
- Same command framing as run mode (16-byte header + lmac message, `src` 100), but commands go to **bulk 0x01**. Replies on 0x82.
- Linux keeps a 3712-byte IN URB queued; any request size works.
- Messages (dest task 1, DBG):
  | id | name | params | CFM |
  |---|---|---|---|
  | 0x0400 | DBG_MEM_READ_REQ | u32 addr | 0x0401, 24 bytes: u32 addr, u32 value |
  | 0x0402 | DBG_MEM_WRITE_REQ | u32 addr, u32 value | 0x0403 |
  | 0x040b | DBG_MEM_BLOCK_WRITE_REQ | u32 addr, u32 len, data (1024 B per frame) | 0x040c, **20 bytes** (u32 status) |
  | 0x040d | DBG_START_APP_REQ | u32 boot addr, u32 boot type | none: the chip reboots into the firmware |
- The D80 U02 sequence: 6 reads, 155 writes, 400 block writes (patch table, adid @0x201940, patch, ext0 patch @0x20b43c, fmacfw
  ~400 KB), then START_APP. After START_APP the chip re-enumerates as 368b:8d81 within ~1 s.
- Reads: `0x40500000` = `0xf9078820` (chip ID), `0x40100020` = 2 (revision U02; reads 3 once a previous upload wrote it), then four
  firmware-table pointers at 0x120198/0x1201a0/0x12001c/0x1201a4 read after the upload.
- No control transfers besides enumeration and SET_CONFIGURATION.

## Milestones

The scan, WPA2, data path and cold-start loader are implemented; see `README.md` for what is tested.
