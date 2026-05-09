/* 
 * SimcomA76xx API usage Example
 * This example demonstrates API of the SimcomA76xx library
 * for Simcom A76xx series modems
 */

#include <Arduino.h>
#include <SafeSerial.h>
#include "SimcomA76xx.h"

// Change it to the desired destination phone number in international format.
const char* phoneNumber = "+3630XXXXXXX";
// Set it true if you want test SMS sending
const bool sendSms = false;

SimcomA76xx modem;

void setup() {
    Serial.begin(115200);
    Serial.println(F("=== SIMCOM API TEST ==="));

    Serial.println(F("Starting modem..."));
    // Modem Rx pin: 40, Tx pin 39
    Serial1.begin(115200, SERIAL_8N1, 40, 39);
    // Modem pwrPin: 6
    modem.begin(Serial1, 6);
    if (modem.waitForReady(20000)) {
        Serial.println(F("Modem got ready - PB DONE has arrived."));
        if (modem.setAPN("internet")) {
            Serial.println(F("APN has been set successfully."));
        }
    } else {
        Serial.println(F("Timeout: the modem is not responding."));
        Serial.println(F("Press RESET button to restart the system."));
        while(1);
    }

    if (modem.sendAT("AT")) {
        Serial.println(F("Answer: OK - the modem is accessible"));
    } else {
        Serial.println(F("ERROR: The modem does not answer."));
        Serial.println(F("Please, check the power and UART connections of the modem."));
        while(1);
    }

    // Example: limit the band access for an antenna developed for
    // the 900 MHz band LTE (B8). GSM 900 is set for safety reasons.
    Serial.println(F("Limit the communication to 900 MHz LTE band (B8)..."));
    modem.setAllowedModes(MODE_LTE_ONLY);
    if (modem.setAllowedBands(Bands::GSM_900, Bands::B8)) {
        Serial.println(F("Band configuration accepted."));
        // On some firmware versions the modem reattaches automatically.
        // In other cases band changes may require a radio cycle to take effect.
        // To force the radio cycle uncomment the lines below.
        // Serial.println(F("Forcing reattach..."));
        // modem.forceReattach();
    } else {
        Serial.println(F("Warning: Band configuration rejected by modem."));
    }
    delay(500);

    // Reading back the settings to verify
    SupportedBands activeBands;
    if (modem.getSupportedBands(&activeBands)) {
        bool b8ok  = activeBands.lteB8;
        bool g900ok = activeBands.gsm900;
        bool modeok = (modem.getMode() == MODE_LTE_ONLY);

        Serial.print(F("LTE B8:        ")); Serial.println(b8ok   ? "OK" : "NO");
        Serial.print(F("GSM 900:       ")); Serial.println(g900ok  ? "OK" : "NO");
        Serial.print(F("LTE only mode: ")); Serial.println(modeok  ? "YES" : "NO");
        Serial.print(F("LTE mask: 0x")); Serial.println(activeBands.lteMask, HEX);

        if (b8ok && g900ok && modeok) {
            Serial.println(F("Band configuration: OK"));
        } else {
            Serial.println(F("Warning: Band configuration mismatch."));
        }
    }

    // Signal strength
    int rssi = modem.getSignalQuality();
    if (rssi == 99) {
        Serial.println(F("No signal."));
    } else {
        Serial.print(F("Signal strength (CSQ): ")); Serial.println(rssi);
    }
    
    Serial.println(F("Re-check the results of settings..."));
    NetworkMode netMode = modem.getMode();
    if (netMode == MODE_AUTOMATIC || netMode == MODE_LTE_ONLY) {
        Serial.println(F(" LTE mode is enabled."));
    } else {
        Serial.println(F(" Warning: LTE mode is not enabled."));
    }

    if (modem.isBandValid(Bands::B8)) {
        Serial.println(F(" LTE B8 band is configured."));
    } else {
        Serial.println(F(" Warning: LTE B8 band is not configured."));
    }

    // SMS send test
    if (sendSms) {
      Serial.println(F("Sending message..."));
      if (modem.sendSMS(phoneNumber, "Hello from the Simcom API test.")) {
          Serial.println(F("Message has been sent successfully."));
      } else {
          Serial.println(F("Unable to send message."));
      }
    }

    Serial.println(F("=== END OF THE TEST ==="));
}

void loop() {
    // Check the network status every 10 seconds
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 10000) {
        lastCheck = millis();
        
        char opName[32];
        char opId[8];
        if (modem.getOperatorCode(opId, sizeof(opId))) {
            Serial.print(F("Operator: "));
            if (modem.getOperatorName(opName, sizeof(opName))) {
                Serial.print(opName);
                Serial.print(F(" ("));
                Serial.print(opId);
                Serial.print(F(")"));
            } else {
                Serial.print(opId);
            }
        }
        
        Serial.print(F(" | Signal: ")); Serial.print(modem.getSignalQuality());

        switch (modem.getCurrentBand()) {
            case BAND_LTE_B1:
                Serial.print(F(" | Band: LTE B1"));
                break;
            case BAND_LTE_B3:
                Serial.print(F(" | Band: LTE B3"));
                break;
            case BAND_LTE_B7:
                Serial.print(F(" | Band: LTE B7"));
                break;
            case BAND_LTE_B8:
                Serial.print(F(" | Band: LTE B8"));
                break;
            case BAND_LTE_B20:
                Serial.print(F(" | Band: LTE B20"));
                break;
            case BAND_LTE_B28:
                Serial.print(F(" | Band: LTE B28"));
                break;
            case BAND_GSM_900:
                Serial.print(F(" | Band: GSM 900"));
                break;
            case BAND_GSM_1800:
                Serial.print(F(" | Band: GSM 1800"));
                break;
            case BAND_NONE:
                Serial.print(F(" | Band: none"));
                break;
            default:
                // You will never get here.
                Serial.print(F(" | Band: none"));
                break;
        }

        uint32_t cellId = modem.getCellId();
        if (cellId != 0) {
            Serial.print(F(" | Cell ID: 0x")); Serial.println(cellId, HEX);
        }

        RegistrationStatus regStat = modem.getRegistrationStatus();
        Serial.print(F("Registration status: "));
        switch (regStat) {
            case REG_NOT_REGISTERED:
                Serial.println(F("Not registered."));
                break;
            case REG_HOME:
                Serial.println(F("Registered on home network."));
                break;
            case REG_SEARCHING:
                Serial.println(F("Not registered, searching."));
                break;
            case REG_DENIED:
                Serial.println(F("Not registered, registration denied."));
                break;
            case REG_UNKNOWN:
                Serial.println(F("Unknown."));
                break;
            case REG_ROAMING:
                Serial.println(F("Registered, roaming."));
                break;
            case REG_ERROR:
                Serial.println(F("Error."));
                break;
            default:
                // You will never get here.
                Serial.println(F("Error."));
                break;
        }
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
}