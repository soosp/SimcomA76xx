# SimcomA76xx API Reference (v0.2.0)

## Class: SimcomA76xx

### Core Methods

#### `void begin(Stream& serialPort, int8_t pwrPin = -1)`

Initializes the library with a serial interface and an optional power pin.

- **serialPort**: A reference to a `Stream` object (e.g., `Serial2` or `SafeSerial`).
- **pwrPin**: GPIO pin connected to the modem's Power Key.

#### `bool waitForReady(uint32_t timeoutMs = 20000)`

Blocks execution until the modem sends the `PB DONE` string, indicating it is
fully booted.

- **Returns**: `true` if ready, `false` on timeout.

#### `void hwRestart()`

Sends a hardware pulse (low-high) to the power pin to toggle the modem's power state.

#### `bool sendAT(const char* command, const char* expectedResponse = "OK", uint32_t timeout = 1000, bool multiline = false)`

Sends a raw AT command and waits for a specific response.

- **command**: The AT command string to send (e.g. `"AT+CMGF=1"`).
- **expectedResponse**: In normal mode: the exact response line to match (e.g. `"OK"`).
  In multiline mode: a substring prefix to search for (e.g. `"+CSQ:"`).
- **timeout**: Maximum time to wait for the response in milliseconds.
- **multiline**: If `true`, searches for `expectedResponse` as a substring and
  leaves the matched line in the internal buffer for the caller to parse.
- **Returns**: `true` if the expected response was found, `false` on `ERROR`
  or timeout.

> **Note**: In multiline mode the internal line buffer holds the matched line
> after a successful return. This is used internally by query methods such as
> `getSignalQuality()` and `getRegistrationStatus()`.

---

### Network Management

#### `bool setAllowedModes(uint8_t mode)`

Sets the preferred radio technology.

- **Modes**: `MODE_AUTOMATIC`, `MODE_GSM_ONLY`, `MODE_LTE_ONLY`.

#### `bool setAllowedBands(uint64_t gsmMask, uint64_t lteMask)`

Restricts the modem to specific frequency bands. Use `Bands::LTE` and
`Bands::GSM` constants.

> **Note**: Band changes may require a radio cycle to take effect. On some
> firmware versions the modem handles reattachment automatically after
> `setAllowedBands()`. If the new configuration is not applied, call
> `forceReattach()` explicitly.

#### `bool setAPN(const char* apn, const char* user = "", const char* pwd = "")`

Configures the Access Point Name for data connectivity. Required for HTTP/MQTT.

#### `bool isBandValid(Bands::LTE targetBand)`

Validates if the modem is currently attached to the specified LTE band (e.g., `Bands::B8`).

- **Returns**: `true` if the modem reports the specific band in its active status.

---

### Status & Information

#### `int getSignalQuality()`

Returns the Received Signal Strength Indicator (RSSI).

- **Values**: 0-31 (Higher is better), 99 (Unknown/No signal).

#### `RegistrationStatus getRegistrationStatus()`

Queries the network registration state (`AT+CEREG?`).

**Returns:**

- `REG_HOME` (1): Registered on home network.
- `REG_SEARCHING` (2): Not registered, but currently searching for an operator.
- `REG_ROAMING` (5): Registered on a roaming partner network.
- `REG_NOT_REGISTERED` (0): Not registered and not searching.
- `REG_DENIED` (3): Registration was rejected by the network.
- `REG_UNKNOWN` (4): Unknown state (out of coverage).
- `REG_ERROR` (-1): Communication error with the modem.

##### Application example

```cpp
RegistrationStatus reg = modem.getRegistrationStatus();

if (reg == REG_HOME || reg == REG_ROAMING) {
    Serial.println("Network connection: OK");
} else if (reg == REG_SEARCHING) {
    Serial.println("Searching for network...");
}
```

#### `bool getOperatorName(char* nameBuf, uint8_t maxLen)`

Retrieves the network operator name (e.g., "vodafone HU").

#### `bool getOperatorCode(char* buf, uint8_t maxLen)`

Retrieves the numeric PLMN (MCC/MNC) code of the current operator.

- **Example output**: `"21630"`
- **Returns**: `true` on success.

#### `uint32_t getCellId()`

Retrieves the unique identifier of the current serving cell (Global Cell ID).

- **Returns**: The ID as `uint32_t` (e.g., `11187458`), or `0` if not available.

#### `bool getSupportedBands(SupportedBands* bands)`

Parses the current band configuration into a `SupportedBands` struct.

---

### Utility

#### `bool sendSMS(const char* phoneNumber, const char* message)`

Sends a plain text SMS.

- **phoneNumber**: International format recommended (e.g., "+3630...").

#### `bool forceReattach()`

Quickly toggles the radio (CFUN 0/1) to force the modem to look for a new cell.

- **Returns**: `true` on success.

### Configuration

#### `void setMutexTimeout(uint32_t ms)` *(ESP32 only)*

Sets the base time to wait for the mutex lock before an AT transaction.
Time-intensive operations (`sendSMS`, `forceReattach`) automatically extend
this value by their expected transaction duration. Default is 10000 ms.

If the lock cannot be acquired within the timeout, the method returns its
error value (e.g. `false`, `99`, or `REG_ERROR`) without communicating with
the modem.

> **Note**: Increase this value if multiple tasks frequently call long-running
> methods concurrently. Setting it too low may cause methods to return its error
> value even when the modem is functioning correctly.

---

### Advanced Diagnostics

#### `NetworkMode getMode()`

Returns the preferred network mode configured in the modem.

- **Returns**: `MODE_ERROR`, `MODE_AUTOMATIC`, `MODE_GSM_ONLY`, or `MODE_LTE_ONLY`.

#### `SystemMode getCurrentMode()`

Returns the live radio technology currently in use.

- **Returns**: `SYS_MODE_LTE`, `SYS_MODE_GSM`, `SYS_MODE_NO_SERVICE`, or `SYS_MODE_UNKNOWN`.

#### `ActiveBand getCurrentBand()`

Returns the named constant of the currently active frequency band. It supports
various parsing formats like `BANDx`, `,Bx,`, or frequency values for GSM.

- **Returns**:

- LTE: `BAND_LTE_B1` (1), `BAND_LTE_B8` (8), etc.
- GSM: `BAND_GSM_900` (900), `BAND_GSM_1800` (1800).
- `BAND_NONE` (0) if disconnected.

---

### Namespaces

#### Bands

##### GSM

|Name|Value|Note|
|:--|--:|:--|
|GSM_NONE|0x0|No GSM Band|
|GSM_900|0x80|900 MHz GSM Band|
|GSM_1800|0x100|1800 MHz GSM Band|
|GSM_ALL|0x180|All GSM Bands|

##### LTE

|Name|Value|Note|
|:--|--:|:--|
|LTE_NONE|0x0|No LTE Band|
|B1|0x1|B1 Band (2100 MHz)|
|B3|0x4|B3 Band (1800 MHz)|
|B7|0x40|B7 Band (2600 MHz)|
|B8|0x80|B8 Band (900 MHz)|
|B20|0x80000|B20 Band (800 MHz)|
|B28|0x8000000|B28 Band (700 MHz)|
|ALL|0xFFFFFFFFFFFFFFFF|All LTE Bands|

##### Application example

Enable GSM900 band for GSM, B8 and B20 for LTE.

```cpp
modem.setAllowedBands(Bands::GSM_900, Bands::B8 | Bands::B20);
```

### Data Structures

#### SupportedBands

|Member|Type|Description|
|:--|:--|:--|
|gsmMask|uint64_t|The raw bitmask representing enabled GSM bands.|
|lteMask|uint64_t|The raw bitmask representing enabled LTE bands.|
|lteB1|bool|true if LTE Band 1 (2100 MHz) is enabled.|
|lteB3|bool|true if LTE Band 3 (1800 MHz) is enabled.|
|lteB7|bool|true if LTE Band 7 (2600 MHz) is enabled.|
|lteB8|bool|true if LTE Band 8 (900 MHz) is enabled.|
|lteB20|bool|true if LTE Band 20 (800 MHz) is enabled.|
|lteB28|bool|true if LTE Band 28 (700 MHz) is enabled.|
|gsm900|bool|true if GSM 900 MHz is enabled.|
|gsm1800|bool|true if GSM 1800 MHz (DCS) is enabled.|

##### Implementation Example

Using a struct makes your main code much cleaner. Instead of performing complex
bitwise operations manually, you can simply access the boolean properties:

```cpp
SupportedBands currentBands;

if (modem.getSupportedBands(&currentBands)) {
    Serial.println("Modem Band Configuration:");
    
    if (currentBands.lteB8) {
        Serial.println("- LTE Band 8 (900MHz) is AVAILABLE");
    }
    
    if (currentBands.lteB20) {
        Serial.println("- LTE Band 20 (800MHz) is AVAILABLE");
    }
    
    // Check raw mask if needed
    Serial.printf("Raw LTE Mask: 0x%llX\n", currentBands.lteMask);
}
```

> **Note**: The gsmMask and lteMask are included for advanced users who might
> need to check for bands not yet explicitly mapped to boolean flags in the
> current version of the library.

---
