# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Fixed

- Queries (`sendAT()` in multiline mode, and the getters built on it) now
  read their response up to the final `OK`. The `OK` was left unread, and if
  it arrived after the next command had flushed the input, that command took
  it for its own answer

## [0.5.0] - 2026-10-11

### Added

- `lastError()`: why the last AT transaction failed (`ModemError`: plain
  `ERROR`, `+CME ERROR`, `+CMS ERROR`, timeout, no SMS prompt, lock busy,
  invalid argument, not started) and the `+CME`/`+CMS` code
- `isPermanentSmsError()`: `+CMS ERROR` codes where resending to the same
  number cannot succeed

### Changed

- `waitForReady()` sets numeric error reports (`AT+CMEE=1`) once the modem is
  ready, so error codes can be read

## [0.4.1] - 2026-10-11

### Fixed

- `sendSMS()` writes the PDU in 64-character pieces instead of one `write()`
  per hex digit. On a queued port such as SafeSerial every `write()` is one
  queue item, and a PDU (up to 318 digits) overflowed the queue and arrived
  cut short
- AVR and ESP8266 builds: the cores' `binary.h` defines `B1` as a macro, which
  broke the `Bands::B1` declaration; the macro is set aside around it. On
  these cores user code cannot write `Bands::B1` (use its value, 1)

## [0.4.0] - 2026-10-11

### Added

- `isNetworkReady()` (non-blocking, `AT+CEREG?` then `AT+CREG?`),
  `waitForNetwork()` (blocking) and `isRegistered()`: wait for registration
  before sending, since `waitForReady()` returns before it
- `RegistrationStatus` values `REG_SMS_ONLY_HOME`, `REG_SMS_ONLY_ROAMING` and
  `REG_EMERGENCY_ONLY`
- Example `SimcomA76xx_SMS` waits for registration before sending

### Changed

- `sendSMS()` waits up to 45 s for the result (was 30 s); the AT manual gives
  40 s as the maximum response time of `AT+CMGS`

## [0.3.0] - 2026-10-08

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

[unreleased]: https://github.com/soosp/SimcomA76xx/compare/0.5.0...HEAD
[0.5.0]: https://github.com/soosp/SimcomA76xx/compare/0.4.1...0.5.0
[0.4.1]: https://github.com/soosp/SimcomA76xx/compare/0.4.0...0.4.1
[0.4.0]: https://github.com/soosp/SimcomA76xx/compare/0.3.0...0.4.0
[0.3.0]: https://github.com/soosp/SimcomA76xx/compare/0.2.0...0.3.0
[0.2.0]: https://github.com/soosp/SimcomA76xx/compare/0.1.1...0.2.0
[0.1.1]: https://github.com/soosp/SimcomA76xx/compare/0.1.0...0.1.1
[0.1.0]: https://github.com/soosp/SimcomA76xx/releases/tag/0.1.0
