# AirportItlwm for macOS Tahoe (26.x) — Intel AX210

A native macOS Wi-Fi driver for Intel iwl/iwx adapters (AX200, AX210, AX211, …)
built against the **macOS 26 (Tahoe)** private IO80211 API.

The upstream project this is derived from does not support Tahoe. This repository
carries both a **prebuilt, ad-hoc signed kext** and the **complete source tree
needed to rebuild it from scratch**, plus a roadmap for the Apple Continuity
features that do not work yet.

---

## Contents

| Path | What it is |
|---|---|
| `AirportItlwm.kext/` | The prebuilt, signed kext (x86_64, ad-hoc) |
| `src/itlwm/` | The complete driver source, vendored at a pinned commit |
| `src/itlwm/MacKernelSDK/` | The kernel headers the build needs, vendored too |
| `LICENSE` | GNU GPL v2 |

---

## The prebuilt kext

| | |
|---|---|
| Architecture | x86_64 (Mach-O 64-bit kext bundle) |
| Bundle identifier | `com.zxystd.AirportItlwm` |
| Version | 2.4.0 |
| Signing | ad-hoc (`codesign -v` → VALID) |
| Size | 15,943,056 bytes |
| SHA-256 of `Contents/MacOS/AirportItlwm` | `4401df85eba33535928fcf5f7df42d58400683b84d0881a54d4af935b303dd35` |

Also attached to the [v1.1.0 release](releases/tag/v1.1.0) as
`AirportItlwm-Tahoe-v1.1.0.zip`, together with
`AirportItlwm-Tahoe-v1.1.0-source.tar.gz` — a tarball of this entire repository,
which is everything needed to rebuild the kext. The kext in v1.1.0 is
byte-identical to the v1.0.0 kext; v1.1.0 adds the source tree and this README.

### Installing

1. Copy `AirportItlwm.kext` into your OpenCore `EFI/OC/Kexts/` folder.
2. Add it to `config.plist` → `Kernel` → `Add`:

```xml
<dict>
    <key>Arch</key>
    <string>x86_64</string>
    <key>BundlePath</key>
    <string>AirportItlwm.kext</string>
    <key>Enabled</key>
    <true/>
    <key>Identifier</key>
    <string>com.zxystd.AirportItlwm</string>
    <key>MaxKernel</key>
    <string>25.99.99</string>
    <key>MinKernel</key>
    <string>25.0.0</string>
    <key>PlistPath</key>
    <string>AirportItlwm.kext/Contents/Info.plist</string>
</dict>
```

3. Reboot.

`MinKernel`/`MaxKernel` pin it to Darwin 25.x so it can never be loaded on an
older macOS by mistake — the Tahoe and Sequoia builds have different ABIs and
are separate build products.

**SIP must be disabled** (Kext Signing included) for any third-party kext to
load at all.

---

## Feature support

Verified against the source in `src/itlwm/`, not copied from a marketing page.

| | Status |
|---|---|
| WPA2-Personal (PSK, CCMP/TKIP) | Supported |
| WPA2/WPA3-transition networks | Supported (WPA2 side) |
| WPA3-only (SAE) | **Not** supported |
| WPA3-Enterprise (SUITE-B, 192-bit) | **Not** supported |
| WPA2-Enterprise (802.1X/EAP) | **Not** supported |
| PMF / 802.11w | Optional (MFPC) only; mandatory PMF not supported |
| 802.11n (Wi-Fi 4) | Supported |
| 802.11ac (Wi-Fi 5) | Supported |
| 802.11ax (Wi-Fi 6 / HE) | Supported, but **opt-in** — see below |
| 802.11be (Wi-Fi 7) | Not supported |
| 6 GHz / Wi-Fi 6E | Not supported |
| AWDL / AirDrop / Handoff-dependent features | Not supported — see [Roadmap](#roadmap) |
| MLO (multi-link) | Not supported |

**Wi-Fi 6 (HE) is disabled by default.** Add the boot-arg `itlwm_he=1` to enable
it. Without the flag the adapter advertises `802.11 a/b/g/n/ac` only; with it,
`ax` is added. The gate lives in `AirportItlwm/AirportItlwmV2.cpp` and is
consumed by `IEEE80211_F_HEON` in `ieee80211.c`, `ieee80211_proto.c` and
`ieee80211_output.c`.

**6 GHz is unavailable on this hardware path.** The regulatory domain in
`itl80211/openbsd/net80211/ieee80211_regdomain.h` tops out at 5825 MHz — there
are no 6 GHz channels at all — and `ieee80211_he_supported()` explicitly
refuses a node whose HE operation parameters carry
`IEEE80211_HE_OPERATION_6GHZ_OP_INFO`. 6 GHz would additionally require WPA3/SAE,
which this stack does not implement.

### Why WPA3 is missing

The AKM enum in `itl80211/openbsd/net80211/ieee80211_crypto.h` defines exactly
four authentication key management modes — `NONE`, `8021X`, `PSK` and the SHA-256
variants for 11w. There is no `SAE` and no `SUITE_B` entry, and no SAE
implementation exists anywhere in the tree. WPA2/WPA3-transition networks work
because the WPA2 side of the handshake is fully implemented (PSK derivation,
PMKID caching, PTK derivation and the four-way EAPOL-Key exchange all live in
`ieee80211_pae_input.c` / `ieee80211_pae_output.c`), and the AP accepts a WPA2
supplicant on a mixed-mode network.

---

## Building from source

Everything needed is in this repository. There is nothing to download except
Xcode itself.

### Requirements

- **macOS** with Xcode 15 or newer (built and tested with Xcode 26.5 on
  macOS 26.5 / Darwin 25.6.0)
- **Python 3** — used by `scripts/zlib_compress_fw.py` to generate the embedded
  firmware blob
- No network access is required; the Intel firmware images are vendored

### Layout

```
src/itlwm/                        the driver source (itlwm fork, pinned commit)
├── itlwm.xcodeproj/              Xcode project — includes the AirportItlwm-Tahoe scheme
├── AirportItlwm/                 the macOS IO80211 glue, including the Tahoe WCL class
│   ├── WCL/                      WCL.cpp / WCL.hpp — the new Tahoe "WCL" class
│   └── AirportAWDL.cpp           AWDL hooks (currently ABI stubs)
├── include/Airport/              private Apple headers
│   ├── macOS26/                  Tahoe-correct IO80211 headers
│   ├── macOS15/                  Sequoia headers
│   └── IO80211InfraProtocol.h    header dispatch on __IO80211_TARGET
├── itl80211/openbsd/net80211/    the OpenBSD-derived 802.11 stack
├── itlwm/                        the Intel iwl/iwx firmware HAL
│   └── firmware/                 vendored Intel firmware images (35 files, 33 MB)
├── scripts/fw_gen.sh             pre-build step: generates include/FwBinary.cpp
└── MacKernelSDK/                 kernel headers + libkmod.a (pinned commit)
```

### Build

```sh
cd src/itlwm

xcodebuild -project itlwm.xcodeproj \
    -scheme AirportItlwm-Tahoe \
    -configuration Release \
    ARCHS=x86_64 \
    CODE_SIGNING_ALLOWED=NO \
    SYMROOT="$PWD/build" \
    build
```

The kext lands in `src/itlwm/build/Release/Tahoe/AirportItlwm.kext`.

`SYMROOT` is passed explicitly because the project does not set it. Without it
Xcode puts the product under `~/Library/Developer/Xcode/DerivedData/…`, which is
fine but awkward to find. The `AirportItlwm-Tahoe` scheme also sets
`CONFIGURATION_BUILD_DIR` to `$(SYMROOT)/$(CONFIGURATION)/Tahoe`, which is why the
product sits in a `Tahoe/` subdirectory rather than directly in `Release/`.

The first build generates `src/itlwm/include/FwBinary.cpp` (~84 MB of embedded,
zlib-compressed firmware) from `src/itlwm/itlwm/firmware/`. That step is
automatic — it is a pre-build phase in the Xcode project, and it is skipped if
the file already exists. It is git-ignored on purpose: it is a build artifact,
not source.

### Sign it

Ad-hoc signing is sufficient when Kext Signing is disabled:

```sh
codesign -f -s - --timestamp=none src/itlwm/build/Release/Tahoe/AirportItlwm.kext
codesign -v --verbose=2 src/itlwm/build/Release/Tahoe/AirportItlwm.kext
```

### Verify it before you ship it

Two checks catch the failure modes that actually bite with private Apple APIs.

**1. Symbols must resolve against the live kernel.** This is the definitive ABI
check — it proves every undefined symbol the kext imports exists in the kernel
collections that are actually installed:

```sh
kmutil libraries -p src/itlwm/build/Release/Tahoe/AirportItlwm.kext
echo $?    # must be 0
```

**2. The vtable must line up.** Apple calls virtual methods by slot index, and
a kext whose `IO80211InfraProtocol` overrides land on the wrong slots will
have the framework call the wrong function — silently, or with a panic. The
real vtables can be read straight out of
`/System/Library/KernelCollections/BootKernelExtensions.kc`: they are exposed
as `__ZTV*` symbols, and the kernel collections use 32-bit compressed pointers
where the loaded address is `0xffffff8000100000 + (raw & 0xFFFFFFFF)`.

Use Apple’s *concrete* implementers to read slot names. `AppleBCMWLANInfraProtocol`
is the useful one: the abstract base leaves some slots pure-virtual, and
`__cxa_pure_virtual` erases their names, but the concrete implementer carries
the real ones.

For this kext the result is: **200 overrides land on exactly the same slot index**
as Tahoe’s real `IO80211InfraProtocol` / `IO80211InfraInterface` /
`IO80211SkywalkInterface`, and the kext’s own new virtuals begin at slot **668**
— precisely where Tahoe’s real `IO80211InfraProtocol` ends. Clean boundary, no
overlap, no gap.

### Reproducibility

A build from this vendored tree produces a kext that is **functionally identical**
to the published one, but not byte-for-byte. Every Mach-O segment and section
has the same size and the same contents — `__text` 537,744 bytes, `__cstring`
43,241, `__const` 14,737,376, `__DATA` 270,336 — and the symbol tables contain
the same 12,110 symbols.

The only difference is the **absolute build path**, which clang records in the
Mach-O debug-info section (`N_SO` "compilation unit source file" entries) and
therefore in the `__LINKEDIT` string table. A build at
`/Users/michael/itlwm-work/laobamac-itlwm` and a build at
`/tmp/whatever` differ in those path strings and nowhere else. That is why the
two builds have different Mach-O UUIDs, and it is expected.

If you need bit-identical output across machines, build from the same absolute
path every time.

### Pinned upstream commits

| Repository | Commit | Why |
|---|---|---|
| [`laobamac/itlwm`](https://github.com/laobamac/itlwm) | `738d4c61b5a64433b27c1ab09dba94916ab77d8d` | Adds the Tahoe layer: `include/Airport/macOS26/`, `AirportItlwm/WCL/`, the `AirportItlwm-Tahoe` scheme |
| [`acidanthera/MacKernelSDK`](https://github.com/acidanthera/MacKernelSDK) | `3f750085caa17ec3a7880f11c11bf4f48cd6a164` | "Fix IOPCIHostBridge instance layout for macOS Tahoe" |
| [`OpenIntelWireless/itlwm`](https://github.com/OpenIntelWireless/itlwm) | 2.4.0 | Upstream origin of the driver and the firmware HAL |

To rebuild from a pristine upstream clone instead of the vendored tree:

```sh
git clone https://github.com/laobamac/itlwm.git
cd itlwm && git checkout 738d4c61b5a64433b27c1ab09dba94916ab77d8d
git clone https://github.com/acidanthera/MacKernelSDK.git
cd MacKernelSDK && git checkout 3f750085caa17ec3a7880f11c11bf4f48cd6a164 && cd ..

xcodebuild -project itlwm.xcodeproj -scheme AirportItlwm-Tahoe \
    -configuration Release ARCHS=x86_64 CODE_SIGNING_ALLOWED=NO \
    SYMROOT="$PWD/build" build
codesign -f -s - --timestamp=none build/Release/Tahoe/AirportItlwm.kext
```

---

## Why the upstream build could not work

`OpenIntelWireless/itlwm` has no Tahoe support — its newest build target is
`AirportItlwm-Sonoma14.4`. Building a Tahoe target against those 14.4-era
headers produces a kext that links cleanly and is nonetheless wrong at runtime.

Apple restructured `IO80211InfraProtocol` between Sonoma and Tahoe. Measured
against the real vtables in the live kernel collection:

- the kext’s `getSSID` sat at vtable slot **451**, while Tahoe’s real `getSSID`
  lives in `IO80211BSSBeacon` at slot **39**
- **199 new WCL virtuals** were added to `IO80211InfraProtocol`
- **144 of the kext’s overrides had no counterpart** in Tahoe’s vtable at all
- the ones that did match were off by anywhere from **−862 to +20** slots

Apple’s framework would have called the wrong function on the interface object.
Apple’s dynamic vtable patchup does not rescue this: the kernel provides only
`OSMetaClassDeclareReservedUnused` / `reservedCalled` for reserved slots, and
there is no general reordering patchup — so an ABI mismatch is fatal.

The Tahoe headers and the WCL class in this tree are what fix it.

---

## Roadmap

> ### ⚠️ Disclaimer — this is a wishlist, not a commitment
>
> Everything in this section is **planned**, not delivered. A meaningful fraction
> of it will probably **never ship**, and that is not a matter of effort or
> engineering time. Some of it is *not implementable on this hardware* rather
> than merely unimplemented.
>
> Specifically:
>
> - The AX210’s firmware is closed and does not expose the raw frame injection
>   and offloaded channel-switching primitives that AWDL needs. If the hardware
>   cannot carry the traffic, no amount of driver work will conjure it.
> - Apple’s AWDL sync/election protocol is proprietary and undocumented. The
>   public IO80211 headers describe the *hook points*, not the protocol.
> - Several Continuity features are additionally gated on hardware that is
>   simply absent from a Hackintosh (e.g. Apple’s own proximity radios,
>   `amfid`-backed device identity, the `IDS` (Apple Internal Delivery Service)
>   trust chain).
>
> The rule for this project is **fail-safe iteration**: if a feature turns out
> to be impossible, it gets documented as impossible rather than forced into
> the driver. No change may trade a missing feature for a kernel panic, a
> corrupted mbuf chain, or a wedged interface. A driver that is stable and
> honestly limited is worth more than one that claims everything and breaks.
>
> Status legend: 🔴 not started · 🟡 in progress · 🟢 done · ⛔ assessed as
> probably impossible.

### Where AWDL actually stands today

This is the honest baseline, and it is more specific than "AWDL does not work":

- `src/itlwm/AirportItlwm/AirportAWDL.cpp` is a **58-line stub**. Every method
  (`getIE`, `setIE`, `setP2P_SCAN`, `setP2P_LISTEN`, `setP2P_GO_CONF`) logs its
  arguments and returns `kIOReturnSuccess`. It exists to keep the vtable
  layout correct, not to implement a protocol.
- `getAWDL_RSDB_CAPS()` is implemented as
  `return kIOReturnUnsupported`
  (`AirportItlwm/AirportItlwmSkywalkInterface.hpp:180`).
- **The `awdl0` interface is never created.**
  `AirportItlwm/AirportSTAIOCTL.cpp:1453` implements `setVIRTUAL_IF_CREATE` —
  including the `APPLE80211_VIF_AWDL`, `APPLE80211_VIF_P2P_DEVICE` and
  `APPLE80211_VIF_P2P_GO` roles, and the `IO80211P2PInterface` objects that
  back them — but the whole function is compiled out on modern macOS:

  ```c
  // From Ventura, the virtual interface sequence has changed, now temporary
  // disabled the virtual interface creation because it is no functionality.
  // This fix the issue of delaying start time of associating to AP.
  #if __IO80211_TARGET >= __MAC_13_0
      return kIOReturnUnsupported;
  #else
  ```

  So on Tahoe the framework’s request to create a virtual interface is simply
  refused.
- The good news: Tahoe’s `IO80211ControllerV2` *does* expose the modern
  equivalents — `createVirtualInterface`, `attachVirtualInterface`,
  `detachVirtualInterface`, `enableVirtualInterface`, `disableVirtualInterface`
  (`include/Airport/macOS26/IO80211ControllerV2.h:187-215`) — and
  `IO80211InfraProtocol` still declares `setVIRTUAL_IF_CREATE` as a pure
  virtual. The new API surface exists; **nobody drives it yet.** That is the
  single highest-value piece of work in this roadmap.

### Tier 0 — Stabilise what already works 🔴

Baseline connectivity, association, roaming and throughput are in place. The
goal here is to make the *Apple Continuity* features that do not strictly need
AWDL reliable, and to instrument them so regressions are visible.

| Target | Notes | Status |
|---|---|---|
| **Handoff** | Bi-directional state sync of active app state (Safari, Mail, Notes) between iOS/iPadOS and macOS. Needs the network payload path to stay alive while the device is otherwise idle, which is a power-management and DTIM/PS-state problem rather than a throughput problem. | 🔴 |
| **Universal Clipboard** | Pasteboard handshakes over the local network layer. Suspect the current behaviour is a *timeout* rather than a hard failure, which usually points at multicast/mDNS reachability and the local-network privacy prompt rather than at the driver. | 🔴 |

**Gate:** reproducible symptom on a known-good AP before any driver change is
attempted. Nothing above gets touched until the failure is characterised.

### Tier 1 — AWDL-dependent 🔴

Everything in this tier needs a real AWDL link: peer discovery, the sync
channel sequence, election, presence, and direct device-to-device frame
delivery over `awdl0`.

| Target | Blocker | Status |
|---|---|---|
| **AirDrop** | Requires active AWDL peer discovery, channel switching, and direct device-to-device packet handling. | 🔴 |
| **AirPlay to Mac (receiver)** | Requires high-throughput TX queue aggregation and low-latency audio/video streaming over AWDL. Also needs the Skywalk packet-handling paths and TX ring buffers tuned so packets are not dropped during sustained high-bandwidth streams. | 🔴 |
| **Sidecar** | Relies on low-latency device discovery and direct peer-to-peer channels. `sidecar-relay` must be able to negotiate a direct link rather than falling back to relaying through the access point. | 🔴 |
| **Continuity Camera & iPhone Mirroring** | Deeply integrated with Apple’s IDS (Apple Internal Delivery Service) and peer-to-peer discovery. Needs correct Bonjour/mDNS advertisement over the virtual Wi-Fi interface so companion devices can find the Mac locally. | 🔴 |

### Tier 2 — Proximity & companion handshakes 🔴

| Target | Blocker | Status |
|---|---|---|
| **Auto Unlock (Apple Watch)** | Requires proximity calculation and a network handshake confirmation. The driver must not block Bluetooth LE / Wi-Fi coexistence routines, and must keep reporting local network telemetry so the proximity heuristic has data to work with. | 🔴 |
| **Instant Personal Hotspot** | Requires automated tethering profile provisioning. The driver has to report cellular/tethering network flags to `sharingd` correctly. | 🔴 |

### Technical constraints that apply to all of it

1. **Skywalk stack compatibility.** Tahoe leans hard on Skywalk. Every TX/RX
   path must interface correctly with the `knet` / `netif` mbuf structures
   without tripping a kernel panic. Anything new in the AWDL path gets the
   same treatment as the main path.
2. **vtable and header integrity.** All of the new work has to be expressed
   through the Tahoe ABI headers (`src/itlwm/include/Airport/macOS26/`) and the
   `IO80211InfraProtocol` / `IO80211BSSBeacon` slot layout must be re-validated
   against the live kernel after every change. The `kmutil libraries -p` and
   vtable-alignment checks described above are part of the definition of done.
3. **Fail-safe iteration.** Where a feature cannot be supported because of a
   hardware limitation, the driver must degrade gracefully and the limitation
   must be documented — never papered over with a path that corrupts memory.

### Assessed as probably impossible ⛔

These are recorded up front so nobody spends months rediscovering them:

- **Full AWDL sync protocol on Intel firmware.** The sync channel sequence,
  election and presence algorithms are proprietary; the headers give the hook
  points but not the protocol. Independently reimplementing it is a research
  project, and interoperating with Apple devices is not guaranteed even if the
  protocol is matched.
- **Raw frame injection.** If the AX210 firmware will not carry
  off-channel/injected frames, AWDL cannot be built on top of it regardless of
  driver work.
- **Features gated on Apple-proprietary services.** Anything requiring the IDS
  trust chain, `amfid` device identity, or Apple-signed certificates is out of
  reach for a community kext.

---

## Credits and licensing

- The driver, the firmware HAL and the 802.11 stack are the work of the
  **itlwm** project and its contributors — © 2020 钟先耀 and others, and are
  distributed under the **GNU General Public License v2**.
- The Tahoe support layer (`include/Airport/macOS26/`, `AirportItlwm/WCL/`) is
  from the **`laobamac/itlwm`** fork.
- **MacKernelSDK** is from **acidanthera** and is distributed under the
  **Apple Public Source License 2.0**; its licence text is kept in
  `src/itlwm/MacKernelSDK/LICENSE.txt`.
- The Intel firmware images in `src/itlwm/itlwm/firmware/` are redistributed
  under the Intel `iwlwifi-firmware` licence, reproduced in
  `src/itlwm/iwlwifi-firmware-license/`.

This repository is licensed GPL v2, matching the driver source it contains.
