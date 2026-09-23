# TagTinker Quality & Dead-Code Audit

**Status:** Read-only audit. No source was modified; no code was staged, committed, or pushed.
**Branch:** `nfc-scan-vusion-fixes` @ `6aa6796`
**Date:** 2026-09-22
**Scope:** ~9,144 lines of C across the Flipper FAP, the ESP32-S2 companion firmware, and the shared
wire protocol. The Cloudflare worker (`cloud-plugins/`) and web image prep (`web-image-prep/`) were not
in scope beyond noting that `ufbt lint` trips over `cloud-plugins/node_modules`.

---

## 0. Method and how to read this

### 0.1 How the findings were produced

Six dimension-specialised finders read the real source in parallel (NFC/NDEF parsing; ESL protocol &
image encode; memory/resource lifecycle; Flipper↔ESP32 UART link + ESP firmware; threads/callbacks/state
machines; dead code). Every raw finding then went through an **independent adversarial verifier** that
reopened the cited `file:line`, grepped the tree, and was told to **refute by default** — downgrade an
overstated severity, reject a misread, or confirm. 41 agents total (6 find + 35 verify).

Result: **35 raw → 32 survived** (31 CONFIRMED, 1 PLAUSIBLE, 3 REFUTED). The verifier corrected many
finder-assigned severities (finders over-labelled cleanup items as high) and caught three false
positives, recorded in §5 for transparency. Every surviving finding here was confirmed against the
source; I additionally re-verified the single highest-severity bug (§2.1) by hand.

### 0.2 Labels

- **Severity** — critical / high / medium / low / info, as corrected by verification (not the finder's
  initial guess).
- **Confidence** — high / medium / low that the defect is real and reachable.
- **Verdict** — CONFIRMED (verifier reopened evidence and it held) or PLAUSIBLE (mechanism real, reachable
  failure path uncertain).
- **Dead-code class** — *confirmed* (no caller/definition anywhere), *likely* (write-only / unused pending
  runtime check), *dormant* (intentional-looking API or platform surface kept for future/out-of-tree use).

### 0.3 Cross-branch reality (important)

The fork carries several unmerged sibling branches (`bmp-bounds`, `custom-size`, `docs-hygiene`,
`esp-console-fix`, `esp-worker-url`, `unknown-tag-guards`, and their integration branch
`local-all-fixes`). **Some findings below are already fixed there but not on this branch.** Each finding
notes its cross-branch status; §6 uses it to prioritise. This audit's unique value is the findings that
are **not** covered anywhere yet — chiefly §2.2 (thread UAF) and §2.4 (Wi-Fi state).

---

## 1. Build / test / lint / static-analysis run

| Command | Documented in | Result |
|---|---|---|
| `.venv/bin/ufbt` (build) | CONTRIBUTING.md ("ensure `ufbt build` passes") | **PASS** — FAP built, Target 7 / API 87.1, no warnings |
| `.venv/bin/ufbt lint` | ufbt built-in | **ERRORS** — but only on `cloud-plugins/node_modules/**` folder names; the app's own C was never linted. See §4 "lint scope". Side effect: drops a `.clang-format` (identical to the ufbt SDK template) at repo root. |
| Host unit tests | — | **None.** The repo's only host test was removed in `f144730` ("Drop the Color 2.6 host unit test"). There is no `make test` on this branch (no Makefile). |
| ESP firmware validation | — | Not run here (requires ESP-IDF; out of scope for this host). |

**Finding (build config):** the project has **no working lint or test gate**. `ufbt lint` is unusable as
configured (§4), and there is no unit harness. For an embedded C project where "the Flipper has very
limited heap" is a stated concern (CONTRIBUTING.md), that is the single biggest process gap — it is why
the bugs in §2 reached a branch.

---

## 2. Bugs

### 2.1 HIGH — Stack buffer overflow: BMP row read into fixed 128-byte `row_buf` with unbounded stride
- **Severity** high · **Confidence** high · **Verdict** CONFIRMED (found independently by two dimensions; re-verified by hand)
- **File:** `scenes/tagtinker_scene_transmit.c:637` (read), `:803` (buffer), `:560`/`:574-578` (unbounded stride)
- **Evidence:**
  ```c
  // :803
  uint8_t row_buf[128];
  // :637  (bmp_read_row_at)
  return storage_file_read(file, row_buf, info->row_stride) == info->row_stride;
  ```
- **Impact:** `tx_bmp_open` accepts bpp 1/2/24/32 (`:560`) and computes `row_stride` straight from the
  BMP header width with **no bound** (`:574` 1/2bpp, `:576` 24bpp `((w*3)+3)&~3`, `:578` 32bpp `w*4`). The
  non-Color2.6 streaming path then reads a whole source row into the 128-byte stack array. A **24bpp BMP
  only ~43px wide** already yields `row_stride > 128`; a 32bpp BMP ≥33px, or a 1/2bpp BMP wider than
  1024px, likewise. Image files are user-supplied (dropped into `apps_data/tagtinker/dropped/` or the
  synced-image list) and the width is taken from the file, not the target profile. The in-code comment
  claims the stride is "bounded by max profile width (800px) → 104 B" — true only for the **monochrome**
  case; it does not hold for the 24/32bpp inputs the same function accepts. Result: an unbounded stack
  write of file-controlled bytes on the transmit thread → crash, and in principle control-flow corruption
  on device.
- **Validation:** place a normal 24-bit BMP ≥64px wide in `apps_data/tagtinker/dropped/`, pick any
  non-Color26 graphics target, `Push image → Transmit`; the first `BMP_FETCH_ROW` overruns `row_buf`.
- **Cross-branch:** **already fixed on `fork/bmp-bounds` and `fork/local-all-fixes`** — they add
  `#define TX_BMP_ROW_BUF_SIZE 128U`, `if(info->row_stride > TX_BMP_ROW_BUF_SIZE) return false;` in
  `tx_bmp_open`, and size `row_buf` from that macro. **Not merged into `nfc-scan-vusion-fixes`.**
- **Remediation:** merge the `bmp-bounds` fix, or independently reject in `tx_bmp_open` any file whose
  computed `row_stride > sizeof(row_buf)` (and/or any width exceeding the max profile width). Do not apply
  until approved.

### 2.2 HIGH — `s_bmp_writer` raced between the Wi-Fi worker thread and the GUI thread (use-after-free)
- **Severity** high · **Confidence** high · **Verdict** CONFIRMED · **not addressed on any branch**
- **File:** `scenes/tagtinker_scene_wifi_run.c:409` (on_exit abort), `:373` (error-path abort); writer callback `scenes/.../wifi_run.c:208` (`run_event_cb`)
- **Evidence:**
  ```c
  // wifi_run.c:409, in tagtinker_scene_wifi_run_on_exit
  tagtinker_wifi_bmp_abort(&s_bmp_writer);
  ```
- **Impact:** `run_event_cb()` runs **entirely on the Wi-Fi worker thread** (`wifi/tagtinker_wifi.c:11`:
  "All callback invocations happen on the worker thread") and mutates the file-scope `static s_bmp_writer`
  via `tagtinker_wifi_bmp_open/chunk/close`. `on_exit` (GUI thread) calls `tagtinker_wifi_bmp_abort(&s_bmp_writer)`.
  If the user leaves the run scene (Back) while `RESULT_CHUNK` frames are still arriving, the GUI thread
  aborts/frees the writer's `File*`/state underneath the worker thread that is still writing to it →
  use-after-free / double-close / heap corruption. There is no mutex or quiesce around `s_bmp_writer`.
- **Validation:** WiFi Plugins → select a plugin → Generate a run that streams a sizeable image; while
  `RESULT_CHUNK` frames are still coming, press Back. Racy, so reproduce under repetition.
- **Remediation:** serialise `s_bmp_writer` access (mutex around open/chunk/close/abort), or stop/quiesce
  the worker before `on_exit` aborts, or gate the worker on a flag cleared under the same lock before the
  abort. This is the **highest-value open finding** — no sibling branch touches it. Do not apply until approved.

### 2.3 MEDIUM — `uint16` truncation of image `byte_count` on large non-chunked BMPs → param/data-frame mismatch
- **Severity** medium · **Confidence** high · **Verdict** CONFIRMED
- **File:** `scenes/tagtinker_scene_transmit.c:169`
- **Evidence:** `(uint16_t)payload->byte_count,` — first arg to `tx_send_image_start`, whose wire field is `uint16_t` (`protocol/tagtinker_proto.h:148`).
- **Impact:** the generic (non-Color26) BMP path encodes the **entire** image into one heap buffer with no
  height chunking, then narrows `payload->byte_count` (`size_t`) to `uint16_t` for the param frame. For a
  two-plane accent image on a large profile this exceeds 65535: SmartTag HD200 Red (800×480, 2 planes) raw
  = 96000 → wraps to 30464; HD150 Red (648×480) = 77760 → 12224. `Auto` compression falls back to raw for
  incompressible content, so it is reachable without forcing Raw. The data loop still emits the full
  `size_t` count of 20-byte frames, so the param frame advertises a truncated size while all data frames
  are sent — the two disagree; the tag receives a malformed upload.
- **Validation:** 800×480 accent target, push a poorly-compressible full-frame BMP, compare the param
  frame `byte_count` against the actual number of 20-byte data frames.
- **Cross-branch:** `transmit.c` is modified on `local-all-fixes` — **may be addressed there; verify** the
  specific narrowing at this call.
- **Remediation:** chunk the generic BMP path by rows (as the Color26/text paths already bound their
  payloads), or clamp/reject when `byte_count` exceeds the 16-bit field.

### 2.4 MEDIUM — ESP `wifi_net_wait_connected()` reports connected while offline (`EV_CONNECTED` never cleared)
- **Severity** medium · **Confidence** high · **Verdict** CONFIRMED · **not addressed on any branch**
- **File:** `esp32-wifi-fw/main/wifi_net.c:139` (wait), `:80` (only set site)
- **Evidence:** `xEventGroupWaitBits(s_eg, EV_CONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));` — `xClearOnExit = pdFALSE`.
- **Impact:** `EV_CONNECTED` is set once in the GOT_IP handler (`:80`) and **never cleared** anywhere
  (the disconnect handler only sets `EV_FAIL`; `forget()` clears nothing). Once the device has connected
  even once, `wait_connected()` returns "connected" immediately thereafter regardless of the true link
  state. *(Verifier correction: not "permanent until reboot" — a successful auto-reconnect re-fires
  GOT_IP and restores real state; but between a drop and a reconnect the bit lies.)* Callers can push
  traffic at a down link.
- **Validation:** on the ESP, connect once (GOT_IP → bit set), drop the AP (STA_DISCONNECTED), then call
  `wifi_net_wait_connected()` — it returns connected though the link is down.
- **Remediation:** `xEventGroupClearBits(s_eg, EV_CONNECTED)` in the STA_DISCONNECTED handler and in
  `wifi_net_forget()`; and/or wait on `EV_CONNECTED|EV_FAIL` and check which fired.

### 2.5 MEDIUM — Transmit Back handler dead-ends and hangs when Transmit is the root scene (web-job auto-start)
- **Severity** medium · **Confidence** high · **Verdict** CONFIRMED
- **File:** `scenes/tagtinker_scene_transmit.c:1100` (fallback chain); root-boot at `tagtinker_app.c:944-945`
- **Evidence:** five nested `scene_manager_search_and_switch_to_previous_scene(...)` calls
  (TargetActions/About/Broadcast/BroadcastMenu/MainMenu), all returning into a `return true`.
- **Impact:** when `web_job.txt` exists, `tagtinker_app_main` boots straight into `TagTinkerSceneTransmit`
  as the **only** scene on the stack. On completion (`tx_active==false`), Back searches for five previous
  scenes that are not on the stack; all searches fail, the handler still `return true` (event consumed),
  so nothing pops and the app is stuck on the Transmit screen — Back does nothing.
- **Validation:** place a valid `web_job.txt` in `APP_DATA_PATH`, launch; it opens Transmit directly; let
  it finish; press Back → no exit.
- **Cross-branch:** `transmit.c` modified on `local-all-fixes` — **may be addressed; verify.**
- **Remediation:** when every fallback search fails, `return false` (let the empty stack pop out) or stop
  the view dispatcher / exit the app explicitly.

### 2.6 LOW — `wifi_link.c` file header documents the wrong UART number and pins
- **Severity** low · **Confidence** high · **Verdict** CONFIRMED
- **File:** `esp32-wifi-fw/main/wifi_link.c:4-8`
- **Evidence:** header says `UART1 by default`, `pin TX = GPIO17, pin RX = GPIO18`; the code below uses
  `UART_NUM_0` with `UART_PIN_NO_CHANGE` (IO_MUX defaults U0TXD=GPIO43/U0RXD=GPIO44, per its own inline
  comment at `:27-30`).
- **Impact:** documentation-only, but actively misleading for anyone wiring the dev board.
- **Cross-branch:** `wifi_link.c` modified on `local-all-fixes` (`esp-console-fix`) — **likely fixed; verify.**
- **Remediation:** correct the header to UART0 / default IO_MUX pins.

### 2.7 INFO / PLAUSIBLE — `extract_uri` truncates silently; caller cannot tell a cut URL from a whole one
- **Severity** info · **Confidence** low · **Verdict** PLAUSIBLE (mechanism real; reachable wrong-accept not demonstrated)
- **File:** `nfc/tagtinker_nfc.c:167` (buffer-full break), `:165` (page-run-out break)
- **Evidence:** `if(out + 1 >= url_size) break;` then `url[out]='\0'; return out > 0;` — both truncation
  exits return success with no signal. `TAGTINKER_NFC_URL_LEN` is 96.
- **Impact:** `tagtinker_nfc_decode_url()` locates the id via `strrchr(url,'/')`. For the normal ESL layout
  a truncated tail shortens the final segment and the `strlen==10` gate rejects it (safe). The verifier
  judged the "maliciously cut to a spurious valid segment" path **not concretely reachable** in this build
  (a real ESL id is ~short, well under 96B), so this is a defensive-robustness note, not a live bug.
- **Remediation (defensive):** give `extract_uri` a `bool* truncated` out-param and have `classify()`/
  decode reject a truncated URL. *(This is exactly what the paused edit to `nfc/tagtinker_nfc.h` was
  starting toward; left for your decision.)*

---

## 3. Dead code

All confirmed by whole-tree grep (excluding `.venv`, `node_modules`). Class per §0.2. None is a runtime
fault; these are cleanup / clarity items. Remediation for every row: remove the symbol (and its
declaration), or wire the intended caller — **do not delete until approved**, and check §0.3 first
(several may be resolved by integrating a sibling branch).

### 3.1 Header-declared functions with no definition anywhere (stale API — would fail to link if called)
| Symbol | File:line | Class |
|---|---|---|
| `tagtinker_make_mcu_frame` | `protocol/tagtinker_proto.h:181` | confirmed |
| `tagtinker_rle_compress` | `protocol/tagtinker_proto.h:185` | confirmed |
| `tagtinker_build_image_sequence` | `protocol/tagtinker_proto.h:190` | confirmed |
| `tagtinker_ir_is_busy` | `ir/tagtinker_ir.h:20` | confirmed |

These four are prototypes whose real work is done by other (often `static`) functions — the transmit
sequence is assembled inline in `tagtinker_scene_transmit.c`; `mcu_frame` exists as a `static` helper;
RLE is `tagtinker_pack_fn_rle`; IR busy-state uses internal flags.

### 3.2 Defined/exported functions with no caller
| Symbol | File:line | Class | Note |
|---|---|---|---|
| `tagtinker_encode_image_payload` | `protocol/tagtinker_proto.c:305` | confirmed | transmit scene calls `..._planes_payload`/`..._fn_payload` directly |
| `tagtinker_pick_chunk_height` | `tagtinker_app.c:341` | confirmed | scene uses its own static `tx_pick_chunk_height`; transitively frees macro `TAGTINKER_STREAM_PIXEL_BUDGET` (`tagtinker_app.c:34`) |
| `tagtinker_nfc_decode_barcode` (+ public `tagtinker_nfc_extract_url` it wraps) | `nfc/tagtinker_nfc.c:203` / `:174` | dormant | scan path uses `tagtinker_nfc_classify` + static `extract_uri`; keep only if intended as external API |
| `tagtinker_wifi_ping` | `wifi/tagtinker_wifi.c:74` | likely | public WiFi API; keep-alive handled by link-lost timeout instead |
| `render_text` (static inline) | `views/tagtinker_font.h:248` | confirmed | only `render_text_ex`/`render_text_region_ex` are used |

### 3.3 Write-only / unused struct fields and state
| Field | File:line | Class | Note |
|---|---|---|---|
| `raw_mode` | `tagtinker_app.h:210` | confirmed | never read or written; comment describes behaviour that doesn't exist |
| `signal_mode` (+ single-value enum `TagTinkerSignalMode`) | `tagtinker_app.h:206` | dormant | written in 5 places, never read; `tx_apply_signal_mode` does `UNUSED(app)` |
| `wifi_rssi` | `tagtinker_app.h:251` | likely | assigned from WifiStatus event, never displayed |
| `ble_synced_lines` | `tagtinker_app.h:223` | likely | written twice in BLE sync, never read |
| `ble_sync_last_completed_chunks`, `ble_sync_last_compact_protocol` | `tagtinker_app.h:239`, `:241` | likely | assigned at sync end, never read |
| `target_actions_view` (+ view id `TagTinkerViewTargetActions`) | `tagtinker_app.h:126`, `:96` | confirmed | never allocated/registered/freed (unlike the other views) |

### 3.4 Unused macros, enums, includes
| Item | File:line | Class |
|---|---|---|
| `TAGTINKER_PROTO_SEG` (`0x84`) — segment opcode never emitted | `protocol/tagtinker_proto.h:17` | confirmed |
| `TAGTINKER_HEX_LEN` (`64`) | `tagtinker_app.h:39` | confirmed |
| `TAGTINKER_NFC_HOST_LEN` (`32`) — *this is the `+4` currently modified in `nfc/tagtinker_nfc.h`; unused as no host buffer exists yet* | `nfc/tagtinker_nfc.h:21` | confirmed |
| `TagTinkerMainMenuItem` enum — superseded by a local enum in the scene | `tagtinker_app.h:274` | confirmed |
| `#include "../views/tagtinker_font.h"` — no font symbol used in this TU | `scenes/tagtinker_scene_size_picker.c:7` | confirmed |

### 3.5 Build configuration — `ufbt lint` scope
- **File:** `.gitignore:11` (`node_modules/`) + on-disk `cloud-plugins/node_modules/`
- **Class:** n/a (config) · **Confidence** high
- **Impact:** `node_modules` is gitignored but present on disk under the FAP root. `ufbt`'s lint target
  recursively globs `.h/.c/.cpp/.cxx/.hpp` and hits vendored C/C++ under `cloud-plugins/node_modules/`
  (e.g. `sharp/src/*.h`), erroring on folder-name and formatting rules. *(Verifier corrected the finder's
  "13 files / pipeline.cc" specifics — lint's extension set excludes `.cc`.)* Net effect: **the app's own
  C is never linted**, so there is no working style/static gate.
- **Remediation:** scope linting to the FAP's own directories (a lint target/CI job listing app dirs, or
  excluding `*/node_modules`), or keep `node_modules` outside the FAP root. **Do not "fix" by editing the
  vendored sources.**

---

## 4. Refuted findings (transparency)

The verifier rejected three raw findings; they are recorded so they are not re-raised:

1. **`tagtinker_app.c:206` — "synced-image record truncated to 255 bytes on index rewrite."** Mechanic
   real (`line_copy[256]` would truncate a >255B line) but **unreachable**: no writer produces a
   `synced_images.txt` record longer than ~133 bytes, so the truncation branch is never hit.
2. **`cloud_client.c:150` — "unchecked `snprintf` truncation → OOB stack write."** The `wrote2` truncation
   is genuinely unchecked and asymmetric with the `:139` check, but with `uint16_t` width/height and the
   fixed buffer there is **no concrete overflow path**; `url[pos]=0` stays in bounds. Cosmetic at most.
3. **`tagtinker_app.h:49` — "zero-value enum members never referenced are dead."** **Backwards** — these
   are deliberate zero-value sentinels that are load-bearing via zero-initialisation (`TxModeNone`,
   `SignalPP4`, etc.). Removing them would break defaults. Good catch by the verifier.

---

## 5. Cross-branch status summary

| Finding | Severity | On this branch | Elsewhere |
|---|---|---|---|
| §2.1 BMP `row_buf` overflow | HIGH | present | **fixed** on `bmp-bounds` / `local-all-fixes` |
| §2.2 `s_bmp_writer` thread UAF | HIGH | present | **not addressed anywhere** |
| §2.3 `uint16` `byte_count` truncation | MED | present | `transmit.c` changed on `local-all-fixes` — verify |
| §2.4 `wifi_net` EV_CONNECTED | MED | present | **not addressed anywhere** |
| §2.5 Transmit Back dead-end | MED | present | `transmit.c` changed on `local-all-fixes` — verify |
| §2.6 `wifi_link` UART comment | LOW | present | likely fixed (`esp-console-fix`) — verify |
| §3.2 `cloud_client_set_url` unused | LOW | present | `esp-worker-url` makes URL a build option (Kconfig) — verify |
| §3.x remaining dead code | LOW/INFO | present | mostly not swept anywhere |

---

## 6. Highest-priority summary and proposed plan

**The two findings that matter most are the ones no branch fixes yet:**

1. **§2.2 (HIGH, open) — `s_bmp_writer` worker/GUI use-after-free.** Real memory-safety bug on the WiFi
   image path, triggerable by pressing Back mid-stream. No existing branch touches it.
2. **§2.4 (MED, open) — ESP `EV_CONNECTED` never cleared.** Silent "connected while offline."

**Already solved, just not here:**

3. **§2.1 (HIGH) — BMP stack overflow — is fixed on `bmp-bounds`.** The priority action is *integration*,
   not a new fix: get `bmp-bounds` into this line of work.

### Proposed implementation plan (for your approval — nothing done yet)

**Phase 1 — safety (do first):**
- Integrate the `bmp-bounds` fix for §2.1 (bound `row_stride` in `tx_bmp_open`).
- Fix §2.2 with a mutex (or worker quiesce) around `s_bmp_writer`.
- Fix §2.4 by clearing `EV_CONNECTED` on disconnect / `forget()`.

**Phase 2 — correctness:**
- §2.3: chunk the generic BMP path or clamp/reject `byte_count > 65535`. *(Confirm whether `local-all-fixes`
  already did this before writing new code.)*
- §2.5: make the Transmit root-scene Back path exit cleanly.
- §2.7: optional defensive truncation flag on `extract_uri` (aligns with the paused `nfc.h` edit).

**Phase 3 — hygiene (low risk, high signal-to-noise):**
- Re-scope `ufbt lint` off `node_modules` (§3.5) — **do this before the dead-code sweep**, because a
  working lint is what prevents the next §2.
- Remove the confirmed dead code in §3 in one reviewable commit, after a final `grep` per symbol and after
  integrating sibling branches (so you don't delete something a branch is about to use).

**Sequencing note:** several sibling branches already carry fixes. The cleanest path is to decide the
branch-integration order first (what merges into `nfc-scan-vusion-fixes`, or whether this work rebases
onto `local-all-fixes`), then apply only the genuinely-open fixes (§2.2, §2.4) plus the hygiene pass on
top. That avoids re-solving §2.1/§2.5/§2.6 by hand.

I have not implemented, deleted, reformatted, staged, committed, or pushed anything. Awaiting your go-ahead
on which phase to start and against which branch.
