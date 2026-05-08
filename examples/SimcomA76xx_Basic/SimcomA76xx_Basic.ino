/* 
 * SimcomA76xx Basic Example
 * This example demonstrates basic usage of the SimcomA76xx library
 * for Simcom A76xx series modems
 */

#include <Arduino.h>
#include <SimcomA76xx.h>

SimcomA76xx modem;

void setup() {
    // For logging
    Serial.begin(115200);
    // Underlyng modem interface (RX pin 40, TX pin 39)
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
    // Get network registration
    RegistrationStatus reg = modem.getRegistrationStatus();
    if (reg == REG_HOME || reg == REG_ROAMING) {
        char opName[32];
        if (modem.getOperatorName(opName, sizeof(opName))) {
            Serial.print(F("Operator: ")); Serial.print(opName); Serial.print(F(", "));
        }
        // Get Signal Quality
        int csq = modem.getSignalQuality();
        Serial.print(F("Signal: ")); Serial.println(csq);
    } else if (reg == REG_SEARCHING) {
        Serial.println("Searching for network...");
    }

    delay(1000);
}