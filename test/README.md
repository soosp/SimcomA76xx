# Host tests

The PDU encoder (`src/SmsPdu.h`) has no Arduino dependency, and
`SimcomA76xx.cpp` compiles against the minimal `Arduino.h` / `Stream.h` in
`stubs/`. Everything runs on a PC: no board, no modem.

## Running

Press `Ctrl+Shift+B` in VS Code — the default build task builds and runs every
test and reports a summary.

From a shell:

```sh
bash test/run_tests.sh   # Linux / macOS
test\run_tests.bat       # Windows
```

Both scripts exit non-zero if any test fails. They need nothing but a C++17
compiler (`g++` by default; override with `CXX`).

On Windows the VSCode task prepends `C:\msys64\ucrt64\bin` to `PATH` so the
MSYS2 toolchain is found without changing the system PATH. If MSYS2 lives
elsewhere, edit that entry in `.vscode/tasks.json`.

## What is covered

|File|Area|
|---|---|
|`test_pdu.cpp`|Whole PDUs: the classic `hellohello` reference, validity on/off, address padding, UCS-2, surrogate pairs, GSM special characters, extension escapes|
|`test_analyze.cpp`|Encoding choice, 160/70 capacity, cutting at character boundaries, invalid UTF-8, number rules, validity rounding, the whole GSM alphabet|
|`test_modem.cpp`|`sendSMS()` against a simulated modem: the AT dialogue, the PDU on the wire, prompt/network/CMGF failures; band-mask parsing|

The generated PDUs were also decoded with an independent implementation
(python-gsmmodem's `decodeSmsPdu`) while this suite was written; they decode
to the original text, number and validity.

What the host cannot show is the real modem's behaviour. Before relying on a
new firmware, send a test SMS with plain ASCII, with accented letters and with
`€` to a real phone.
