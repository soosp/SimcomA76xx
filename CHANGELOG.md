# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- `SmsPdu.h`: SMS-SUBMIT PDU encoder with no Arduino dependency — UTF-8
  input, GSM 7-bit (default alphabet and extension table) or UCS-2 chosen
  automatically, cut at a character boundary to one SMS, relative validity
  period, hex output streamed through a callback
- `sendSMS()` parameters `validityMinutes`, `encoding` and `info`
- `analyzeSMS()`: how a text would be sent, without the modem
- Host test suite in `test/` (reference PDUs, a simulated modem), runnable
  from VS Code with Ctrl+Shift+B
- Example: `SimcomA76xx_SMS`

### Changed

- `sendSMS()` sends in PDU mode (`AT+CMGF=0`) instead of text mode, so
  accented letters and other non-ASCII text arrive intact
- `sendSMS()` accepts the number as an optional `+` and digits only — `+`
  numbers are sent as international, others (national numbers, operator short
  codes) as dialled — and refuses anything else without contacting the modem
- `+CMS ERROR` responses end a command as a failure, like `+CME ERROR`
- Removed unnecessary header from API example

### Fixed

- AVR builds: `getSupportedBands()` no longer uses `strtoull()`, which
  avr-libc does not provide

## [0.2.0] - 2026-05-10

### Added

- Thread-safe AT communication on ESP32 via recursive FreeRTOS mutex
- `setMutexTimeout(uint32_t ms)` to configure lock timeout (ESP32 only)

### Changed

- `setAllowedBands()` no longer sends third parameter to improve firmware
  compatibility
- Band masks in `AT+CNBP` commands now use full 16-character hex format

### Fixed

- Synchronize the `platforms` and `architectures` between library description files

## [0.1.1] - 2026-05-08

## Fixed

- Fix SMS prompt detection
- Fix SafeSerial reference in README.md
- Fix Changelog

## Added

- Add examples to library.json
- Notes about setting band

### Changed

- Moved license badge below the package description

## [0.1.0] - 2026-05-08

### Added

- First public release

[unreleased]: https://github.com/soosp/SimcomA76xx/compare/0.2.0...HEAD
[0.2.0]: https://github.com/soosp/SimcomA76xx/compare/0.1.1...0.2.0
[0.1.1]: https://github.com/soosp/SimcomA76xx/compare/0.1.0...0.1.1
[0.1.0]: https://github.com/soosp/SimcomA76xx/releases/tag/0.1.0