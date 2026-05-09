# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

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