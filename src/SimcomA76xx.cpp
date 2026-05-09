/**
 * @file SimcomA76xx.cpp
 * @brief Implementation of the SimcomA76xx library.
 */

#include "SimcomA76xx.h"

SimcomA76xx::SimcomA76xx() : _serial(NULL), _pwrPin(-1), _mutexTimeout(10000) {
    memset(_lineBuf, 0, sizeof(_lineBuf));
#ifdef ARDUINO_ARCH_ESP32
    _mutex = xSemaphoreCreateRecursiveMutex();
#endif
}

bool SimcomA76xx::_lock(uint32_t timeoutMs) {
#ifdef ARDUINO_ARCH_ESP32
    return xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
#else
    (void)timeoutMs; // unused on non-ESP32
    return true;
#endif
}

void SimcomA76xx::_unlock() {
#ifdef ARDUINO_ARCH_ESP32
    xSemaphoreGiveRecursive(_mutex);
#endif
}

void SimcomA76xx::_wait(uint32_t ms) {
#ifdef ARDUINO_ARCH_ESP32
    vTaskDelay(pdMS_TO_TICKS(ms));
#else
    delay(ms);
#endif
}

void SimcomA76xx::uint64ToHex(uint64_t val, char* out) {
    uint32_t high = (uint32_t)(val >> 32);
    uint32_t low  = (uint32_t)(val & 0xFFFFFFFF);
    sprintf(out, "0x%08lX%08lX", (unsigned long)high, (unsigned long)low);
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
            // SMS prompt: "> " arrives without \n
            if (idx == 2 && _lineBuf[0] == '>' && _lineBuf[1] == ' ') return true;
        }
        _wait(1);
    }
    return false;
}

bool SimcomA76xx::waitForResponse(const char* expected, uint32_t timeout) {
    if (!_lock(_mutexTimeout)) return false;

    bool result = false;
    uint32_t start = millis();
    while (millis() - start < timeout) {
        if (readLine(timeout - (millis() - start))) {
            if (strcmp(_lineBuf, expected) == 0)          { result = true;  break; }
            if (strcmp(_lineBuf, "ERROR") == 0)           { result = false; break; }
            if (strncmp(_lineBuf, "+CME ERROR", 10) == 0) { result = false; break; }
        }
    }
    _unlock();
    return result;
}

bool SimcomA76xx::sendAT(const char* command, const char* expectedResponse,
                          uint32_t timeout, bool multiline) {
    if (!_serial) return false;
    if (!_lock(_mutexTimeout)) return false;

    while (_serial->available()) _serial->read();
    _serial->print(command);
    _serial->print('\r');

    bool result = false;
    uint32_t start = millis();
    while (millis() - start < timeout) {
        if (readLine(timeout - (millis() - start))) {
            if (strcmp(_lineBuf, "ERROR") == 0)           { result = false; break; }
            if (strncmp(_lineBuf, "+CME ERROR", 10) == 0) { result = false; break; }
            if (strcmp(_lineBuf, expectedResponse) == 0)  { result = true;  break; }
            if (multiline && strstr(_lineBuf, expectedResponse)) { result = true; break; }
        }
    }
    _unlock();
    return result;
}

#ifdef ARDUINO_ARCH_ESP32
void SimcomA76xx::setMutexTimeout(uint32_t ms) {
    _mutexTimeout = ms;
}
#endif

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
    _wait(500);
    digitalWrite(_pwrPin, HIGH);
}

bool SimcomA76xx::waitForReady(uint32_t timeoutMs) {
    if (!_lock(timeoutMs)) return false;

    bool result = false;
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (readLine(500)) {
            if (strstr(_lineBuf, "PB DONE")) { result = true; break; }
        }
        _wait(10);
    }
    _unlock();
    return result;
}

bool SimcomA76xx::setAllowedModes(uint8_t mode) {
    if (!_lock(_mutexTimeout)) return false;

    char cmd[32];
    sprintf(cmd, "AT+CNMP=%d", mode);
    bool result = sendAT(cmd, "OK", 2000);
    _unlock();
    return result;
}

bool SimcomA76xx::setAllowedBands(uint64_t gsmMask, uint64_t lteMask) {
    if (!_lock(_mutexTimeout)) return false;

    char gsmHex[20], lteHex[20], cmd[128];
    uint64ToHex(gsmMask, gsmHex);
    uint64ToHex(lteMask, lteHex);
    sprintf(cmd, "AT+CNBP=%s,%s", gsmHex, lteHex);
    bool result = sendAT(cmd, "OK", 5000);
    _unlock();
    return result;
}

bool SimcomA76xx::setAPN(const char* apn, const char* user, const char* pwd) {
    if (!_serial) return false;
    if (!_lock(_mutexTimeout)) return false;

    bool result = false;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    if (sendAT(cmd, "OK", 2000)) {
        if (strlen(user) > 0) {
            snprintf(cmd, sizeof(cmd), "AT+AUTHPAR=1,3,\"%s\",\"%s\"", pwd, user);
            result = sendAT(cmd, "OK", 2000);
        } else {
            result = true;
        }
    }
    _unlock();
    return result;
}

bool SimcomA76xx::getSupportedBands(SupportedBands* bands) {
    if (!bands || !_serial) return false;
    if (!_lock(_mutexTimeout)) return false;

    bool result = false;
    memset(bands, 0, sizeof(SupportedBands));
    if (sendAT("AT+CNBP?", "+CNBP:", 3000, true)) {
        char* gsmPart = strchr(_lineBuf, ':');
        if (gsmPart) {
            gsmPart += 2;
            char* ltePart = strchr(gsmPart, ',');
            if (ltePart) {
                bands->gsmMask = strtoull(gsmPart, NULL, 16);
                ltePart++;
                bands->lteMask = strtoull(ltePart, NULL, 16);
                bands->lteB1   = (bands->lteMask & Bands::B1);
                bands->lteB3   = (bands->lteMask & Bands::B3);
                bands->lteB7   = (bands->lteMask & Bands::B7);
                bands->lteB8   = (bands->lteMask & Bands::B8);
                bands->lteB20  = (bands->lteMask & Bands::B20);
                bands->lteB28  = (bands->lteMask & Bands::B28);
                bands->gsm900  = (bands->gsmMask & Bands::GSM_900);
                bands->gsm1800 = (bands->gsmMask & Bands::GSM_1800);
                result = true;
            }
        }
    }
    _unlock();
    return result;
}

NetworkMode SimcomA76xx::getMode() {
    if (!_serial) return MODE_ERROR;
    if (!_lock(_mutexTimeout)) return MODE_ERROR;

    NetworkMode result = MODE_ERROR;
    if (sendAT("AT+CNMP?", "+CNMP:", 1000, true)) {
        int mode;
        if (sscanf(_lineBuf, "+CNMP: %d", &mode) == 1) result = (NetworkMode)mode;
    }
    _unlock();
    return result;
}

SystemMode SimcomA76xx::getCurrentMode() {
    if (!_lock(_mutexTimeout)) return SYS_MODE_UNKNOWN;

    SystemMode result = SYS_MODE_UNKNOWN;
    if (sendAT("AT+CPSI?", "+CPSI:", 1000, true)) {
        if (strstr(_lineBuf, "LTE"))             result = SYS_MODE_LTE;
        else if (strstr(_lineBuf, "GSM"))        result = SYS_MODE_GSM;
        else if (strstr(_lineBuf, "NO SERVICE")) result = SYS_MODE_NO_SERVICE;
    }
    _unlock();
    return result;
}

ActiveBand SimcomA76xx::getCurrentBand() {
    if (!_lock(_mutexTimeout)) return BAND_NONE;

    ActiveBand result = BAND_NONE;
    if (sendAT("AT+CPSI?", "+CPSI:", 1000, true)) {
        char* ptr = strstr(_lineBuf, "BAND");
        if (ptr) {
            result = (ActiveBand)atoi(ptr + 4);
        } else {
            ptr = strstr(_lineBuf, ",B");
            if (ptr) {
                result = (ActiveBand)atoi(ptr + 2);
            } else if (strstr(_lineBuf, "GSM")) {
                if (strstr(_lineBuf, "900"))       result = BAND_GSM_900;
                else if (strstr(_lineBuf, "1800")) result = BAND_GSM_1800;
            }
        }
    }
    _unlock();
    return result;
}

bool SimcomA76xx::isBandValid(Bands::LTE targetBand) {
    if (!_lock(_mutexTimeout)) return false;

    bool result = false;
    ActiveBand current = getCurrentBand();
    if (current != BAND_NONE) {
        uint8_t targetNum = 0;
        uint64_t mask = (uint64_t)targetBand;
        if (mask != 0) {
            while (mask >>= 1) targetNum++;
            targetNum += 1;
            result = (uint8_t)current == targetNum;
        }
    }
    _unlock();
    return result;
}

int SimcomA76xx::getSignalQuality() {
    if (!_serial) return 99;
    if (!_lock(_mutexTimeout)) return 99;

    int rssi = 99;
    if (sendAT("AT+CSQ", "+CSQ:", 2000, true)) {
        sscanf(_lineBuf, "+CSQ: %d,", &rssi);
    }
    _unlock();
    return rssi;
}

RegistrationStatus SimcomA76xx::getRegistrationStatus() {
    if (!_serial) return REG_ERROR;
    if (!_lock(_mutexTimeout)) return REG_ERROR;

    RegistrationStatus result = REG_ERROR;
    if (sendAT("AT+CEREG?", "+CEREG:", 1000, true)) {
        int n, stat;
        if (sscanf(_lineBuf, "+CEREG: %d,%d", &n, &stat) == 2)
            result = (RegistrationStatus)stat;
        else if (sscanf(_lineBuf, "+CEREG: %d", &stat) == 1)
            result = (RegistrationStatus)stat;
    }
    _unlock();
    return result;
}

bool SimcomA76xx::getOperatorName(char* nameBuf, uint8_t maxLen) {
    if (!_serial || !nameBuf) return false;
    if (!_lock(_mutexTimeout)) return false;

    sendAT("AT+COPS=3,0", "OK", 1000);
    bool result = sendAT("AT+COPS?", "+COPS:", 2000, true) &&
                  parseQuotedField(nameBuf, maxLen);
    _unlock();
    return result;
}

bool SimcomA76xx::getOperatorCode(char* buf, uint8_t maxLen) {
    if (!buf || maxLen == 0) return false;
    if (!_lock(_mutexTimeout)) return false;

    sendAT("AT+COPS=3,2", "OK", 1000);
    bool result = sendAT("AT+COPS?", "+COPS:", 1000, true) &&
                  parseQuotedField(buf, maxLen);
    _unlock();
    return result;
}

uint32_t SimcomA76xx::getCellId() {
    if (!_lock(_mutexTimeout)) return 0;

    uint32_t result = 0;
    if (sendAT("AT+CPSI?", "+CPSI:", 1000, true)) {
        char* ptr = strstr(_lineBuf, "+CPSI:");
        if (ptr) {
            int commaCount = 0;
            char* current = ptr;
            while (*current && commaCount < 4) {
                if (*current == ',') commaCount++;
                current++;
            }
            if (commaCount == 4) result = (uint32_t)strtoul(current, NULL, 10);
        }
    }
    _unlock();
    return result;
}

bool SimcomA76xx::sendSMS(const char* phoneNumber, const char* message) {
    if (!_lock(_mutexTimeout + SEND_SMS_DURATION_MS)) return false;

    bool result = false;
    if (sendAT("AT+CMGF=1", "OK", 2000)) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", phoneNumber);
        if (sendAT(cmd, "> ", 5000)) {
            _serial->print(message);
            _serial->write(26); // Ctrl+Z to send
            result = waitForResponse("OK", 30000);
        }
    }
    _unlock();
    return result;
}

bool SimcomA76xx::forceReattach() {
    if (!_serial) return false;
    if (!_lock(_mutexTimeout + FORCE_REATTACH_DURATION_MS)) return false;

    bool result = false;
    if (sendAT("AT+CFUN=0", "OK", 5000)) {
        _wait(1500);
        if (sendAT("AT+CFUN=1", "OK", 5000)) {
            waitForReady(WAIT_READY_TIMEOUT_MS);
            result = true;
        }
    }
    _unlock();
    return result;
}
