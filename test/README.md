# TagTinker host tests

Off-device unit tests for the pure logic of FAP modules. They compile the
**real** module source against tiny stub headers in `test/stubs/` (for `<furi.h>`
and `<storage/storage.h>`), so no Flipper SDK or hardware is needed.

## Run

```sh
./test/run_oepl_test.sh
```

Requires a C compiler (`cc`, or set `CC=gcc`). Exit code 0 = all pass.

## What is covered

- `oepl/tagtinker_oepl.c` — MAC normalization, config + owned-tag allow-list
  loading (from `test/fixtures/`), the allow-list safety gate, and the exact
  `POST /imgupload` request bytes and `Content-Length`.

The tests transmit nothing and touch no network or radio.
