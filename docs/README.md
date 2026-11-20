# AIC8800D80 Wi-Fi for macOS Tahoe

Native Wi-Fi for the AICSemi AIC8800D80 USB dongle (Wi-Fi 6 + BT 5) on macOS Tahoe 26.x (Intel, x86_64).
The dongle shows up in the Wi-Fi menu, scans, joins WPA2 networks and carries traffic like built-in Wi-Fi.

The driver is `AirportItlwm.kext`: the AirportItlwm-Tahoe base (OpenIntelWireless itlwm, Tahoe port by
Youtubefan01/laobamac) plus a new AIC8800 USB backend. Licence: GPL-2.0.

Companion doc: `PROTOCOL.md` (USB/firmware protocol).

---

## What works (tested 2026-11-08 in a QEMU/KVM macOS 26.7 VM with the dongle passed through)

| Feature | State |
|---|---|
| Cold start: the kext loads the dongle's firmware itself | ✅ plug-in to connected in ~14 s in the VM |
| Wi-Fi menu: scan, 2.4 and 5 GHz networks, signal, security type | ✅ |
| Join WPA2-Personal (CCMP) from the menu, auto-join at boot | ✅ |
| DHCP, IPv4/IPv6, internet | ✅ ~6 MB/s download, 0 % ping loss |
| macOS private Wi-Fi address (per-network random MAC) | ✅ |
| Group-key and pairwise rekeys | ✅ forced from the client, traffic continues |
| Unplugging while connected | ✅ no panic |
| Open networks | code done, not tested |
| Unplug and replug in the same boot | ❌ Wi-Fi stays off until reboot (see Limits) |
| Sleep/wake, WPA3, hidden-network edge cases, Bluetooth | not tested / not supported |

Hardware it was built for: AIC8800D80 revision U02, chip ID `0xf9078820`, USB IDs `a69c:5721` → `a69c:8d80` → `368b:8d81`.

### Additional support in version2

- UGREEN AIC8800D80 adapters use `a69c:5724` in driver-disk mode and `368b:8d88`
  after firmware loads. Both USB IDs are matched.
- The driver now keeps beacon updates flowing to macOS while connected, fixing
  the recurring disconnect after roughly one minute.
- While connected, the driver can scan passively without leaving the current
  network. Weather successfully resolved the current location on a physical
  AIC8800D80 setup running macOS Tahoe 26.7.3.

---

## Installing

### Hackintosh (OpenCore) — recommended
1. Copy `AirportItlwm.kext` to `EFI/OC/Kexts/`.
2. Add it to `config.plist` → `Kernel` → `Add`:
   - `BundlePath` `AirportItlwm.kext`, `ExecutablePath` `Contents/MacOS/AirportItlwm`, `PlistPath` `Contents/Info.plist`
   - `MinKernel` `25.0.0`, `MaxKernel` `25.99.99` (built for Tahoe's IO80211Family 1200.x only), `Arch` `x86_64`, `Enabled` true
   - Do not also load another AirportItlwm/itlwm build.
3. Kexts injected by OpenCore need no approval in System Settings.
4. Plug the dongle into any working USB port, before or after boot.

#### Tahoe USB note (keyboard/mouse dead in the installer)
Tahoe ignores the old per-port keys in USB map kexts (`port`, `UsbConnector`). Each port in `USBMap.kext` also needs
`usb-port-number` (Data, same 4 bytes as `port`) and `usb-port-type` (Number, same as `UsbConnector`), and the personality must use
`AppleUSBHostMergeProperties`. Keep the old keys so the map also works on older macOS. Reset NVRAM once after changing the map.
If you use `ResetNvramEntry.efi`, run it with `--preserve-boot` to keep non-macOS boot entries.

### Real Mac or macOS without OpenCore injection
1. `sudo cp -R AirportItlwm.kext /Library/Extensions/ && sudo chown -R root:wheel /Library/Extensions/AirportItlwm.kext`
2. SIP must allow unsigned kexts (for example `csr-active-config 03080000`).
3. Approve it in System Settings → Privacy & Security, then reboot. Every rebuilt binary needs approving again; to skip the dialog:
   `sudo kmutil create --allow-missing-kdk --volume-root / --new aux --boot-path /System/Library/KernelCollections/BootKernelExtensions.kc --system-path /System/Library/KernelCollections/SystemKernelExtensions.kc`
   and reboot. The stage-2 diagnostic `AIC8800.kext` must not be installed at the same time (it claims the same USB interface).

---

## How it works

### Bringing the dongle up (`AicLoader`)
The dongle enumerates three times:
1. **`a69c:5721` "Aic MSC"** — a fake CD drive. The kext ejects it (SCSI START STOP UNIT with LoEj), like Linux's udev rule.
2. **`a69c:8d80` "AIC Wlan"** — the chip's ROM bootloader. The kext uploads the firmware (patch table, adid, patch, ext patch, main
   firmware; ~420 KB, 562 messages) and starts it. Before uploading it checks the chip ID and refuses other chips.
3. **`368b:8d81` "AIC 8800D80"** — firmware running. The Wi-Fi driver takes over.
The upload is a replay of what Linux `aic_load_fw` sends to this chip revision, recorded with usbmon.

### Wi-Fi (`AicUSBDeviceWrapper` + `ItlAic`)
```
Wi-Fi menu / airportd ──► IO80211Family (Tahoe Skywalk) ──► AirportItlwm + WCL layer
                                                               │
                                     net80211 (OpenBSD): node table, state machine, WPA2 4-way handshake
                                                               │
                                     ItlAic: USB transport, firmware commands, data path
                                                               │
                                     AIC8800 firmware (fullmac: does auth/assoc and encryption)
```
- **Scan:** `SCANU_START_REQ`; each result is a raw beacon fed to net80211, so macOS sees normal scan results.
  While associated, the driver uses the same firmware command in passive mode,
  honors channel restrictions from macOS, and reports scan completion or
  failure without changing the association.
- **Join:** the firmware does authentication and association (`SM_CONNECT_REQ`). When it reports success, the driver injects
  matching auth/assoc responses so net80211 moves to "associated" and runs the WPA2 handshake itself.
- **Keys:** net80211's keys go to the firmware with `MM_KEY_ADD_REQ`; the firmware encrypts and decrypts.
- **Data:** received frames are already decrypted 802.11 frames; sent frames are Ethernet frames with a 28-byte descriptor.
- USB is read by two kernel threads; command replies are matched directly, everything else is handed to the driver's workloop.

### Changes to the AirportItlwm base
- The USB nub subclasses the PCI wrapper, so the existing AirportItlwm personality accepts it; PCI-only setup runs only for PCI.
- Two base bugs that only USB exposes are fixed in `AirportItlwmSkywalkInterface::stop()`: a heap-zone mismatch when freeing the
  Skywalk registration info, and a NULL dereference when WCL updated link quality on an already stopped interface.

---

## Limits and known issues
- **Replug in the same boot:** after unplugging, the dongle is brought up again but airportd never powers the new interface on
  ("Wireless is not available"). Reboot to get Wi-Fi back.
- **One chip revision:** the firmware upload only supports the recorded chip (D80 U02). Other AIC8800 variants (D80X2, D81, clones
  with VID:PID 1111:1111) are refused or not matched.
- **Idle latency** ~90 ms (≈9 ms under load), probably firmware power save.
- **Scanning while connected** is passive. Hidden networks may require a directed scan.
- **No A-MPDU reordering** on receive; frames reach the network stack in arrival order.
- **Tahoe only.** A macOS update can change the private IO80211 interfaces; rebuild and recheck with `kmutil libraries -p`.
- The kext still logs key installs and the first 40 sent/received frames of each connection (for debugging).

---

## Troubleshooting
Driver messages carry the prefix `AIC8800` (and `AIC8800 loader` for the bring-up). Read them with:
```
sudo log show --last 10m --style compact --predicate 'eventMessage CONTAINS "AIC8800"'
```
| Message | Meaning |
|---|---|
| `loader: mass-storage mode, starting` / `eject sent` | step 1 ran |
| `loader: bootloader mode` … `firmware uploaded (562 frames)` | step 2 done; the Wi-Fi device should appear within a few seconds |
| `loader: … not this chip, stopping` | the dongle is a different chip/revision; no firmware was uploaded |
| `loader: device ignored eject` | the fake CD did not switch; replug and check the USB IDs |
| `firmware … mac …, vif 0` | Wi-Fi firmware answered |
| `connecting to …` / `connected, ap sta 0` | the firmware joined the AP |
| `key pairwise …` / `key group …` / `control port … open 1` | WPA2 keys installed, traffic allowed |
| `timeout waiting for cfm 0x…` | the firmware did not answer a command |
| `vif mac -> …` | the private Wi-Fi address was applied |
| `background scan started …` / `background scan completed …` | a passive scan ran while connected; completion logs the firmware status and result count |
| `background scan timed out …` | the firmware did not finish the scan in time; the driver attempted to cancel it |

- Self-assigned 169.254.x.x address: DHCP got no answer; check that `vif mac` matches `ifconfig en1`'s `ether`.
- Keyboard/mouse dead in the Tahoe installer: see the Tahoe USB note above.
- Rekeys can be tested in seconds: send an EAPOL-Key Request from the client (see `PROTOCOL.md`, Notes added 2026-11-08).

---

## Firmware and licence
The driver code is GPL-2.0 (itlwm base, Linux aic8800 driver as reference). `hal_aic/AICLoaderSeq.h` embeds AICSemi's firmware
files from the Linux aic8800 package. The firmware is a separate proprietary work and is not covered by the GPL; check its
licence before redistributing a build. Captures are not distributed with the repository because they contain session keys
and device identifiers.

---

## Source map
| Path (under `third_party/airportitlwmtahoe/src/airportitlwmtahoe/src/itlwm/`) | What |
|---|---|
| `itlwm/hal_aic/ItlAic.{hpp,cpp}` | Wi-Fi backend (HAL) |
| `itlwm/hal_aic/AicUSBDeviceWrapper.{hpp,cpp}` | USB nub for 368b:8d81 interface 2 |
| `itlwm/hal_aic/AicLoader.cpp` | Cold-start eject + firmware upload |
| `itlwm/hal_aic/AICInitSeq.h`, `AICLoaderSeq.h` | Generated replay tables (`tools/gen_init_table.py`, `tools/gen_loader_table.py`) |
| `AirportItlwm/IOPCIEDeviceWrapper.cpp` | Compiles the hal_aic sources (stop-gap instead of editing the Xcode project) |
| `AirportItlwm/WCL/Info.plist` | Personalities `aic8800`, `aic8800-eject`, `aic8800-bootloader` |

Build (in macOS with Xcode 26.6): `xcodebuild -project itlwm.xcodeproj -target AirportItlwm-Tahoe -configuration Release`.
