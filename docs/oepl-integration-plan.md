# OpenEPaperLink Integration — Implementation Plan

**Scope:** owner-controlled OpenEPaperLink (OEPL) hardware only, via the AP's **documented local HTTP
API**. The Flipper is a UI/controller; the **OEPL access point does all tag radio**. No proprietary
retail ESL RF, no pairing handshakes, no direct radio transmission from the Flipper.

**Grounded facts (confirmed against OEPL docs):**
- Image push is `POST /imgupload` on the AP, `multipart/form-data`, fields: `mac` (6- or 8-byte hex,
  e.g. `0197E5CB3B38`), `dither` (`0`/`1`), file part named `file`; image sized to the tag.
  Source: [OEPL wiki — Image upload](https://github.com/OpenEPaperLink/OpenEPaperLink/wiki/Image-upload).
- The AP stores tag check-ins in `tagDB.json`; the exact tag-DB HTTP path varies by AP firmware, so it
  stays configurable rather than hardcoded.

---

## Architecture: why a two-phase split

The Flipper Zero has **no IP stack**. Every HTTP call to the AP must traverse a WiFi bridge. TagTinker
already has exactly this: the ESP32-S2 dev board firmware (`esp32-wifi-fw/`) speaking the framed UART
protocol (`shared/tt_wifi_proto_fap.h`) to the FAP module `wifi/tagtinker_wifi.c`. OEPL reuses that
path:

```
Flipper (UI) ──UART framed──► ESP32-S2 bridge ──HTTP/LAN──► OEPL AP ──radio──► your tags
   oepl module builds            (sends the request,          /imgupload      (AP owns all RF)
   the HTTP request              streams status back)
```

- **Phase 1 (this change):** the FAP-side `oepl` module — config, owned-tag allow-list, `/imgupload`
  request shaping, tag-DB JSON parsing — plus a read-only status/preview scene. Fully compiled and
  unit-tested; **transmits nothing**.
- **Phase 2 (follow-up):** an ESP firmware frame that relays the module's request to the AP over the
  LAN and streams the response back. Requires the ESP-IDF build and a real AP to test end-to-end.

---

## 1. Connecting to the access point
- `APP_DATA/oepl.conf` holds `host`, `port` (default 80), optional `token`. No compiled-in address; the
  feature is inert until the owner writes this file.
- Phase 1: the module formats requests against `http://<host>:<port>`. Phase 2: the bridge opens the
  socket (the ESP already does HTTP in `cloud_client.c`; OEPL adds a generic-request frame).

## 2. Tag discovery / status + owned-tag allow-list
- **Allow-list is the safety gate.** `APP_DATA/oepl_tags.txt`, one MAC per line. Every action calls
  `tagtinker_oepl_is_allowed(mac)`; a MAC not on the list is refused before any request is built.
- Status: parse the AP's tag DB JSON (`tagtinker_oepl_parse_tag_json`) → `{mac, hwType, w, h,
  batteryMv, rssi}`. Offline preview: the owner can drop the AP's `tagDB.json` export at
  `APP_DATA/oepl_tagdb.json` and the status scene renders owned tags with their sizes.

## 3. Select, resize, convert, submit an image
- Reuse TagTinker's existing image path (BMP + the browser image-prep) to produce a tag-sized image.
  OEPL dithers server-side, so the FAP picks the tag's resolution (from its type) and sets `dither=1`
  for pictures / `0` for text.
- `tagtinker_oepl_build_imgupload_request()` produces the exact `POST /imgupload` preamble (request
  line, Host, multipart boundary, `mac` + `dither` parts, file part header) and reports the content
  length. Phase 1 shows this as a **dry-run preview** — the owner sees precisely what would be sent,
  to which allow-listed MAC, before any transport exists.

## 4. Progress, failure, retry, logs, confirmation
- Reuse the WiFi-run event model (`PROGRESS` / `RESULT` / `ERROR`). Submit becomes a bridge job with a
  percent bar; HTTP non-200 or bridge error → failure screen with the AP's status text + Retry; success
  → confirmation showing MAC, tag type, bytes. All steps `FURI_LOG`. Phase 1 defines the result enum
  and messages; Phase 2 wires them to the live job.

## 5. Credentials / configuration without hardcoded secrets
- Everything lives in `APP_DATA/oepl.conf`, created by the owner. An optional `token` (only if their AP
  is access-controlled) is read at runtime, sent as a header by the bridge, and **never logged**.
  Nothing secret is compiled into the FAP.

## 6. Test strategy
- **Build:** `ufbt` compiles the module against the real SDK (host `cc` is Xcode-license-locked on this
  machine, so no host C harness — see limitations).
- **Logic:** a Python reference test mirrors the pure functions (MAC normalize, allow-list membership,
  multipart field formatting, tag-JSON extraction) against known vectors.
- **End-to-end:** point `oepl.conf` at a **local OEPL AP you run yourself or your own AP**, verify the
  dry-run request, then (Phase 2) submit to one allow-listed tag and confirm the refresh. No retail
  infrastructure involved at any step.

---

## What Phase 1 deliberately does NOT do
- No socket/transmit from the Flipper (needs the Phase 2 ESP frame + a real AP).
- No proprietary-ESL anything. OEPL only.
- No auto-discovery that probes the network — the owner names their AP and lists their tags.
