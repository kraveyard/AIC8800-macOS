# AIC8800D80 Wi-Fi for macOS Tahoe

An experimental native Wi-Fi driver for the AICSemi AIC8800D80 USB dongle
(Wi-Fi 6) on macOS Tahoe 26.x, Intel x86_64.

The driver is `AirportItlwm.kext`: the AirportItlwm-Tahoe base
(OpenIntelWireless itlwm, Tahoe port by Youtubefan01/laobamac) plus an AIC8800
USB backend. It brings the dongle up from cold, shows it in the Wi-Fi menu,
scans, joins WPA2-PSK networks and passes traffic.

**Status: experimental.** One chip revision (D80 U02) is supported, on Tahoe
only. Expect kernel panics; read the limits before installing.

* [User guide: what works, installing, limits, troubleshooting](docs/README.md)
* [Protocol notes: USB framing, firmware upload, scan/connect/key/data](docs/PROTOCOL.md)

## Build

Build on macOS with Xcode 26:

```
cd third_party/airportitlwmtahoe/src/airportitlwmtahoe/src/itlwm
xcodebuild -project itlwm.xcodeproj -target AirportItlwm-Tahoe -configuration Release \
  SYMROOT=$PWD/build OBJROOT=$PWD/build/obj CODE_SIGNING_ALLOWED=NO
```

The optional stage-2 diagnostic kext builds with `kext/build.sh`.

## Firmware and licence

The driver code is GPL-2.0 (see `LICENSE`). The AIC8800 firmware embedded in
`hal_aic/AICLoaderSeq.h` is AICSemi's proprietary firmware and is not covered by
the GPL. It is included here for interoperability, so that the built kext works
out of the box. AIC has not published an explicit redistribution licence; if you
are the rights holder and want it removed, open an issue and it will be taken
down promptly. See `NOTICE` for the full third-party list.

Usbmon captures are not distributed with this repository: they contain WPA2
session keys and device identifiers.
