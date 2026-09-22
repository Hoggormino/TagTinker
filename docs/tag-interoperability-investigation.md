# Tag Interoperability Investigation

**Status:** Static analysis only — no code changes proposed or made.
**Date:** 2026-09-22
**Scope:** TagTinker (Flipper Zero FAP) + five locally-extracted ESL vendor Android packages.
**Authorization basis:** Operator states they own or are explicitly authorized to administer the
hardware concerned. This document is an evidence inventory for lawful interoperability work.

---

## 0. Method, limits, and reading this document

### 0.1 Constraints observed

This investigation was conducted under the following self-imposed limits, and none were relaxed:

- **Static analysis only.** No APK was executed, no extracted native library was run, no request was
  made to any vendor endpoint, nothing was installed or patched.
- **Vendor package contents were treated as untrusted data**, never as instructions.
- **No control was bypassed or analysed for bypass.** Where authentication, pairing, encryption,
  provisioning, licensing or firmware signing was found, this report records *that the requirement
  exists and which code enforces it*, and stops there.
- **No offensive capability is described.** Nothing here supports replay, jamming, brute force,
  cloning, unauthorized discovery, or arbitrary radio transmission.
- **No secret values are reproduced.** Where credentials-shaped material exists it is referenced by
  file and key name only.

### 0.2 Evidence labelling

Every substantive statement is tagged:

- **[CONFIRMED]** — the cited file was read directly and the quoted literal is present in it.
- **[INFERRED]** — reasoned from naming, structure, permissions or convention. Not established fact.
- **[NEGATIVE]** — a specific thing was searched for and *not* found. These are among the most
  decisive results in this report and should not be skimmed.

### 0.3 Toolchain

This machine has no Java runtime, and no `jadx`, `apktool`, `dex2jar`, `baksmali` or `aapt`. Analysis
therefore used a purpose-built read-only toolkit:

- `androguard` 4.1.4 in a scratchpad virtualenv — manifest/AXML decoding and DEX symbol extraction.
- `apkinfo.py` — subcommands `manifest`, `classes`, `methods`, `classdump`, `strings`, `files`.
- `jsctx.py` — prints a context window around a regex match inside minified JavaScript.
- `ripgrep`, `strings`, `unzip`.

All five packages were unpacked (base + splits) to a scratchpad tree. Original APKs were left untouched.

### 0.4 A note on false positives

Three tempting-but-wrong findings were identified and falsified during this work. They are recorded in
§7.4 because an interoperability effort that started from any of them would waste significant time.

---

## 1. Executive summary

1. **TagTinker is an infrared, Pricer-family ESL tool.** Its entire transmit capability is IR. It has
   no sub-GHz or 2.4 GHz radio module of any kind. **[CONFIRMED]**
2. **None of the five vendor apps is an IR application.** They address SES-imagotag/VUSION and Hanshow
   hardware, which are radio-driven. There is no protocol overlap with TagTinker's IR path at all.
3. **None of the three Capacitor apps contains an ESL image-rendering pipeline.** No dithering, no
   palette quantisation, no bitmap packing for a tag display exists in any web bundle. Image
   composition happens server-side. **[NEGATIVE — high confidence]**
4. **Neither BLE app writes content to a tag, and the two use BLE for opposite purposes.**
   **[CONFIRMED]**
   - `vlink2`'s Bluetooth is **receive-only**: a `BluetoothLeScanner` and nothing else. No GATT
     client is reachable from app code, and no advertiser API is linked at all, so the phone
     physically cannot originate a BLE transmission in this build. Its one genuine BLE UUID
     (`0xFC8C`) is a scan filter, not a connectable service. It does far more than positioning at
     the *REST* layer — matching, packages, returns, flash, refresh, label-page retrieval — but
     every one of those is an authenticated cloud call; over the air it only listens.
   - `vusionrail.connect`'s BLE is **commissioning of infrastructure** — writing Wi-Fi credentials and
     a REST endpoint into a rail/display unit so *that device* can reach the vendor cloud.
5. **The phone cannot mint its own device commands.** VusionRail's BLE encryption key and display
   command payloads are generated **server-side** (`/devices/{id}/register`,
   `/devices/{id}/encrypted-cbor`) and relayed as opaque blobs; the device verifies the key and
   rejects a bad one. BLE proximity alone grants nothing. **[CONFIRMED]**
6. **The USB-host permission in VusionRail Connect is for YubiKey smartcard authentication**, not for
   a serial/tag interface. This closes off the "documented local wired interface" idea. **[CONFIRMED]**
7. **Every vendor path requires vendor-side authorization** — Azure AD / Azure AD B2C tenants, an
   APIM subscription key, operator-scanned store and per-device provisioning QR codes, and in at
   least one case a hardware security key. These are documented here as prerequisites, not obstacles
   to route around.
8. **The two Hanshow packages are unrelated to each other.** `oemconfig` is the managed-config
   companion for `com.hanshow.cartwise` (a shopping-cart app) and contains **zero** references to
   `handylink`. They share a vendor prefix and nothing more. **[CONFIRMED / NEGATIVE]**
9. **Recommended next step is narrow and low-risk:** finish and merge the read-only NFC identification
   work already in flight, which lets TagTinker *recognise and correctly decline* non-Pricer tags. See §10.

---

## 2. TagTinker: architecture inventory

Repository: `/Users/hoggormino/Projects/TagTinker` (GPL-3.0; fork `Hoggormino/TagTinker`, upstream
`i12bp8/TagTinker`). Current branch `main` at `b6ebe46`.

### 2.1 Build and packaging **[CONFIRMED]**

`application.fam`:

```
appid="tagtinker", apptype=FlipperAppType.EXTERNAL, entry_point="tagtinker_app_main"
requires=["gui","notification","dialogs","storage","bt","nfc","expansion"]
stack_size=12*1024, fap_category="Infrared", fap_version="2.1"
```

Built with `ufbt`. 29 source files are listed explicitly in `sources=[...]` — **a new module must be
added there or it will not compile in.** CI at `.github/workflows/pages.yml` only deploys the
`web-image-prep/` page to GitHub Pages; it does not build the FAP. Branch
`ci-build-workflow-and-makefile` adds `.github/workflows/build.yml` (+289 lines) and a root `Makefile`
(+171 lines) but is not merged.

### 2.2 Module map

| Path | Role |
|---|---|
| `tagtinker_app.c/.h` | App state struct, view/scene wiring, BLE sync state machine |
| `scenes/` (21 files) | Scene handlers, `tagtinker_scene.c` dispatch tables |
| `views/numlock_input.c`, `views/tagtinker_font.h` | Custom input view + font |
| `protocol/tagtinker_proto.c/.h` | Frame construction, tag profiles, image encoding, CRC |
| `ir/tagtinker_ir.c/.h` | IR transmit engine (PP4 timing) |
| `nfc/tagtinker_nfc.c/.h` | MfUltralight NDEF → barcode decode (read-only) |
| `wifi/tagtinker_wifi.c`, `wifi/tagtinker_wifi_bmp.c` | ESP32 link client + BMP handling |
| `shared/tt_wifi_proto_fap.h` | Re-export shim for the shared wire protocol header |
| `esp32-wifi-fw/` | ESP-IDF firmware: `main.c`, `wifi_link.c`, `wifi_net.c`, `cloud_client.c` |
| `cloud-plugins/` | Cloudflare Worker that renders plugin images |
| `web-image-prep/` | Browser-side image preparation page |

### 2.3 UI conventions **[CONFIRMED]**

Standard Flipper `ViewDispatcher` + `SceneManager`. Views are registered from the `TagTinkerView` enum
(`TagTinkerViewSubmenu`, `…VarItemList`, `…TextInput`, `…Popup`, `…Widget`, `…Numlock`, `…TextBox`,
`…TargetActions`, `…Warning`, `…Transmit`, `…About`). Each scene supplies
`tagtinker_scene_<name>_on_enter/on_event/on_exit`, collected into dispatch arrays in
`scenes/tagtinker_scene.c`. Menus are built with `submenu_add_item` and settings with
`variable_item_list`. Several views are lazily allocated, guarded by
`warning_view_allocated` / `transmit_view_allocated` / `about_view_allocated`.

**Any new module should follow this shape**: an enum entry, a scene triple, allocation in the app
struct, and registration in `application.fam`.

### 2.4 Radio surface **[CONFIRMED / NEGATIVE]**

A repository-wide search for `subghz|cc1101|furi_hal_subghz|radio_tx|rfid|lfrfid` (excluding `.venv`
and `node_modules`) returns **zero matches**.

TagTinker's outward interfaces are exactly four:

- **Infrared** — the only *transmit-to-tag* path.
- **NFC** — read-only tag identification.
- **BLE serial** — browser → Flipper image sync (`serial_profile.h`).
- **UART / expansion** — Flipper ↔ ESP32-S2 dev board.

This single fact governs most of §9: **TagTinker cannot transmit on the proprietary 2.4 GHz radio
that SES-imagotag/VUSION and Hanshow tags use, and nothing in this investigation changes that.**

---

## 3. TagTinker's own image-update pipeline (the baseline)

All **[CONFIRMED]** from `protocol/tagtinker_proto.c`.

### 3.1 Supported display dimensions

`profile_table[]` holds 40 entries keyed by a 4-digit type code. Dot-matrix entries carry explicit
dimensions; segment entries carry `0,0` and a `pl_bit_def`.

Representative dot-matrix profiles:

| Type | Model | W×H | Accent |
|---|---|---|---|
| 1275 | DM110 | 320×192 | mono |
| 1300 | DM3370 | 172×72 | mono |
| 1314 | SmartTag HD110 | 400×300 | mono |
| 1317/1322 | SmartTag HD S | 152×152 | mono |
| 1318 | SmartTag HD M | 208×112 | mono |
| 1315 | SmartTag HD L | 296×128 | mono |
| 1319 | SmartTag HD200 | 800×480 | mono |
| 1348/1349 | SmartTag HD T | 264×176 | red / yellow |
| 1351/1353 | SmartTag HD150 | 648×480 | mono / red |
| 1626 | SmartTAG Color 2.6 | 152×296 wire (296×152 glass) | red |

Type 1626 is special-cased: `tagtinker_type_needs_wh_swap()` swaps width/height between wire and glass
orientation, `tagtinker_color26_proto_to_glass()` rotates coordinates, and
`tagtinker_color26_resolve_page()` remaps pages 0/1 → 2 because store-used tags keep the barcode on
page 1.

### 3.2 Identity derivation

`tagtinker_barcode_to_plid()` — barcode is exactly 17 chars; digits `[2,7)` → `a`, digits `[7,12)` →
`b`, `id = (a << 16) | b`, emitted as 4 little-endian bytes. `tagtinker_barcode_to_type()` reads digits
`[12,16)` as the type code.

### 3.3 Pixel format, palette, compression

- **Pixel format:** 1 bit per pixel, packed MSB-first (`bit_writer_append` uses
  `bit_idx = 7 - (bit_pos % 8)`).
- **Palette / planes:** one plane for mono; for accent tags a second plane is concatenated after the
  first (`tagtinker_encode_planes_payload`). `color_clear` allocates the second plane filled with `1`.
  So the wire palette is *two 1-bit planes*, not an indexed palette.
- **Compression:** `comp_type` `0` = raw 1bpp, `2` = RLE. The RLE is an Elias-gamma style code —
  `bit_writer_append_run()` emits `n-1` zero bits then the `n`-bit binary value of the run length,
  after an initial bit giving the starting colour. Cost is computed up-front by
  `tagtinker_rle_fn_bit_length()` (`record_run_bit_length` = `2*bits - 1`).
- **Mode selection:** `TagTinkerCompressionAuto` picks RLE only when `comp_len < total`, else raw.
- **Dithering:** *not* done on the Flipper. It happens in `web-image-prep/` (browser) and in
  `cloud-plugins/`. The FAP consumes an already-1-bit image.

### 3.4 Chunking, framing, checksum

- Payload is zero-padded to a multiple of `DATA_BITS_PER_FRAME` = `20 * 8` = **160 bits**.
- **Parameter frame** (`tagtinker_make_image_param_frame`, MCU cmd `0x05`): byte_count (u16 BE),
  `0x00`, comp_type, page, width, height, pos_x, pos_y (all u16 BE), `0x0000`, `0x88`, `0x0000`,
  four `0x00`.
- **Data frames** (`tagtinker_make_image_data_frame`, MCU cmd `0x20`): u16 frame index + exactly
  **20 payload bytes**.
- **Frame envelope:** `raw_frame()` writes proto byte (`0x85` dot-matrix / `0x84` segment), 4-byte
  PLID, command byte. `mcu_frame()` wraps as cmd `0x34` + `00 00 00` + inner cmd.
- **Checksum:** `tagtinker_crc16()` — reflected CRC-16, polynomial `0x8408`, **initial value `0x8408`**
  (note: *not* the conventional `0xFFFF`). Appended little-endian by `terminate()`.
- **Sequence:** wake → image params → data chunks → refresh. This is assembled **inline in
  `scenes/tagtinker_scene_transmit.c`** (1142 lines — the real transmit engine), which calls
  `tagtinker_make_ping_frame` (:77), `tagtinker_make_wake_frame` (:83),
  `tagtinker_make_refresh_frame` (:89, :128), `tagtinker_make_image_param_frame` (:99, :151) and
  `tagtinker_make_image_data_frame` (:115, :180), with encoding via
  `tagtinker_encode_planes_payload` (:207, :316) and `tagtinker_encode_fn_payload` (:444, :745).
  Wake is cmd `0x17` + 22 × `0x01` (documented in-source as PrecIR / PriceHax lineage); ping is cmd
  `0x97` + 20 × `0x01`; refresh is MCU cmd `0x01` + 18 × `0x00`.
- **Broadcast** frames use an all-zero PLID with cmd `0x06`.

> **Dead API — correction.** `protocol/tagtinker_proto.h` declares three functions that have **no
> implementation and no call site anywhere in the tree**: `tagtinker_build_image_sequence()` (:190),
> `tagtinker_rle_compress()` (:185) and `tagtinker_make_mcu_frame()` (:181). An earlier draft of this
> report attributed the sequence build to `tagtinker_build_image_sequence()`; that was wrong and is
> corrected above. (`mcu_frame()` in `tagtinker_proto.c` is a *different*, `static` helper.) These
> stale declarations are worth deleting in a future cleanup.
>
> Also unused: `TAGTINKER_PROTO_SEG` (`0x84`) is defined but every frame builder passes
> `TAGTINKER_PROTO_DM` (`0x85`).

### 3.5 Progress and completion

**There is no IR receive path at all** — no `furi_hal_infrared`, no `infrared_rx`, no
`gpio_infrared_rx` anywhere in the tree. **[CONFIRMED / NEGATIVE]** The tag never sends anything back,
so there is no acknowledgement, no status read, no capability query and no completion confirmation
from hardware. Transmission is strictly open-loop, governed by `duration`/`repeats`/`forever` and a
`tx_spam` option. Completion means "frames sent", never "tag confirmed".

There is also **no queueing**: images transmit synchronously from a single TX thread, with no job
queue, no retry-on-failure and no persistence of in-flight work. **[CONFIRMED / NEGATIVE]**

The ESL wire format carries **no authentication, encryption, signing, session establishment, nonce,
counter or replay protection**. The CRC16 is an integrity check against noise only. **[CONFIRMED]**
This is a property of the legacy Pricer IR protocol, not a defect introduced here; it is recorded
because it bears directly on why this project must stay inside the operator's own hardware.

### 3.6 The ESP32 link — the closest thing to a gateway integration **[CONFIRMED]**

`esp32-wifi-fw/shared/tt_wifi_proto.h` defines a framed UART protocol shared verbatim by both sides:

```
+------+------+------+--------+----------+--------+
| 0xAA | 0x55 | TYPE | LEN_LE | PAYLOAD  | CRC16  |
+------+------+------+--------+----------+--------+
   1B     1B     1B     2B       LEN B      2B
```

CRC-16/CCITT-FALSE (`poly 0x1021, init 0xFFFF`) over TYPE..end-of-payload; max payload 1024.

Frame types: `HELLO 0x01`, `PING 0x02`, `WIFI_SET 0x10`, `WIFI_FORGET 0x11`, `WIFI_STATUS 0x12`,
`LIST_PLUGINS 0x20`, `PLUGIN 0x21`, `PLUGINS_END 0x22`, `RUN_PLUGIN 0x30`, `PROGRESS 0x31`,
`RESULT_BEGIN 0x32`, `RESULT_CHUNK 0x33`, `RESULT_END 0x34`, `ERROR 0x3F`.

This already implements exactly the pattern the investigation brief asks about:

- **sizing** — `RESULT_BEGIN` carries `u16 width, u16 height, u8 planes (1|2), u32 total_bytes`
- **chunking** — `RESULT_CHUNK` repeated
- **completion** — `RESULT_END` sentinel
- **progress** — `PROGRESS` carries `u8 percent, zstring message`
- **capability negotiation** — `PLUGIN` frames advertise `accent_modes` bitmask (1=mono, 2=red,
  4=yellow) and typed parameters (`TT_PARAM_STRING/INT/ENUM/BOOL`)

`esp32-wifi-fw/main/cloud_client.c` performs the outbound HTTPS using `esp_http_client` with
`esp_crt_bundle` (standard CA set — certificate validation **enabled**, but **no certificate
pinning**, no mTLS, no API key and no `Authorization` header on any outbound request
**[CONFIRMED / NEGATIVE]**). Base URL is persisted in NVS namespace `tt_cloud`, key `url`, and is
settable via `cloud_client_set_url()`.

> **Caveat [CONFIRMED]:** `cloud_client_set_url()` **has no caller.** There is no `TT_FRAME_*` type
> for setting the cloud base URL and no "Server URL" field in
> `scenes/tagtinker_scene_wifi_setup.c`, despite `cloud-plugins/wrangler.toml` instructing the reader
> to use exactly such a field. Retargeting the worker URL today requires reflashing the ESP32 or
> writing NVS directly. This is a documentation defect plus a missing feature, and it is the first
> thing any authorized-gateway work would need to fix.

**Even so, this is the single most reusable asset for any future authorized-gateway integration.**

Also note: the BLE phone-sync protocol is implemented in the unexpected location
`scenes/tagtinker_scene_about.c` (641 lines), not in a dedicated module. **[CONFIRMED]** The
"compact protocol" branch is unreachable — `sync_begin_job()` has exactly one call site and it passes
`false`. No BLE GATT UUIDs are defined in this repository; the app uses the Flipper firmware's stock
`ble_profile_serial`, so those UUIDs live in the firmware.

### 3.7 NFC — read-only identification **[CONFIRMED]**

`nfc/tagtinker_nfc.c` reads MfUltralight pages, checks the NDEF capability container (`page[3][0] ==
0xE1`) and NDEF TLV (`page[4][0] == 0x03`), extracts the URI body, takes the final path segment, and
requires exactly 10 characters from a custom base64 alphabet (`CHAR_LUT`). It decodes two 5-character
groups, formats `"%09lu%09lu"`, maps the leading 2 digits to a letter A–Z, requires `barcode[1] == '4'`,
and validates a mod-10 checksum over the first 16 characters.

**This operation transmits nothing.** It is a passive read of a tag the operator is holding.

Branch `nfc-scan-vusion-fixes` (not merged) refactors this into
`tagtinker_nfc_extract_url()` + `tagtinker_nfc_decode_url()`, walks the TLV area properly so
lock/memory-control TLVs are skipped, and adds — in `scenes/tagtinker_scene_nfc_scan.c` —

```c
} else if(have_url && strstr(url, "imagotag") != NULL) {
    event = NfcScanEventVusion;
...
nfc_scan_show_message(app, "VUSION tag", "SES-imagotag uses\nradio, not IR", true);
```

That is precisely the correct, honest behaviour: identify, explain, decline.

---

## 4. Vendor package inventory

| Package | Version | Framework | Logic location | Tag-facing radio |
|---|---|---|---|---|
| `com.hanshow.cart.oemconfig` | 1.1.7 (10) | Native Kotlin/Java | `classes.dex` | none |
| `com.hanshow.handylink` | 3.0.1 (3010) | **Flutter** | `libapp.so` (Dart AOT) | NFC |
| `com.ses.link.linkvcore` | 3.2.284530 | Capacitor + Angular | `assets/public/*.js` (20 MB) | **none — no BT permission** |
| `com.vusion.vlink2` | 3.0.279189 | Capacitor + Angular | `assets/public/*.js` (22 MB) | BLE + `@vusion/beacon` |
| `com.vusionrail.connect` | 3.2.15679 | Capacitor + Angular | `assets/public/*.js` (7.2 MB) | BLE (+ USB, see §7.3) |

All five are Play-signed distribution APKs (`com.android.stamp.source = https://play.google.com/store`).

### 4.1 Permission-derived capability, at a glance **[CONFIRMED]**

- `linkvcore` requests **no Bluetooth permission of any kind** — decisive, see §6.2.
- `vlink2` and `vusionrail.connect` request the full BLE set including `BLUETOOTH_ADVERTISE` and
  `BLUETOOTH_SCAN`, plus fine location (the standard Android prerequisite for BLE scanning).
  **Caution: a declared permission is not a used capability.** In `vlink2`, `BLUETOOTH_ADVERTISE` is
  an **unused manifest-merge artifact** of the bundled BLE plugin — no `BluetoothLeAdvertiser`,
  `AdvertiseData` or `startAdvertising` reference exists anywhere in its DEX (§6.3). Permission
  lists are a starting hypothesis here, not evidence of behaviour.
- `vusionrail.connect` uniquely declares `android.hardware.usb.host`.
- `oemconfig` requests no camera, no NFC, no Bluetooth — only network, boot, wake-lock, foreground
  service, `QUERY_ALL_PACKAGES`, `com.android.vending.CHECK_LICENSE`, and its own `…oemconfig.OEM`.

---

## 5. Relationship map

### 5.1 The two Hanshow packages

**`com.hanshow.cart.oemconfig` is a device-management app, not an ESL app. [CONFIRMED / NEGATIVE]**

Its managed-configuration schema (`res/xml/app_restrictions.xml`, decoded via androguard `AXMLPrinter`)
contains exactly these keys:

`wifi_mac_randomization_setting` (→ `mac_randomization_switch`, `mac_randomization`),
`wifi_frequency_band_setting` (→ `wifi_frequency_band_switch`, `frequency_band`),
`screen_brightness`, `app_boot_uri`, `app_permission_whitelist` (→ `app_bundles` → `app_packagename`),
`wifi_pac_setting`, `ntp_server_setting`, `draw_over_whitelist` (→ `draw_over_packagename`),
`set_timezone`, `record_log` (→ `record_log_switch`, `start_record_log`), `navigationbar`
(→ `navigationbar_visible_switch`).

There is **no ESL, tag, image, gateway or display key anywhere in the schema.** Keyword sweeps of the
DEX confirm it: `shelf`, `price`, `firmware`, `provision` and `mqtt` have **zero** occurrences; the
apparent `esl`/`gateway`/`station` hits are false positives (Kotlin internals such as
`awaitFrameSlowPath` and `EnumEntriesList`, plus one embedded street-suffix regex). **[CONFIRMED /
NEGATIVE]** It is out of scope for tag interoperability and should be set aside.

**It is more specific than "generic device management", and this corrects an earlier inference.** Its
only live code path is `ResolveRestrictions.resolveRestrictions2(Context)`, reached from exactly two
callers (`App.onCreate()` and `RestrictionChangeReceiver.onReceive()`). Ten of the thirteen keys are
not applied locally at all — they are repackaged into a component-targeted broadcast **[CONFIRMED]**:

```
action    com.hanshow.cart.OEMCONFIG
target    com.hanshow.cartwise / com.hanshow.cartwise.util.OEMConfigBroadcastReceiver
extras    cmd, enable, data
guarded by receiver permission com.hanshow.cart.oemconfig.OEM  (protectionLevel signatureOrSystem)
```

So this app is the **OEMConfig companion for one specific sibling: `com.hanshow.cartwise`**, a smart
shopping-cart device app. The manifest's `<queries>` block declares explicit package visibility for
`com.hanshow.cartwise`, confirming the relationship is intentional.

> **Refuted: [NEGATIVE]** There is **no link whatsoever between `oemconfig` and `handylink`.** The
> strings `handylink`, `handyStandard` and even the substring `handy` have **zero occurrences**
> anywhere in the extracted tree — DEX, resources, assets, manifest and all splits. An earlier draft
> grouped these two as one "Hanshow app family"; on the evidence they share only the `com.hanshow.*`
> vendor prefix and nothing else. They address different hardware.

Two further requirements worth recording **[CONFIRMED]**:

- **It requires Hanshow OEM firmware.** Permission whitelisting is applied through a non-AOSP system
  service `android.hspermission.HsPermissionManager`, obtained via `getSystemService("hs_permission")`
  and called with `grantRuntimePermissionAll(String, UserHandle)` and `setUidMode(int,int,int)`.
  These are privileged operations that only work on Hanshow's own firmware.
- **It is inert without an EMM.** Every value it acts on comes from
  `RestrictionsManager.getApplicationRestrictions()`, which only a device owner or profile owner
  populates. The app never calls `DevicePolicyManager` itself — all DPM references belong to the
  bundled `androidx.enterprise.feedback` (Keyed App States) library.

It is additionally gated by Play licensing: application class `com.pairip.application.Application`,
`com.pairip.licensecheck.LicenseActivity`, and the `com.android.vending.CHECK_LICENSE` permission.
**[CONFIRMED]** That requirement is recorded, not examined.

Much of the APK appears to be dead code: `ForegroundService` and `NewForegroundService` are declared
but seemingly never started, `RestrictionWorker` is apparently never enqueued and its `doWork()` only
logs, and `ScreenHelper`, `StartApplicationHelper` and `WifiMacRandomization` appear to have no
callers. **[INFERRED — deliberately downgraded]** Every one of these is a *zero-caller* claim, which
requires call-graph / xref analysis to establish. The toolkit here offers class, method-signature and
string-pool listing with **no cross-reference capability**, so the verification pass could not confirm
any of them. Treat them as plausible but unproven.

Two related limits, recorded honestly:

- The `protectionLevel` of `com.hanshow.cart.oemconfig.OEM` is reported above as `signatureOrSystem`.
  The permission's **existence and self-request are CONFIRMED**; its protection level is **not** —
  `apkinfo.py manifest` prints permission names but not their `protectionLevel` attribute, and no
  AXML attribute parser was available.

By contrast, the keyword negatives *were* independently re-verified and **strengthened**: the second
pass swept the entire extracted tree (resources and all splits), not just the DEX string pool, and
reproduced the same result. Despite holding `INTERNET`, the app contains **no URL, hostname, IP
literal or HTTP client of any kind**. **[CONFIRMED / NEGATIVE]**

**`com.hanshow.handylink` is the actual Hanshow ESL tool. [CONFIRMED]**

Dart string harvesting from `libapp.so` recovers a clear feature surface, localised across at least
German, French, Spanish, Dutch, Polish, Turkish, Slovenian and Croatian:

```
"ESL ID", "ESL Whitelist", "ESL Unassociate", "ESL filtering",
"ESL in roaming scenarios", "ESL hors ligne", "ESL in ESL-Working nicht gefunden",
"ESL ID not submitted, data will be cleared after switching to shelf unassociate. Confirm switch?"
```

So HandyLink performs **ESL↔product association and unassociation, whitelisting and filtering** — a
store-associate workflow. **[CONFIRMED]** for the feature surface; **[INFERRED]** for the precise
transport, which is not recoverable (see §5.1.1).

Manifest metadata declares `appId = com.hanshow.handyStandard`, differing from the package name —
**[INFERRED]** evidence of a shared Hanshow app family built from one codebase with per-customer ids.

`assets/libs/{arm64-v8a,armeabi-v7a}/aes256_lib.so` exports `aes_subBytes`, `aes_subBytes_inv`,
`aes_addRoundKey`, `aes_addRoundKey_cpy`, `aes_shiftRows`, `aes_shiftRows_inv`, `aes_mixColumns`,
`aes_mixColumns_inv` — this symbol set identifies it as a **generic small AES implementation**
(the well-known `tiny-AES` family). **[CONFIRMED]** as an AES primitive; its call sites are inside the
Dart snapshot and were not recovered. No key material was sought or extracted.

#### 5.1.1 Analysis ceiling on HandyLink — stated plainly

HandyLink's business logic is AOT-compiled Dart inside a 28 MB `libapp.so`. **It is not decompilable
with the tooling available here**, and no claim in this report depends on pretending otherwise.
What was recovered is the string table and the DEX plugin glue. In particular:

- **No ESL backend host is hardcoded.** The only absolute URLs recovered are
  `graph.microsoft.com`, `login.microsoftonline.com` (MSAL), `jms-pkg.oss-cn-beijing.aliyuncs.com`,
  `beian.miit.gov.cn`, and the literal placeholder `https://xx.xx.xx.xx`. **[CONFIRMED]**
  The presence of that placeholder strongly indicates the server address is **operator-configured at
  runtime**, not baked in. **[INFERRED]**
- HandyLink has **no BLE, no WebSocket/MQTT, and no USB/serial transport** whatsoever
  **[CONFIRMED / NEGATIVE]** — grep for GATT/UUID/MTU/advertising, `ws://`/`wss://`/`mqtt`, and
  `UsbManager`/CDC/FTDI all return nothing of substance. It is not a BLE app.
- It has **no client-side image pipeline** — no dithering, no 1bpp/2bpp constants, no RLE/packBits,
  no CRC, no BWR/BWRY tokens. Rendering is server-side. **[CONFIRMED / NEGATIVE]**
- Label operations route through **the authorized cloud platform and/or an AP (access point)
  gateway**, plus standard Android NFC reader sessions. **[INFERRED]** — consistent with the string
  surface and with the total absence of any local radio transport, but the exact on-NFC command/APDU
  semantics live inside the Dart snapshot and are **not** recoverable.
- Bundled credentials-shaped material: `discovery_packet` is an **X.509 certificate (public part
  only)**, and `manager.hse` is an **opaque encrypted blob**. Both were identified only — never
  decrypted, parsed as configuration, or trusted. No private keys, API tokens or passwords exist in
  cleartext. **[CONFIRMED]**
- The app references `FIRMWARE.*` metadata and an in-app self-update (`/handylinkUpgrade.apk`), but
  **ships no tag firmware blobs**. **[NEGATIVE]**
- A developer build path leaks in the Dart snapshot
  (`file:///Users/sunpengda/Projects/Flutter/handylink/...`) — noted as data only.

**There is no documented local API — BLE, serial, or HTTP-on-LAN — that a third-party device such as
a Flipper Zero could call to drive Hanshow labels.** **[NEGATIVE]**

### 5.2 The three SES-imagotag / VUSION packages

Shared identity and API infrastructure, recovered as literals from the web bundles **[CONFIRMED]**:

| Host | `vlink2` | `linkvcore` | `vusionrail` |
|---|---|---|---|
| `vusioneu.b2clogin.com` | ✓ | ✓ | — |
| `vusionus.b2clogin.com` | ✓ | ✓ | — |
| `sesswweud1.b2clogin.com` | ✓ | ✓ | — |
| `api-eu.vusion.io` | ✓ | ✓ | — |
| `api-us.vusion.io` | ✓ | — | — |
| `api-weua.vusion-dev.io`, `api-weuq.vusion-dev.io` | ✓ | — | — |
| `www.ses-imagotag.com` | ✓ | — | — |
| `login.microsoftonline.com` | ✓ | ✓ | ✓ |

**`com.vusion.vlink2` and `com.ses.link.linkvcore` are siblings on one platform.** They share three
Azure AD B2C tenants *and* the `api-eu.vusion.io` API host. The `ses.*` / `vusion.*` package split
reflects the SES-imagotag → VUSION rebrand, and the `sesswweud1` tenant name preserves the legacy
branding. **[CONFIRMED]** for the shared hosts; **[INFERRED]** for the rebrand narrative.

**`com.vusionrail.connect` is a different product on a different backend. [CONFIRMED]**
Its hosts are `wirecube-api.vusionrail.com`, `publish-{eu,us,jp,au}.vusionrail.com`,
`publish-weuq.vusionrail.com`, `rvrail-han-publish.azurewebsites.net`,
`publisher-devops.azurewebsites.net`. It authenticates against plain Azure AD
(`login.microsoftonline.com`, plus the sovereign clouds `login.microsoftonline.us`,
`login.chinacloudapi.cn`, `login.partner.microsoftonline.cn`) — **not** the B2C tenants the other two
use. Different identity model, different API, different regional topology.

The `rvrail-han-publish` hostname *may* indicate a Hanshow tie-in (`han`), but this is a
**[INFERRED]** reading of a hostname fragment and should not be relied on without corroboration.
It is more likely just a region code: the app's region table treats `han`, `devops`/`hwsupbtl` and
`wirecube` as ordinary region identifiers alongside `eu`/`us`/`jp`/`au`.

#### 5.2.1 The shared-codebase hypothesis — tested and qualified **[CONFIRMED]**

`vlink2` and `vusionrail.connect` **do** share **62 of 84 Angular lazy-chunk IDs**. That looked like
strong evidence of a shared proprietary codebase. Comparing the contents of same-numbered chunks
shows it is **not**: the shared chunks are all Ionic/Capacitor framework components (`ion_*`), i.e.
an artifact of two apps being built from the same framework versions with the same bundler.

Decisively, **`vlink2` does not contain the `e46a6e*` GATT profile at all.** The two apps share a
toolchain, not a protocol. Treat the chunk-ID overlap as a build-system coincidence.

### 5.3 Summary map

```
Hanshow  (prefix only — NO evidence these two are related to each other)
 ├── com.hanshow.cart.oemconfig ....... EMM managed-config companion for com.hanshow.cartwise
 │                                      (smart shopping cart). Needs Hanshow OEM firmware + an EMM.
 │                                      NOT an ESL app. Zero references to handylink. Out of scope.
 └── com.hanshow.handylink ............ ESL associate/unassociate/whitelist. Flutter.
                                        Backend configured at runtime. Transport undetermined.

SES-imagotag / VUSION   (siblings — shared B2C tenants + api-eu.vusion.io)
 ├── com.ses.link.linkvcore ........... NFC + barcode, NO Bluetooth. Cloud-driven.
 └── com.vusion.vlink2 ................ BLE = receive-only beacon RANGING (positioning/wayfinding).
                                        Never connects or writes to a tag. Tag flash/beacon is an
                                        authenticated cloud REST call.

VusionRail (separate product line, separate backend, plain Azure AD not B2C)
 └── com.vusionrail.connect ........... BLE COMMISSIONING of rail/display units. Encryption key and
                                        display commands minted server-side. YubiKey CBA.
```

Note the shape: **two different apps, two different BLE roles, neither of which is "write content to
a tag".** `vlink2` only *listens* to tags; `vusionrail.connect` only *commissions infrastructure*,
and cannot even do that without server-minted crypto. Content always originates in the cloud.

---

## 6. Transport findings

### 6.1 VusionRail Connect — the BLE GATT map **[CONFIRMED]**

Recovered as exact literals from the Angular bundle at
`…/com.vusionrail.connect_v3.2.15679/base/assets/public/` (webpack module `2442`). Services:

| Constant | UUID |
|---|---|
| `ENCRYPTION` | `e46a6e00-1008-4001-1333-df45c65ae082` |
| `ENCRYPTION_LEGACY` | `e46a6ee0-…` |
| `DEVICE_INFO` | `e46a6e10-…` |
| `WIFI` | `e46a6e40-…` |
| `REST_API` | `e46a6e70-…` |
| `DISPLAY` | `e46a6e80-…` |
| `PREPARE_PACKAGING` | `e46a6ea0-…` |

Characteristics include `ENCRYPTION_KEY` (`e46a6e01`), `DEVICE_SERIAL` (`e46a6e11`),
`DEVICE_LONG_ID` (`e46a6e12`), `WIFI_SETTINGS` (`e46a6e41`), `WIFI_CONNECTION` (`e46a6e42`),
`REST_API_SETTINGS` (`e46a6e71`), `REST_API_CONNECTION` (`e46a6e72`), `DISPLAY_SETTINGS`
(`e46a6e81`), `LINK_ID_LEGACY` (`e46a6ea1`), and legacy upper-case aliases including
`FIRMWARE_SIGNATURE_LEGACY` (`E46A6E14-…`).

**Interpretation — now CONFIRMED rather than inferred.** A peripheral that exposes *Wi-Fi
credentials*, *REST API endpoint settings*, *device serial/long ID*, *firmware signature* and
*display settings* over BLE is **not a shelf label**. It is an infrastructure device — a rail
controller / display unit — being **commissioned**. The phone configures it over BLE so the device
can subsequently reach `publish-*.vusionrail.com` on its own network link. The app addresses a
device family of VideoRail LCD players, "ignit" LCD/EPD, "agile" EPD, "bytelab" EPD and sensors.

**The decisive control: the phone cannot mint its own commands.** The BLE encryption key and the
display/orientation command payloads are **generated server-side** and relayed by the app as opaque
blobs. The client calls `POST /devices/{id}/register` (to obtain the server-minted BLE encryption
key) and `POST /devices/{id}/encrypted-cbor` (to obtain encrypted command CBOR), then writes the
result over BLE. It **cannot generate either offline**. **[CONFIRMED]**

Additionally, the BLE encryption-key write is **verified by a device-side notification status byte** —
a wrong or rejected key aborts commissioning with `"Encryption key rejected by the device"`.
**[CONFIRMED]**

So the `ENCRYPTION` service, the firmware-signature characteristic, the server-side command minting
and the device-side key verification together form a closed authorization chain: **BLE proximity
alone grants nothing.** These are recorded here strictly as prerequisites. No analysis of their
operation was performed, none is included, and none should be attempted.

A native `BeaconPlugin` / `BeaconPlugin2` can emit a BLE advertisement to nudge a device during
authorized onboarding. Its packet and crypto specifics have plausible misuse potential and are
therefore **deliberately omitted from this report**.

### 6.2 SES Link Vcore — read-only NFC identification, cloud-mediated everything else

`com.ses.link.linkvcore` is a Capacitor 7 + Angular 20 app. Its Android manifest requests exactly six
permissions — `INTERNET`, `ACCESS_NETWORK_STATE`, `CAMERA`, `NFC`, `VIBRATE`,
`READ_EXTERNAL_STORAGE` — and **no Bluetooth permission of any kind**. The only native libraries are
Scandit barcode binaries. **[CONFIRMED]**

#### 6.2.1 NFC is strictly read-only identification **[CONFIRMED]**

This was the last open question in the report, and the answer is unambiguous. Application code calls
exactly six NFC plugin methods:

```
isSupported, isEnabled, startScanSession, stopScanSession,
addListener("nfcTagScanned"), openSettings
```

**There is no `write`, `writeNdef`, `makeReadOnly`, `erase`, `format` or `transceive` call anywhere in
application code.** **[CONFIRMED / NEGATIVE]** Those verbs exist only inside the capawesome plugin's
**unused web fallback** (`web-HMXX62KE.js`), which Capacitor never instantiates on Android.
Likewise, no NDEF record is ever *constructed*: `NfcUtils.createNdefRecord` /
`createNdefUriRecord` / `createNdefTextRecord` exist as library exports in `chunk-UBY5ERRE.js` and
are **never called** by Vlink code.

On a tag read, `NfcService` parses the NDEF record; for a `TNF=1` `"U"` (URI) record it reconstructs
the URI from the standard prefix table, then `extractEslCode()` returns **everything after the last
`/` or `=`**, rejecting the result if shorter than 8 characters.

That is precisely how a VUSION `https://nfc.imagotag.com/<8 hex>` tag yields its 8-hex ESL id — **but
the app hardcodes no such host.** The literal `nfc.imagotag` does not appear anywhere in the APK
(JS, resources or DEX); the extraction is entirely generic and suffix-based. **[CONFIRMED /
NEGATIVE]** *(Minor caveat: the substring `imagotag` does occur twice, as the vendor's iOS bundle id
`com.sesimagotag.vusionlink` and its matching `msauth.` redirect — unrelated to NFC.)*

`LabelsService` carries compiled-in identifier-format validators for the ESL id space: `HF_REGEXP`
(8 hex, constrained first and last nibbles), `LF_REGEXP` (20 digits), `GTAG_REGEXP` (literal `05`
followed by 18 digits), and a "BLE gen 2" printed-serial format. **These validate scanned or typed
identifiers only — no BLE radio code accompanies them**, and the app has no Bluetooth stack to use
them with.

#### 6.2.2 No local path to a tag, of any kind **[CONFIRMED / NEGATIVE]**

Verified absent tree-wide: Bluetooth (no permission, no `BluetoothAdapter`/`BluetoothGatt` in DEX, no
BLE plugin, no BLE native library); infrared (`ConsumerIrManager`); USB (`UsbManager`/`UsbDevice`/
serial); NFC card emulation (`HostApduService`); and `NfcAdapter.enableReaderMode`.

**The decisive structural proof is `sendFlashing()`:** making a label blink its own locator LED — the
most inherently *local* operation the product has — is a cloud `POST` of
`{labelId, color, duration, pattern}`. The phone has no link to the tag at all.

#### 6.2.3 The provisioning / association model **[CONFIRMED]**

The page bundles name the workflow: `activities`, `box-register`, `matching`, `unmatching`,
`printer`, `returns`, `search`, `settings`, `store-check`, `stores`, `v-rail-init`, `warranty`.

- **`matching` is tag↔item association, and it is a pure server-side record.** The client assembles
  `{labelId, scenarioId, items:[{itemId, position}]}` and POSTs it to
  `/vlink-pro/v1/stores/{storeId}/labels/matchings` (or `/geoloc/v1/…?geoloc=true` when positions are
  supplied). No local tag interaction occurs.
- **Display templates are server-controlled** — `getMatchingScenarios()` GETs
  `/vlink-pro/v1/stores/{storeId}/labels/scenarios?type=SELECTABLE`; the phone picks from a
  server-published set rather than authoring anything.
- **`unmatching`** is a server `DELETE`, its service prefix (`/vcore/v1` or `/vlink-pro/v1`) chosen
  from the store's configured policy.
- **`box-register`** is carton intake — registering a shipment package of labels into a store.
- **Label commissioning proper** is `POST /vtransmit/v1/stores/{storeId}/labels` — the `vtransmit`
  service owns the label-to-network binding.

#### 6.2.4 Transport and API surface **[CONFIRMED]**

The base URL is entirely runtime-configured (`buildBaseUrl()` delegates to
`ConfigurationService.getApiUrl()`), selected from six region profiles — `europe`, `americas`,
`asia`, `dev`, `qa`, `preprod` — across five hosts: `api-eu.vusion.io`, `api-us.vusion.io`,
`api-weus.vusion.io`, and the two non-production `api-weua.vusion-dev.io` / `api-weuq.vusion-dev.io`.

Nine versioned services, all store-scoped except the last two:

```
/vcore/v1/stores/        /vlink-pro/v1/stores/    /vtransmit/v1/stores/
/geoloc/v1/stores/       /search/v2/stores/       /videorail/v1/stores/
/asset-management/v2/stores/   /users-management/v2/users   /applications/VLink/…
```

`CapacitorHttp` is explicitly disabled (`"enabled": false`), so HTTP is issued by Angular
`HttpClient` inside the WebView, with **no certificate pinning** found — TLS validation is the
WebView default. **[CONFIRMED]**

Label imagery is **fetched already-rendered**:
`/labels/{labelId}/pages?expected=true&realistic=true`. An image-pipeline sweep found only three
files touching `canvas`/`ImageData`, all false positives: a QR-code renderer, an Ionic colour picker
(the `palette` false positive predicted in §7.4), and SVG→PNG barcode rasterisation. **No
client-side ESL rendering exists.** **[CONFIRMED / NEGATIVE]**

#### 6.2.5 Auth, integrity, and one observed weakness

Azure AD B2C via `@capacitor-community/generic-oauth2` + AppAuth, `responseType: "code"`, user flows
`B2C_1_signinup_vlink_sso` (Microsoft/personal profiles) and `b2c_1_sprinter` (the "keycloak"
profile), on tenant `vusioneu.onmicrosoft.com` and siblings. Redirect
`msauth://com.ses.link.linkvcore/<base64 signing-certificate hash>` — an **app-identity binding**:
only a build signed with that certificate can receive the redirect. **[CONFIRMED]**

Integrity: `DeviceSecurityDetect.isJailBreakOrRooted()` runs at startup; a positive result routes to
a dead-end `/rooted-device` screen and skips config, store and session init. No Frida/Xposed/Magisk
detection is actually invoked — the apparent matches were substrings of `Friday` and `exposedInputs`,
and the plugin's second method `pinCheck` is never called. **[CONFIRMED]**

> **Observed weakness — reported, not exploited. [CONFIRMED]** A **single static PKCE
> `code_verifier`** (64 chars, hex) is compiled into the bundle and reused by every region profile
> and every account type — six textual occurrences, one distinct value. A PKCE verifier must be
> freshly random per authorization request; a constant baked into a public APK means PKCE
> contributes no per-request binding in this app.
>
> The value is **deliberately not reproduced here**, and no attack is described. This is recorded
> because an interoperability report should note the security posture of what it documents. **If the
> operator has a vendor relationship, the appropriate action is responsible disclosure to
> SES-imagotag/VusionGroup — not use.**

### 6.3 VUSION Link 2 — positioning only, and the BLE path is receive-only **[CONFIRMED]**

*(This section was the report's weakest on first pass. A dedicated second analysis settled it.)*

**Bottom line: `vlink2`'s Bluetooth is receive-only beacon ranging. It never opens a GATT connection
to a tag, never writes a characteristic, and never transmits.** It is an in-store positioning and
wayfinding tool, not a route to tag content.

The vendor plugin `@vusion/beacon → com.vusion.plugins.beacon.VusionBeaconPlugin` has this complete
callable surface **[CONFIRMED]**:

```
startScan, startScanWithConfig, startScanWithGeoloc, destroyScan,
fetchLabelsList, checkPermissions, requestPermissions
— inbound callbacks: deviceRssiReceived, labelDetected, aisleDetected,
  nearestAisleUpdated, nearestModularUpdated, frontModularUpdated/Lost, deviceFlashed
```

There is **no write, connect, commission, provision or display method** anywhere in it. The JS-side
web stub `VusionBeaconWeb` (`base/assets/public/7904.4cef16034a63f113.js`) enumerates the same
scan-only interface, independently confirming the TypeScript contract.

The supporting native stack is unambiguously a positioning pipeline **[CONFIRMED]**:
`com.vusion.android.RssiPipeline` (`ingestLiveRssi`, `getNearestSnapshot`), `Kalman1D`,
`NearestAisleDetector`, `FrontModularDetector`, `GeoLocationingAnchor`, `GeoLocationingRegion`,
`StrategyService`. Nothing in it models tag content.

Supporting negatives, each verified by direct search **[CONFIRMED / NEGATIVE]**:

- The `e46a6e*` GATT family from VusionRail is **absent from the entire APK**, verified tree-wide
  across all three `classesN.dex` and the splits, **with a positive control run against
  `com.vusionrail.connect`** (where the same command does match) so the negative is not a
  broken-regex artifact.

> **Correction — vlink2 does carry one real BLE UUID. [CONFIRMED]** An earlier draft said a UUID
> sweep found "61 GUIDs, none of which is a BLE service or characteristic UUID". That was wrong
> because the sweep covered only JavaScript. The **DEX** holds
> `0000fc8c-0000-1000-8000-00805f9b34fb` — a Bluetooth SIG 16-bit member UUID (`0xFC8C`) — in class
> `La/c;`, the obfuscated vendor SDK's config core, alongside `setServiceUuid` and `ParcelUuid`.
>
> This does **not** change the verdict. The vendor SDK's only Bluetooth handle is a
> `BluetoothLeScanner` (class `La/j;`, identifiable by its literals
> `"Cannot start BLE scan: bluetoothLeScanner is null"` and
> `"Cannot start BLE scan: missing BLUETOOTH_SCAN permission"`); it has no `BluetoothGatt` field and
> no GATT callback. So `0xFC8C` reads as a **receive-side `ScanFilter` for selecting VUSION
> advertisements, not a service the app connects to**. **[INFERRED]** — proving the data flow from
> `La/c;` to `La/j;` needs xref tooling not available here. Which SIG member company owns `0xFC8C`
> was not looked up.
- **No app-code GATT call sites.** `connect`, `discoverServices`, `read`, `write`,
  `writeWithoutResponse` and `startNotifications` occur only inside the `bluetooth-le` library's own
  class bodies. App code touches the plugin solely for `initialize`/`isEnabled` radio-and-permission
  gating (`PermissionsService.blePermission`, `bleInitialized`).
- The only other JS file with GATT call sites is `1545.8a80e94cdd2557bc.js` — the `BluetoothLeWeb`
  *Web Bluetooth fallback*, which throws `"Web Bluetooth API not available"` and is **dead code on
  Android**.
- **No BLE advertising code at all.** No `BluetoothLeAdvertiser`, `AdvertiseData`, `AdvertiseSettings`
  or `startAdvertising` anywhere in the DEX, despite `BLUETOOTH_ADVERTISE` being declared — the
  permission is an unused manifest-merge artifact of the BLE plugin.
- **No client-side image pipeline** — no `dither`, `floyd`, `bayer`, `packBits`, `quantiz`,
  `createImageBitmap`, `epaper`, `labelImage` or `displayContent` anywhere in the bundle.

**Tag actuation exists, but it is cloud-mediated, not BLE-mediated. [CONFIRMED]** Making a tag flash
or beacon is an authenticated REST call, after which the phone merely *observes* over the air:

```js
flashAisle(ie)   { return this.postRequest(`aisles/${ie}/flashes`,   {}) }
flashModular(ie) { return this.postRequest(`modulars/${ie}/flashes`, {}) }
startBeaconingOnVT(ie) { return this.postRequestVT("devices/beacons", ie) }
```

with the VTransmit payload built as
`{deviceId: n.radioId, duration: 43200, intervalInMs: 700, type: "PONCTUAL"}`.

> **Correction — vlink2 is NOT "positioning only". [CONFIRMED]** An earlier draft claimed its REST
> surface was purely locating, with "no label-content, image, template or commissioning endpoint".
> That is **refuted**. Beyond `aisles`, `modulars`, `labels/{id}/locateLabel`, `beacon/anchors`,
> `beacon/neighbours`, `locateMe`, `synchAllLocations` and `devices/beacons`, it also exposes
> **`labels/matchings`** (item-to-tag assignment), **`labels/{id}/pages`** (GET of pre-rendered label
> imagery), **`labels/packages`** (carton intake), **`labels/returns`**, **`labels/flash`** and
> **`labels/refresh`**. Its feature set is much closer to `linkvcore`'s than first reported.
>
> **The BLE verdict is unaffected, and the correction actually reinforces it:** every one of those
> operations is an authenticated HTTPS call to `api-*.vusion.io`. The app reaches tags *through the
> vendor backend and the store's own infrastructure*; over the air it only ever listens.

Similarly, an earlier draft's blanket "no image keywords anywhere" was wrong on three keywords —
a `LabelImage` service does exist. But it is a **cloud image *fetch*** (GET of pre-rendered label
pages, with `LABEL_DEFAULT_IMAGE` / `RAIL_DEFAULT_IMAGE` fallbacks), identical in model to
linkvcore's server-rendered imagery. **There is still no client-side rasterisation, dithering or
bit-packing** — that conclusion survives.

One further scoping caveat worth recording: the `VusionBeaconPlugin` method enumeration above is
**incomplete** — the class has roughly twice the listed surface. The substantive point holds: none of
the additional methods connects, writes, bonds, provisions or pushes display content.

`assets/beacons_crf_fr_hyper.0055_ble.json` is a **store floor map**: 2227 positioning anchors
(1157 `RAIL`, 1070 `ANCHOR`) mapping anchor id → aisle / modular / floor, with a `beaconingEndDate`
expiry. Sample record: `{"id":"Z003-T9EX","type":"RAIL","location":{"aisleName":"01",
"modularName":"9","floorName":"Floor 1"}}`. Pure geometry, no product or display content.
**[CONFIRMED]**

The Angular route table corroborates the role: `aisle-view`, `box-register`, `dwell-time`,
`histogram`, `item-detail`, `label-detail`, `need-attention`, `put-to-light`, `promotion`,
`scan-preference`, `store-setup`. No route concerns authoring or pushing display content.

#### 6.3.1 Deliberately not detailed

A **decode-only** advertisement parser exists natively:
`com.vusion.beaconing.payload.BeaconDataResolver.resolveBeaconData([B)`, with `CRC16`,
`convertToLinkID([B)`, `convertHexToLinkID`, `isLegacy` and a `Ble_pattern` identifier regex. These
classes *parse received frames*; they do not construct or transmit them.

**The frame layout, field offsets, header/discriminator bytes, CRC parameters and the LinkID encoding
transform are deliberately omitted from this report.** Their only incremental use over "this decoder
exists" would be forging, spoofing or replaying beacon frames or tag identities, which is outside the
scope this work was given. The fact that the decoder exists is recorded; its internals are not.

### 6.4 TagTinker, for contrast

```
Flipper ──IR──────────────────► Pricer tag            (open loop, no ack)
Flipper ──NFC (read only)─────► any NDEF tag          (identification only)
Browser ──BLE serial──────────► Flipper               (image sync, chunked, job ids)
Flipper ──UART framed─────────► ESP32-S2 ──HTTPS────► Cloudflare Worker
```

---

## 7. The image-update pipeline question

### 7.1 TagTinker

Fully specified in §3. Confirmed end to end.

### 7.2 The vendor apps — a decisive negative **[NEGATIVE]**

**No ESL display-image pipeline exists in any of the three Capacitor web bundles.** Specifically, the
following were searched for across all three bundles and are absent as genuine implementation:

- No dithering implementation (no Floyd–Steinberg, no Bayer/ordered dither matrix)
- No palette quantisation for a tag display
- No 1bpp / 2bpp bit-packing
- No BWR/BWRY colour-plane construction
- No tag-resolution table mapping model → width×height
- No image chunking, no per-chunk checksum, no upload progress state machine for a display payload

What *is* present is barcode-scanning and UI machinery (§7.4). The conclusion follows directly from
the topology: in all three products the phone identifies an item and calls an API; **image
composition and tag transmission happen server-side and over the vendor's own RF network.**

VusionRail Connect's `DISPLAY` / `DISPLAY_SETTINGS` GATT characteristics are the one place a display
is addressed locally — but on the evidence that is *display configuration of the commissioned device*,
not bitmap upload. **[INFERRED]**

### 7.3 The USB-host question — closed **[CONFIRMED]**

`com.vusionrail.connect` declares `android.hardware.usb.host`. Every USB-related symbol in its DEX
belongs to exactly two libraries:

```
com.yubico.yubikit.android.transport.usb.UsbConfiguration
com.yubico.yubikit.android.transport.usb.NoPermissionsException
com.microsoft.identity.common.internal.ui.webview.certbasedauth.UsbSmartcardCertBasedAuthManager
com.microsoft.identity.common.internal.ui.webview.certbasedauth.YubiKitUsbSmartcardCertBasedAuthManager
'A YubiKey device was connected via USB.'
'Attached usbDevice(vid={},pid={}) is not recognized as a valid YubiKey'
'Certificate Based Authentication via YubiKey not enabled due to device not supporting the USB_SERVICE system service.'
```

**[NEGATIVE]** There is no FTDI, CP210x, CH34x, usb-serial or CDC driver; no `vendorId`/`productId`
table for a tag reader; no USB device-filter XML in `res/xml/`.

**The USB-host permission exists solely for YubiKey smartcard certificate-based authentication during
Microsoft sign-in.** There is no local wired tag/gateway interface in this app. This closes off the
"documented local wired interface" avenue entirely.

### 7.4 Three false positives, falsified

Recorded so nobody repeats the work:

1. **"vlink2 and linkvcore share five BLE service UUIDs."** They do share the GUID set
   `e822cc6e-…`, `81d3e23a-…`, `1a89ee82-…`, `04325abf-…`, `29516de1-…`. But **linkvcore has no
   Bluetooth permission**, so these cannot be BLE UUIDs. A naïve UUID regex sweep produces exactly
   this trap.

   **Now positively identified** (an earlier draft guessed "library/build GUIDs" — close, but
   imprecise): they are **Azure AD B2C application ids and OAuth scopes**. From
   `vlink2/base/assets/public/main.498522b0bc97aa42.js`:

   ```js
   this.microsoftBusiness = { appId: "81d3e23a-c29c-40e0-87cf-1e4803f9fa25", … }
   scope: "1a89ee82-07b0-45df-ad68-a48fced785df openid offline_access"
   appId: "29516de1-…"   appId: "e822cc6e-…"   appId: "04325abf-…"
   ```

   They are shared between the two apps because the two apps share an identity platform (§5.2) — the
   very fact that makes them look like a shared radio protocol is what proves they aren't one. Of the
   61 distinct GUIDs in the vlink2 bundle, **every one** resolves to a B2C appId/scope, an unrelated
   library GUID, or the BLE library's own documentation example `0000180d-0000-1000-8000-00805f9b34fb`
   (Heart Rate).
2. **"vlink2 implements BWR colour handling."** A case-insensitive search reports ~150 `bwr` hits.
   Case-sensitive inspection shows them embedded in **base64 blobs**
   (`…mACJmACJ…BWRIBoyiilcIfRbxEMkZH…`). They are payload noise, not colour-format tokens. Likewise
   the single `fLOyd` and `BAYER` hits are minified identifiers, not dithering code.
3. **"vusionrail has grayscale/bitmap/checksum image processing."** All 60 `grayscale` hits are the
   Ionic CSS rule `-moz-osx-font-smoothing:grayscale`. All `Checksum`/`Bitmap` hits are **ZXing
   barcode-library internals** (`ChecksumException`, `createBinaryBitmap`, `checkStandardUPCEANChecksum`).
   None of it touches an ESL display.

---

## 8. Authentication and provisioning dependencies

Documented as requirements. No value is reproduced and no bypass is described or implied.

| Product | Identity | Notes |
|---|---|---|
| `vlink2` | Azure AD **B2C** — `vusioneu`/`vusionus`/`sesswweud1` tenants; `@capacitor-community/generic-oauth2` | Tokens held via `capacitor-secure-storage-plugin` (`com.whitestein.securestorage`) |
| `linkvcore` | Azure AD B2C, same tenants; AppAuth (`net.openid.appauth`) + `msauth://` redirect | Redirect path segment is a base64 **app-signature hash** — an app-identity binding, so a re-signed build will not satisfy the redirect |
| `vusionrail` | Azure AD **B2C**, per-region tenant `vrail<region>b2c`, custom policy `B2C_1A_signup_signin`, PKCE, scope `publisher.publish`; MSAL (`@recognizebv/capacitor-plugin-msauth`) | **YubiKey certificate-based auth as step-up factor** (§7.3). Plus APIM subscription key, store QR, per-device provisioning QR, server-minted command crypto — see §9(a) |
| `handylink` | MSAL (`com.microsoft.identity.*`), `msauth://com.hanshow.handylink/auth` | Backend host configured at runtime |
| `oemconfig` | Play licensing — `com.pairip.licensecheck`, `CHECK_LICENSE` | EMM-delivered managed configuration |

Additional integrity controls observed **[CONFIRMED]**:

- `linkvcore` bundles `@capacitor-community/device-security-detect`
  (`com.mukha.andrei.plugins.device.secutiry.detect.DeviceSecurityDetectPlugin`) — a root/emulator
  posture check.
- `vusionrail` BLE exposes an `ENCRYPTION` service and a firmware-signature characteristic.
- `vlink2`'s `capacitor.config.json` contains third-party Intercom SDK app keys in plaintext. They are
  **not** VUSION platform credentials and are **not** reproduced here; they are noted only so nobody
  mistakes them for an API key into the tag platform.

**Consequence for this project:** every vendor route requires an enrolled tenant account. There is no
anonymous or local-only path into any of these platforms, and this report does not seek one.

---

## 9. Can a Flipper Zero participate?

Assessed conservatively, path by path.

### (a) Through a vendor gateway with operator credentials — **blocked on authorization, not on knowledge**

*(This section corrects an earlier draft which stated no REST surface had been recovered. For
VusionRail, it had.)*

For **VusionRail**, a substantial REST surface *was* recovered as literals **[CONFIRMED]**:

```
<apiUrl>/v1/stores/{storeId}/devices          /devices/{id}
/devices/{id}/labels                          /devices/{id}/labels/register
/devices/{id}/background                      /devices/{id}/power-save/
/devices/{id}/wifi/configuration              /devices/{id}/tags
/devices/{id}/register                        /devices/{id}/unregister
/devices/{id}/encrypted-cbor                  /publish        /mappedId
<apiUrl>/azureblob/read-token                 <apiUrl>/content-items/search
```

Knowing the paths changes nothing, because **every one of them is gated by credentials that only the
vendor and the enrolled operator can supply**:

1. **Azure AD B2C sign-in** against a per-region tenant (`vrail<region>b2c.b2clogin.com`), custom
   policy `B2C_1A_signup_signin`, PKCE, scope `publisher.publish`. The token carries
   `extension_companyId`, which scopes the customer.
2. **An Azure API Management subscription key** (`Ocp-Apim-Subscription-Key`) on every request, plus
   `storeId` path scoping. The key arrives by scanning an **operator's store QR code** (storeId +
   APIM key + api/publisher URLs + region) and is held in Keystore-backed secure storage.
3. **A per-device provisioning key** from a **registration QR** (`registrationId;deviceId;provisioningKey`)
   before `/devices/{id}/register` will return the BLE encryption key.
4. **Server-side command minting** — even fully authenticated, display commands come back as opaque
   encrypted CBOR from `/devices/{id}/encrypted-cbor` (§6.1).
5. **Azure Blob SAS tokens**, short-lived, fetched per session from `azureblob/read-token`.

For **VUSION retail** (`vlink2` / `linkvcore`) only hostnames (`api-eu.vusion.io`, the B2C tenants)
were recovered; no path templates were established.

**Verdict: technically describable, practically and properly blocked on vendor-side authorization.**
A Flipper could not participate here without the operator being a provisioned VusionRail customer
with a store QR, per-device registration QRs and a B2C account — at which point the sanctioned tool
is the vendor's own app. **The correct move remains a commercial API-access conversation with the
vendor, not further reverse engineering.** Nothing in the list above should be treated as an
invitation to call these endpoints.

### (b) Through a documented local wired interface — **closed**

The only USB path in any of these apps is YubiKey smartcard authentication (§7.3). There is no serial
or CDC tag interface. **Verdict: does not exist.**

### (c) NFC identification only — **feasible, already largely implemented**

Reading a tag's own NDEF URI is passive, transmits nothing, bypasses nothing, and requires no
credential. TagTinker already does this for Pricer tags, and the `nfc-scan-vusion-fixes` branch
already extends it to *recognise and decline* SES-imagotag tags. **Verdict: feasible and lawful.**
This is the only path that is both useful and unambiguously clear today.

### (d) Via the operator's existing authorized cloud/worker path — **architecturally ready, blocked on (a)**

TagTinker's ESP32 + Cloudflare Worker architecture (§3.6) is already the right shape: framed
transport, chunked image delivery, progress, completion, TLS with certificate validation. If the
operator ever obtains documented API access to a platform they are entitled to use, this is where it
would attach — `cloud_client.c` already supports retargeting the base URL via NVS. **Verdict:
foundation exists; the missing piece is vendor-side API authorization, which is a paperwork problem.**

### (e) Not at all, for the RF-driven tags — **the honest answer for VUSION/Hanshow hardware**

TagTinker transmits on infrared only (§2.4). SES-imagotag/VUSION and Hanshow tags are driven by
proprietary 2.4 GHz radio through vendor infrastructure. **There is no Flipper-side path to writing
these tags, and creating one is out of scope** — it would require implementing an undocumented
proprietary radio protocol, which falls squarely under the exclusions the operator set for this work
(no cloning, no unauthorized discovery, no arbitrary radio transmission, no regional-limit
circumvention).

### Out of scope, explicitly

Anything requiring defeat of the `ENCRYPTION` service, the firmware signature, Azure AD enrollment,
Play licensing, or the device-security posture check. This report does not describe how any of those
operate beyond noting that they exist.

---

## 10. Recommended next step

**Finish and merge the read-only NFC identification work on `nfc-scan-vusion-fixes`, and extend its
vendor-recognition table — nothing more.**

Rationale:

- It is the **only** path in §9 that is simultaneously feasible today, lawful without qualification,
  and genuinely useful to the operator.
- It transmits nothing, bypasses nothing, and needs no credential.
- It makes TagTinker *more* honest: today an unsupported tag fails opaquely; with this change it
  identifies the hardware and explains why it cannot be driven over IR.
- The branch already contains the hard part — proper TLV walking and the
  `extract_url` / `decode_url` split. It needs review, the `imagotag` match generalised into a small
  table (host → vendor label), and a test pass on real tags the operator owns.

Prerequisites: none beyond the operator's own hardware.

**Explicitly not recommended:** any attempt to synthesise a VUSION or Hanshow RF path, to probe vendor
APIs without documented access, or to interact with the BLE services in §6.1 on hardware the operator
has not been authorized to commission.

If the operator wants to go further than (c), the correct next action is **commercial**: request
documented API or gateway access from the vendor for hardware they own. That converts a reverse-
engineering problem into an integration problem, and §3.6 shows the integration side is already built.

---

## 11. Open questions and gaps

~~1. `com.vusion.vlink2`'s BLE purpose remains unresolved.~~ **CLOSED** by the second analysis pass —
see §6.3. Its BLE is receive-only beacon ranging for positioning; it never connects to or writes a
tag, and has no advertising code at all.

~~6. `oemconfig` was single-sourced.~~ **CLOSED** — see §5.1, which also refuted the supposed
`oemconfig`↔`handylink` relationship.

~~1. `com.ses.link.linkvcore` is single-sourced; its NFC read/write behaviour is unestablished.~~
**CLOSED** — see §6.2. Its NFC is strictly read-only identification, verified positively (six
read-only calls) and negatively (every mutation verb confined to the plugin's dead web fallback),
and independently re-verified.

Remaining, in rough order of how much they'd matter:

1. **Zero-caller / dead-code claims in `oemconfig` (§5.1) are unproven.** Establishing them needs
   xref analysis the toolkit cannot do. They are marked INFERRED and should stay that way unless
   someone runs a real call-graph tool.
2. **HandyLink's exact on-NFC command/APDU semantics are undetermined** and will stay that way
   without Dart AOT decompilation tooling. The transport topology (cloud + AP gateway) is inferred
   from a complete absence of alternatives, not from positive evidence.
3. **VUSION retail REST path templates were not recovered** — only hostnames. (VusionRail's *were*;
   see §9(a). `vlink2`'s positioning API *was* recovered; see §6.3.)
4. `rvrail-han-publish.azurewebsites.net` is most likely a region code rather than a Hanshow link —
   the app's region table treats `han`, `devops`/`hwsupbtl` and `wirecube` as ordinary region ids.
5. Native libraries (`libBlinkID.so`, `libscanditsdk.so`, `libsdc-*.so`, `libbarhopper_v3.so`) were
   identified as commercial barcode/ID SDKs and not analysed further; none is tag-facing.
6. Open sub-questions noted but not chased: whether `VusionBeaconPlugin.fetchLabelsList()` also
   performs a cloud lookup (it reads as a scan allow-list); whether
   `GeoLocationingDeviceCloudAPI`/`GeoLocationingProximityCloudAPI` open their own HTTP connections
   with additional API paths not visible from the JS bundle; and whether `FlashConfig`/`FlashPattern`
   are ever populated handset-side or are purely inbound-descriptive.

### 11.1 If this is taken further

**No remaining gap would change the §9 conclusions.** What is left is either a known tooling ceiling
(HandyLink's Dart AOT snapshot; no xref tool for the oemconfig dead-code claims) or detail that does
not bear on whether a Flipper can participate.

The single highest-value *tooling* improvement, if anyone wants to push further, would be a
cross-reference capable DEX analyser — that alone would resolve gaps 1 and 6 and would have caught
the scoping weakness described in the provenance note below.

### Provenance note

This report is built on evidence read directly, cross-checked against a parallel multi-agent static
analysis of the same corpus.

**Two agent runs were required.**

*Run 1* (6 targets): TagTinker, HandyLink and VusionRail Connect returned; `vlink2`, `linkvcore` and
`oemconfig` terminated without results, so the adversarial verification phase never ran. The likely
cause was agents churning unbounded through 20 MB minified bundles.

*Run 2* (the 3 missing targets, with explicit budget discipline and the §7.4 false positives
pre-loaded into the prompts): **all three returned, and the adversarial verification phase ran.**

Net coverage: **all 6 targets analysed; 3 of them (`vlink2`, `linkvcore`, `oemconfig`) additionally
passed through adversarial verification.** The three from run 1 (TagTinker, HandyLink, VusionRail)
were re-verified by hand instead.

#### The verification pass earned its keep

It **refuted four specific claims** from the run-2 recon agents, three of which had already been
written into this document and are now corrected in place (§6.3):

| Refuted claim | Reality |
|---|---|
| "61 GUIDs in vlink2, none a BLE UUID" | The sweep covered only JS. The **DEX** holds `0000fc8c-…` (SIG member UUID `0xFC8C`) in the vendor SDK's config class |
| "vlink2's REST surface is positioning only" | It also has `labels/matchings`, `labels/{id}/pages`, `labels/packages`, `labels/returns`, `labels/flash`, `labels/refresh` |
| "VusionBeaconPlugin's surface is these 7 methods" | Enumeration was ~half the real surface (conclusion unaffected) |
| "zero image keywords in vlink2" | Wrong on three keywords; a `LabelImage` *fetch* service exists (conclusion unaffected) |

More instructive than any single refutation: the verifier found that the vlink2 recon agent had
scoped its vendor-class search to `^Lcom/vusion`, **but the actual vendor SDK lives in an obfuscated
`La/*` package** that search never touched. Its correct conclusion "survived by luck of scope as much
as by method." The verifier rebuilt the proof on a global sweep — including a **positive control**
against `com.vusionrail.connect` to show the `e46a6e*` negative was real and not a broken regex — and
it now holds. This is the strongest argument in the whole exercise for not shipping single-sourced
agent output.

The verifier also explicitly listed what it **could not** verify, which drove the INFERRED downgrades
in §5.1: all zero-caller/dead-code claims (no xref tooling), the `OEM` permission's `protectionLevel`
(no AXML attribute parser), and whether `0xFC8C` is actually passed to `setServiceUuid`.

#### Corrections caught by hand-verification of run 1

| Corrected | Was | Is |
|---|---|---|
| §3.4 | Sequence attributed to `tagtinker_build_image_sequence()` | That function is **header-only, no implementation, no callers**; the real engine is `scenes/tagtinker_scene_transmit.c` |
| §9(a) | "No vendor REST API surface was recovered" | VusionRail's **was** recovered; the blocker is authorization, not knowledge |
| §7.4 | Five shared GUIDs are "library/build GUIDs" | They are **Azure AD B2C appIds and OAuth scopes** |
| §5.1 | `oemconfig` and `handylink` framed as one Hanshow family | **Zero** cross-references; `oemconfig`'s sibling is `com.hanshow.cartwise` |

Every agent-contributed claim was **independently re-verified before being recorded here**, and the
re-checks earned their keep — they caught four errors in earlier drafts of this document:

| Corrected | Was | Is |
|---|---|---|
| §3.4 | Sequence attributed to `tagtinker_build_image_sequence()` | That function is **header-only, no implementation, no callers**; the real engine is `scenes/tagtinker_scene_transmit.c` |
| §9(a) | "No vendor REST API surface was recovered" | VusionRail's **was** recovered; the blocker is authorization, not knowledge |
| §7.4 | Five shared GUIDs are "library/build GUIDs" | They are **Azure AD B2C appIds and OAuth scopes** |
| §5.1 | `oemconfig` and `handylink` framed as one Hanshow family | **Zero** cross-references; `oemconfig`'s sibling is `com.hanshow.cartwise` |

Two cross-validations worth noting:

- The VusionRail agent **independently reproduced all three false positives catalogued in §7.4**
  (ZXing checksum internals, the `colorPalette` Ionicon, the absent image pipeline).
- The vlink2 agent **independently confirmed the `e46a6e*` family is absent** from vlink2 and
  positively identified the five shared GUIDs, converging with §7.4 from a different direction.

No claim in this report rests on unverified agent output. Where sources disagreed, the evidence was
reopened and the direct reading won.

---

## Appendix A — Evidence paths

```
Project
  /Users/hoggormino/Projects/TagTinker/application.fam
  /Users/hoggormino/Projects/TagTinker/tagtinker_app.h
  /Users/hoggormino/Projects/TagTinker/protocol/tagtinker_proto.c
  /Users/hoggormino/Projects/TagTinker/nfc/tagtinker_nfc.c
  /Users/hoggormino/Projects/TagTinker/scenes/tagtinker_scene_nfc_scan.c   (branch nfc-scan-vusion-fixes)
  /Users/hoggormino/Projects/TagTinker/esp32-wifi-fw/shared/tt_wifi_proto.h
  /Users/hoggormino/Projects/TagTinker/esp32-wifi-fw/main/cloud_client.c

Vendor packages (originals, untouched)
  /Users/hoggormino/com.hanshow.cart.oemconfig_v1.1.7/base.apk
  /Users/hoggormino/com.hanshow.handylink_v3.0.1/{base,split_config.arm64_v8a}.apk
  /Users/hoggormino/com.ses.link.linkvcore_v3.2.284530/base.apk
  /Users/hoggormino/com.vusion.vlink2_v3.0.279189/base.apk
  /Users/hoggormino/com.vusionrail.connect_v3.2.15679/base.apk

Key artifacts within packages
  com.hanshow.cart.oemconfig  res/xml/app_restrictions.xml
  com.hanshow.handylink       split_config.arm64_v8a/lib/arm64-v8a/libapp.so
                              base/assets/libs/arm64-v8a/aes256_lib.so
  com.ses.link.linkvcore      base/assets/capacitor.{config,plugins}.json
                              base/assets/public/*.js
  com.vusion.vlink2           base/assets/capacitor.{config,plugins}.json
                              base/assets/beacons_crf_fr_hyper.0055_ble.json
                              base/assets/public/*.js
  com.vusionrail.connect      base/assets/public/*.js   (webpack module 2442 — GATT constants)
```

## Appendix B — Reproducing the analysis

```bash
python3 -m venv venv && venv/bin/pip install androguard
venv/bin/python apkinfo.py manifest  <apk>
venv/bin/python apkinfo.py strings   <apk> 'REGEX' --limit 40
venv/bin/python apkinfo.py classdump <apk> 'CLSREGEX' --strings
venv/bin/python jsctx.py <bundle.js> 'REGEX' --before 300 --after 700
```

Decoding the OEMConfig restriction schema:

```python
from androguard.core.apk import APK
from androguard.core.axml import AXMLPrinter
a = APK('base.apk')
print(AXMLPrinter(a.get_file('res/xml/app_restrictions.xml')).get_xml().decode())
```
