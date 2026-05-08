/**
 * @file SimcomA76xx.cpp
 * @brief Implementation of the SimcomA76xx library.
 */

#include "SimcomA76xx.h"

SimcomA76xx::SimcomA76xx() : _serial(NULL), _pwrPin(-1) {
    memset(_lineBuf, 0, sizeof(_lineBuf));
}

/**
 * @brief Internal delay wrapper to support RTOS (ESP32) or standard delay.
 */
void SimcomA76xx::_wait(uint32_t ms) {
#ifdef ARDUINO_ARCH_ESP32
    vTaskDelay(pdMS_TO_TICKS(ms));
#else
    delay(ms);
#endif
}

/**
 * @brief Converts uint64_t band mask to Hex string for AT+CNBP command.
 */
void SimcomA76xx::uint64ToHex(uint64_t val, char* out) {
    uint32_t high = (uint32_t)(val >> 32);
    uint32_t low  = (uint32_t)(val & 0xFFFFFFFF);
    if (high > 0) {
        sprintf(out, "0x%lX%08lX", (unsigned long)high, (unsigned long)low);
    } else {
        sprintf(out, "0x%lX", (unsigned long)low);
    }
}

void SimcomA76xx::begin(Stream& serialPort, int8_t pwrPin) {
    _serial = &serialPort;
    _pwrPin = pwrPin;
    if (_pwrPin != -1) {
        pinMode(_pwrPin, OUTPUT);
        hwRestart();
    }
}

void SimcomA76xx::hwRestart() {
    if (_pwrPin == -1) return;
    digitalWrite(_pwrPin, LOW);
    _wait(500); // Toggle pulse
    digitalWrite(_pwrPin, HIGH);
}

bool SimcomA76xx::waitForReady(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (readLine(500)) {
            // "PB DONE" indicates the Phonebook is ready (modem fully booted)
            if (strstr(_lineBuf, "PB DONE")) return true;
        }
        _wait(10);
    }
    return false;
}

/**
 * @brief Low-level line reader. Handles \r\n endings.
 */
bool SimcomA76xx::readLine(uint32_t timeout) {
    if (!_serial) return false;
    uint32_t start = millis();
    uint16_t idx = 0;
    memset(_lineBuf, 0, sizeof(_lineBuf));
    while (millis() - start < timeout) {
        while (_serial->available()) {
            char c = _serial->read();
            if (c == '\n') {
                if (idx > 0 && _lineBuf[idx-1] == '\r') _lineBuf[idx-1] = '\0';
                return true;
            }
            if (idx < sizeof(_lineBuf) - 1) _lineBuf[idx++] = c;
        }
        _wait(1);
    }
    return false;
}

bool SimcomA76xx::waitForResponse(const char* expected, uint32_t timeout) {
    uint32_t start = millis();
    while (millis() - start < timeout) {
        if (readLine(timeout - (millis() - start))) {
            if (strcmp(_lineBuf, expected) == 0)         return true;
            if (strcmp(_lineBuf, "ERROR") == 0)          return false;
            if (strncmp(_lineBuf, "+CME ERROR", 10) == 0) return false;
        }
    }
    return false;
}

bool SimcomA76xx::parseQuotedField(char* buf, uint8_t maxLen) {
    char* firstQuote = strchr(_lineBuf, '"');
    if (!firstQuote) return false;
    char* secondQuote = strchr(firstQuote + 1, '"');
    if (!secondQuote) return false;

    uint8_t len = secondQuote - (firstQuote + 1);
    if (len >= maxLen) len = maxLen - 1;
    strncpy(buf, firstQuote + 1, len);
    buf[len] = '\0';
    return true;
}

bool SimcomA76xx::sendAT(const char* command, const char* expectedResponse, 
                          uint32_t timeout, bool multiline) {
    if (!_serial) return false;
    while (_serial->available()) _serial->read();

    _serial->print(command);
    _serial->print('\r');

    uint32_t start = millis();
    while (millis() - start < timeout) {
        if (readLine(timeout - (millis() - start))) {
            if (strcmp(_lineBuf, "ERROR") == 0)           return false;
            if (strncmp(_lineBuf, "+CME ERROR", 10) == 0) return false;
            if (strcmp(_lineBuf, expectedResponse) == 0)  return true;
            // continues reading in multiline mode and leaves the last
            // matching line in _lineBuf
            if (multiline && strstr(_lineBuf, expectedResponse)) return true;
        }
    }
    return false;
}

bool SimcomA76xx::setAllowedModes(uint8_t mode) {
    char cmd[32];
    sprintf(cmd, "AT+CNMP=%d", mode);
    return sendAT(cmd, "OK", 2000);
}

bool SimcomA76xx::setAllowedBands(uint64_t gsmMask, uint64_t lteMask) {
    char gsmHex[20], lteHex[20], cmd[128];
    uint64ToHex(gsmMask, gsmHex);
    uint64ToHex(lteMask, lteHex);
    // AT+CNBP sets the band preference for GSM and LTE
    sprintf(cmd, "AT+CNBP=%s,%s,0x0000", gsmHex, lteHex);
    return sendAT(cmd, "OK", 5000);
}

bool SimcomA76xx::setAPN(const char* apn, const char* user, const char* pwd) {
    if (!_serial) return false;
    
    char cmd[128];
    // Set PDP Context 1 (Primary context for data)
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    if (!sendAT(cmd, "OK", 2000)) return false;

    // Set authentication parameters if user is provided
    if (strlen(user) > 0) {
        // Auth type 3 = PAP/CHAP auto-detect
        snprintf(cmd, sizeof(cmd), "AT+AUTHPAR=1,3,\"%s\",\"%s\"", pwd, user);
        return sendAT(cmd, "OK", 2000);
    }
    return true;
}

bool SimcomA76xx::getSupportedBands(SupportedBands* bands) {
    if (!bands || !_serial) return false;
    memset(bands, 0, sizeof(SupportedBands));

    if (!sendAT("AT+CNBP?", "+CNBP:", 3000, true)) return false;

    char* gsmPart = strchr(_lineBuf, ':');
    if (!gsmPart) return false;
    gsmPart += 2;

    char* ltePart = strchr(gsmPart, ',');
    if (!ltePart) return false;

    bands->gsmMask = strtoull(gsmPart, NULL, 16);
    ltePart++;
    bands->lteMask = strtoull(ltePart, NULL, 16);

    bands->lteB1  = (bands->lteMask & Bands::B1);
    bands->lteB3  = (bands->lteMask & Bands::B3);
    bands->lteB7  = (bands->lteMask & Bands::B7);
    bands->lteB8  = (bands->lteMask & Bands::B8);
    bands->lteB20 = (bands->lteMask & Bands::B20);
    bands->lteB28 = (bands->lteMask & Bands::B28);
    bands->gsm900  = (bands->gsmMask & Bands::GSM_900);
    bands->gsm1800 = (bands->gsmMask & Bands::GSM_1800);
    return true;
}

NetworkMode SimcomA76xx::getMode() {
    if (!_serial) return MODE_ERROR;
    
    if (!sendAT("AT+CNMP?", "+CNMP:", 1000, true)) return MODE_AUTOMATIC;
    int mode;
    if (sscanf(_lineBuf, "+CNMP: %d", &mode) == 1) return (NetworkMode)mode;
    return MODE_AUTOMATIC;
}

SystemMode SimcomA76xx::getCurrentMode() {
    if (!sendAT("AT+CPSI?", "+CPSI:", 1000, true)) return SYS_MODE_UNKNOWN;
    if (strstr(_lineBuf, "LTE")) return SYS_MODE_LTE;
    if (strstr(_lineBuf, "GSM")) return SYS_MODE_GSM;
    if (strstr(_lineBuf, "NO SERVICE")) return SYS_MODE_NO_SERVICE;
    return SYS_MODE_UNKNOWN;
}

ActiveBand SimcomA76xx::getCurrentBand() {
    if (!sendAT("AT+CPSI?", "+CPSI:", 1000, true)) return BAND_NONE;
    
    // 1. Test the "BANDx" format
    char* ptr = strstr(_lineBuf, "BAND");
    if (ptr) {
        return (ActiveBand)atoi(ptr + 4);
    }

    // 2. Try the ",Bx," format
    ptr = strstr(_lineBuf, ",B");
    if (ptr) {
        return (ActiveBand)atoi(ptr + 2);
    }

    // 3. Get GSM frequency (fallback)
    if (strstr(_lineBuf, "GSM")) {
        if (strstr(_lineBuf, "900")) return BAND_GSM_900;
        if (strstr(_lineBuf, "1800")) return BAND_GSM_1800;
    }

    return BAND_NONE;
}

bool SimcomA76xx::isBandValid(Bands::LTE targetBand) {
    ActiveBand current = getCurrentBand();
    if (current == BAND_NONE) return false;

    // Convert mask to band number (e.g. B8 -> 8)
    uint8_t targetNum = 0;
    uint64_t mask = (uint64_t)targetBand;
    if (mask == 0) return false;
    while (mask >>= 1) targetNum++;
    targetNum += 1;

    return (uint8_t)current == targetNum;
}

int SimcomA76xx::getSignalQuality() {
    if (!_serial) return 99;
    
    if (!sendAT("AT+CSQ", "+CSQ:", 2000, true)) return 99;
    int rssi = 99;
    sscanf(_lineBuf, "+CSQ: %d,", &rssi);
    return rssi;
}

RegistrationStatus SimcomA76xx::getRegistrationStatus() {
    if (!_serial) return REG_ERROR;
    
    if (!sendAT("AT+CEREG?", "+CEREG:", 1000, true)) return REG_ERROR;
    int n, stat;
    if (sscanf(_lineBuf, "+CEREG: %d,%d", &n, &stat) == 2) return (RegistrationStatus)stat;
    if (sscanf(_lineBuf, "+CEREG: %d", &stat) == 1) return (RegistrationStatus)stat;
    return REG_ERROR;
}

bool SimcomA76xx::getOperatorName(char* nameBuf, uint8_t maxLen) {
    if (!_serial || !nameBuf) return false;
    sendAT("AT+COPS=3,0", "OK", 1000);
    if (!sendAT("AT+COPS?", "+COPS:", 2000, true)) return false;
    return parseQuotedField(nameBuf, maxLen);
}

bool SimcomA76xx::getOperatorCode(char* buf, uint8_t maxLen) {
    if (!buf || maxLen == 0) return false;
    sendAT("AT+COPS=3,2", "OK", 1000);
    if (!sendAT("AT+COPS?", "+COPS:", 1000, true)) return false;
    return parseQuotedField(buf, maxLen);
}

uint32_t SimcomA76xx::getCellId() {
    if (!sendAT("AT+CPSI?", "+CPSI:", 1000, true)) return 0;

    char* ptr = strstr(_lineBuf, "+CPSI:");
    if (!ptr) return 0;

    // The Cell ID comes after 4. comma in both mode (LTE/GSM) 
    // LTE: +CPSI: LTE,Online,216-30,0x3A04,11187458,...
    // GSM: +CPSI: GSM,Online,216-30,0x0D45,5513,...
    int commaCount = 0;
    char* current = ptr;
    while (*current && commaCount < 4) {
        if (*current == ',') commaCount++;
        current++;
    }

    if (commaCount == 4) {
        // strtoul handles the decimal number for the comma
        return (uint32_t)strtoul(current, NULL, 10);
    }

    return 0;
}

bool SimcomA76xx::sendSMS(const char* phoneNumber, const char* message) {
    if (!sendAT("AT+CMGF=1", "OK", 2000)) return false;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", phoneNumber);
    if (!sendAT(cmd, "> ", 5000)) return false;
    _serial->print(message);
    _serial->write(26); // Ctrl+Z to send
    return waitForResponse("OK", 30000);
}

bool SimcomA76xx::forceReattach() {
    if (!_serial) return false;

    if (!sendAT("AT+CFUN=0", "OK", 5000)) return false; // Radio off
    _wait(1500);
    if (!sendAT("AT+CFUN=1", "OK", 5000)) return false; // Radio on
    waitForReady(15000);

    return true;
}