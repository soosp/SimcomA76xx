/* 
 * SimcomA76xx SafeSerial Example
 * This example demonstrates usage of the SimcomA76xx library for Simcom A76xx
 * series modems using with SafeSerial thread-safe serial library for ESP32
 * series MCUs.
 */

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
    // Get network registration
    RegistrationStatus reg = modem.getRegistrationStatus();
    if (reg == REG_HOME || reg == REG_ROAMING) {
        char opName[32];
        if (modem.getOperatorName(opName, sizeof(opName))) {
            SafeSerial.printf(F("Operator: %s, "), opName);
        }
        // Get Signal Quality
        int csq = modem.getSignalQuality();
        SafeSerial.printf(F("Signal: %u\n"), csq);
    } else if (reg == REG_SEARCHING) {
        SafeSerial.println("Searching for network...");
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}