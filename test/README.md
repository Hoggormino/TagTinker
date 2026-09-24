# TagTinker host tests

Off-device unit tests for the pure logic of FAP modules. They compile the
**real** module source against tiny stub headers in `test/stubs/` (for `<furi.h>`
and `<storage/storage.h>`), so no Flipper SDK or hardware is needed.

## Run

```sh
./test/run_oepl_test.sh    # OpenEPaperLink module
./test/run_proto_test.sh   # protocol geometry/layout helpers
./test/run_nfc_test.sh     # NFC URL-host matcher
```

Requires a C compiler (`cc`, or set `CC=gcc`). Exit code 0 = all pass.

## What is covered

- `oepl/tagtinker_oepl.c` — MAC normalization, config + owned-tag allow-list
  loading (from `test/fixtures/`), the allow-list safety gate, and the exact
  `POST /imgupload` request bytes and `Content-Length`.
- `protocol/tagtinker_proto.h` — the pure, inline geometry/layout helpers:
  the Color 2.6 wire↔glass coordinate transform (`tagtinker_color26_proto_to_glass`),
  page resolution (`tagtinker_color26_resolve_page`), width/height swap
  (`tagtinker_type_needs_wh_swap`), and profile glass sizing. No stubs are
  needed — the header is plain C.

  Not covered: the functions defined in `protocol/tagtinker_proto.c`
  (`tagtinker_crc16`, barcode validation, frame builders). That translation
  unit includes `tagtinker_app.h`, so exercising it off-device would require
  stubbing the whole app — a separate follow-up.
- `nfc/tagtinker_nfc.c` — `tagtinker_nfc_url_host_is()`, the case-insensitive
  URL-host matcher used to classify a tag's link (e.g. `nfc.imagotag.com`):
  scheme stripping, the label-boundary check, and the NULL/empty guards. Built
  against a minimal `mf_ultralight` stub in `test/stubs/`.

The tests transmit nothing and touch no network or radio.
