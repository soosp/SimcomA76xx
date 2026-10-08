/**
 * SimcomA76xx — SMS
 *
 * Shows how a few texts would be encoded (no modem needed for that), then
 * sends one SMS with a 24-hour validity period.
 *
 * Set PHONE_NUMBER to your own number (digits, optionally with a leading '+'
 * for the international form) and the pins to
 * your wiring. The sketch is saved as UTF-8, so the accented text below is
 * UTF-8 — exactly what sendSMS() expects.
 */

#include <Arduino.h>
#include <SimcomA76xx.h>

#ifndef MODEM_RX_PIN
#define MODEM_RX_PIN 40
#endif
#ifndef MODEM_TX_PIN
#define MODEM_TX_PIN 39
#endif
#ifndef MODEM_PWR_PIN
#define MODEM_PWR_PIN 6
#endif

static const char PHONE_NUMBER[] = "+36301234567";
static const bool SEND           = false;   // set true to actually send

SimcomA76xx modem;

void show(const char* text) {
    SmsInfo i = SimcomA76xx::analyzeSMS(text);
    Serial.print(i.encoding == SmsEncoding::GSM7 ? "GSM7 " : "UCS2 ");
    Serial.print(i.used);
    Serial.print('/');
    Serial.print(i.capacity);
    Serial.print(i.truncated ? " CUT  " : "      ");
    Serial.println(text);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    show("Freezer alarm: -8.5 C");                   // ASCII
    show("Kühlraum Alarm: -8,5 °C");                 // ü in GSM, ° is not -> UCS2
    show("Hűtőkamra riasztás: -8,5 C");              // ű ő á -> UCS2
    show("Price: 10 € [approx]");                    // € [ ] take two septets

#if defined(ARDUINO_ARCH_ESP32)
    Serial2.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    modem.begin(Serial2, MODEM_PWR_PIN);
#else
    Serial1.begin(115200);
    modem.begin(Serial1, MODEM_PWR_PIN);
#endif

    if (!SEND) return;
    if (!modem.waitForReady()) {
        Serial.println("Modem not ready");
        return;
    }
    SmsInfo info;
    bool ok = modem.sendSMS(PHONE_NUMBER, "Hűtőkamra teszt: ékezetek rendben?",
                            24 * 60, SmsEncoding::AUTO, &info);
    Serial.println(ok ? "Sent" : "Send failed");
}

void loop() {}
