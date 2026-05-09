# SimcomA76xx

Arduino library for the **SimCom A76xx** series (including A7670E, A7682E, A7600)
LTE modems.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

This library focuses on stability, predictable AT command parsing, and advanced
network management, specifically designed for Arduino environments.

> ⚠️ **DISCLAIMER**: This library is in its **early development phase**.
APIs are subject to change as we refine the features. Use with caution in
production environments.

## Why this library?

- **Strict Band Management**: Easily restrict the modem to specific LTE/GSM
bands to match your antenna's tuning.
- **Smart Parsing**: Handles command echoes and modem URCs (Unsolicited Result
Codes) gracefully without crashing.
- **Reliable Networking**: Built-in methods for APN configuration and network
registration validation.

## Installation

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps =
    soosp/SimcomA76xx @ ^0.2.0
```

### Arduino IDE

Search for **SimcomA76xx** in the Library Manager (Sketch → Include Library →
Manage Libraries).

### Install manually

1. Download the repository as a ZIP.
2. In the Arduino IDE, go to **Sketch -> Include Library -> Add .ZIP Library**.
For PlatformIO, extract the ZIP file into the `lib` directory in your project folder.

## Quick Example

```cpp
#include <Arduino.h>
#include <SimcomA76xx.h>

SimcomA76xx modem;

void setup() {
    // For logging
    Serial.begin(115200);
    // Underlying modem interface (RX pin 40, TX pin 39)
    Serial2.begin(115200, SERIAL_8N1, 40, 39);

    // Modem power pin connected to pin 6
    modem.begin(Serial2, 6);

    Serial.println(F("Waiting for modem to boot..."));
    if (modem.waitForReady()) {
      Serial.println(F("Modem is READY."));
    } else {
      Serial.println(F("Modem timeout. Check wiring/power."));
      while(1);
    }    
}

void loop() {
    // Get Signal Quality
    int csq = modem.getSignalQuality();
    Serial.print(F("Signal: ")); Serial.println(csq);

    delay(1000);
}
```

## Using with [SafeSerial](https://github.com/soosp/SafeSerial) on ESP32

```cpp
#include <Arduino.h>
#include <SafeSerial.h>
#include <SimcomA76xx.h>

SafeSerialClass modemPort;
SimcomA76xx modem;

void setup() {
    // For logging (using Serial as physical port)
    SafeSerial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(1500));
    // Underlying SafeSerial object (RX pin 40, TX pin 39) for modem using Serial2 as physical interface
    modemPort.begin(Serial2, 115200, SERIAL_8N1, 40, 39);
    // Modem is using SafeSerial. Power pin connected to pin 6
    modem.begin(modemPort, 6);

    SafeSerial.println(F("Waiting for modem to boot..."));
    if (modem.waitForReady()) {
      SafeSerial.println(F("Modem is READY."));
    } else {
      SafeSerial.println(F("Modem timeout. Check wiring/power."));
      while(1);
    }    
}

void loop() {
    // Get Signal Quality
    int csq = modem.getSignalQuality();
    SafeSerial.printf(F("Signal: %u\n"), csq);

    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

## Thread Safety (ESP32)

On ESP32, all AT transactions are protected by a recursive FreeRTOS mutex,
making it safe to call library methods from multiple tasks simultaneously.

The default lock timeout is 10000 ms. If your application uses long-running
concurrent operations, increase it with:

```cpp
modem.setMutexTimeout(15000);
```

On other platforms (AVR, STM32, ESP8266) the mutex is a no-op and has no overhead.

**Recommendations:**

- For fully thread-safe operation on ESP32 series MCUs, recommended to use
[SafeSerial](https://github.com/soosp/SafeSerial) library instead of using
the low level Serial ports directly.

## Contributing

As this is an early-stage project, feedback and Pull Requests are welcome!

## License

Distributed under the **MIT License**. See `LICENSE` for more information.

**Notice**: Portions of this software were generated using AI language models.
This code is provided "as is".

## Authors

Created in 2026 by **Péter Soós**.
