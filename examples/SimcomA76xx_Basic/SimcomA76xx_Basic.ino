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

    // Modem and SIM identity
    char imei[SimcomA76xx::IMEI_SIZE];
    if (modem.getIMEI(imei, sizeof(imei))) {
        Serial.print(F("IMEI: ")); Serial.println(imei);
    }

    switch (modem.getSimStatus()) {
        case SIM_READY:        Serial.println(F("SIM: ready")); break;
        case SIM_PIN:          Serial.println(F("SIM: PIN required")); break;
        case SIM_PUK:          Serial.println(F("SIM: PUK required (PIN blocked)")); break;
        case SIM_LOCKED:       Serial.println(F("SIM: locked")); break;
        case SIM_NOT_INSERTED: Serial.println(F("SIM: not inserted")); break;
        default:               Serial.println(F("SIM: unknown")); break;
    }

    char iccid[SimcomA76xx::ICCID_SIZE];
    if (modem.getICCID(iccid, sizeof(iccid))) {
        Serial.print(F("ICCID: ")); Serial.println(iccid);
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