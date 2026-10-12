/**
 * @file SimcomA76xx.cpp
 * @brief Implementation of the SimcomA76xx library.
 */

#include "SimcomA76xx.h"

SimcomA76xx::SimcomA76xx() : _serial(NULL), _pwrPin(-1), _mutexTimeout(10000) {
    memset(_lineBuf, 0, sizeof(_lineBuf));
    setError(ModemError::NONE);
#ifdef ARDUINO_ARCH_ESP32
    _mutex = xSemaphoreCreateRecursiveMutex();
#endif
}

bool SimcomA76xx::_lock(uint32_t timeoutMs) {
#ifdef ARDUINO_ARCH_ESP32
    if (xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) return true;
    setError(ModemError::BUSY);
    return false;
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

uint64_t SimcomA76xx::hexToUint64(const char* s) {
    while (*s == ' ') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint64_t v = 0;
    for (;; s++) {
        uint8_t d;
        if      (*s >= '0' && *s <= '9') d = (uint8_t)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (uint8_t)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') d = (uint8_t)(*s - 'A' + 10);
        else break;
        v = (v << 4) | d;
    }
    return v;
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

bool SimcomA76xx::takeErrorLine() {
    ModemError e;
    if (strcmp(_lineBuf, "ERROR") == 0)                e = ModemError::AT_ERROR;
    else if (strncmp(_lineBuf, "+CME ERROR", 10) == 0) e = ModemError::CME_ERROR;
    else if (strncmp(_lineBuf, "+CMS ERROR", 10) == 0) e = ModemError::CMS_ERROR;
    else return false;

    int16_t code = -1;
    if (e != ModemError::AT_ERROR) {
        const char* p = _lineBuf + 10;
        if (*p == ':') p++;
        while (*p == ' ') p++;
        if (*p >= '0' && *p <= '9') {
            int32_t v = 0;
            while (*p >= '0' && *p <= '9' && v < 10000) v = v * 10 + (*p++ - '0');
            if (*p == '\0' || *p == ' ') code = (int16_t)v;
        }
    }
    setError(e, code);
    return true;
}

bool SimcomA76xx::waitForResponse(const char* expected, uint32_t timeout) {
    if (!_lock(_mutexTimeout)) return false;

    bool result = false;
    bool failed = false;
    uint32_t start = millis();
    while (millis() - start < timeout) {
        if (readLine(timeout - (millis() - start))) {
            if (strcmp(_lineBuf, expected) == 0) { result = true; break; }
            if (takeErrorLine())                 { failed = true; break; }
        }
    }
    if (result)       setError(ModemError::NONE);
    else if (!failed) setError(ModemError::TIMEOUT);
    _unlock();
    return result;
}

bool SimcomA76xx::drainToFinal(uint32_t timeout) {
    // Own small buffer: _lineBuf keeps the matched line for the caller
    char line[12];
    uint8_t idx = 0;
    uint32_t start = millis();
    while (millis() - start < timeout) {
        while (_serial->available()) {
            char c = _serial->read();
            if (c == '\r') continue;
            if (c != '\n') {
                if (idx < sizeof(line) - 1) line[idx++] = c;
                continue;
            }
            line[idx] = '\0';
            idx = 0;
            if (strcmp(line, "OK") == 0 || strcmp(line, "ERROR") == 0 ||
                strncmp(line, "+CME ERROR", 10) == 0 ||
                strncmp(line, "+CMS ERROR", 10) == 0) {
                return true;
            }
        }
        _wait(1);
    }
    return false;
}

bool SimcomA76xx::sendAT(const char* command, const char* expectedResponse,
                          uint32_t timeout, bool multiline) {
    if (!_serial) { setError(ModemError::NOT_STARTED); return false; }
    if (!_lock(_mutexTimeout)) return false;

    while (_serial->available()) _serial->read();
    _serial->print(command);
    _serial->print('\r');

    bool result = false;
    bool failed = false;
    uint32_t start = millis();
    while (millis() - start < timeout) {
        if (readLine(timeout - (millis() - start))) {
            if (takeErrorLine())                         { failed = true; break; }
            if (strcmp(_lineBuf, expectedResponse) == 0) { result = true; break; }
            if (multiline && strstr(_lineBuf, expectedResponse)) {
                // The final OK follows the data line: consume it now, or the
                // next command may take it for its own answer
                drainToFinal(timeout - (millis() - start));
                result = true;
                break;
            }
        }
    }
    if (result)       setError(ModemError::NONE);
    else if (!failed) setError(ModemError::TIMEOUT);
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
    if (result) {
        // Numeric +CME/+CMS codes for lastError(); a failure here does not
        // make the modem less ready
        sendAT("AT+CMEE=1", "OK", 2000);
        setError(ModemError::NONE);
    } else {
        setError(ModemError::TIMEOUT);
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
                bands->gsmMask = hexToUint64(gsmPart);
                ltePart++;
                bands->lteMask = hexToUint64(ltePart);
#pragma push_macro("B1")   // a macro on AVR and ESP8266 (binary.h)
#undef B1
                bands->lteB1   = (bands->lteMask & Bands::B1);
#pragma pop_macro("B1")
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

RegistrationStatus SimcomA76xx::queryRegistration(const char* command,
                                                  const char* prefix) {
    if (!_serial) return REG_ERROR;
    if (!_lock(_mutexTimeout)) return REG_ERROR;

    RegistrationStatus result = REG_ERROR;
    if (sendAT(command, prefix, 1000, true)) {
        const char* p = strstr(_lineBuf, prefix);
        int n, stat;
        if (p) {
            p += strlen(prefix);
            if (sscanf(p, " %d,%d", &n, &stat) == 2)
                result = (RegistrationStatus)stat;
            else if (sscanf(p, " %d", &stat) == 1)
                result = (RegistrationStatus)stat;
        }
    }
    _unlock();
    return result;
}

RegistrationStatus SimcomA76xx::getRegistrationStatus() {
    return queryRegistration("AT+CEREG?", "+CEREG:");
}

bool SimcomA76xx::isNetworkReady() {
    if (!_lock(_mutexTimeout)) return false;
    bool ready = isRegistered(queryRegistration("AT+CEREG?", "+CEREG:")) ||
                 isRegistered(queryRegistration("AT+CREG?", "+CREG:"));
    _unlock();
    return ready;
}

bool SimcomA76xx::waitForNetwork(uint32_t timeoutMs) {
    uint32_t start = millis();
    for (;;) {
        if (isNetworkReady()) return true;
        if (millis() - start >= timeoutMs) return false;
        _wait(NETWORK_POLL_MS);
    }
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

/// SmsPdu sink: collects hex digits and writes them to the modem in pieces.
struct PduWriter {
    Stream*  port;
    uint8_t  used;
    uint8_t* buf;
    uint8_t  size;

    void flush() {
        if (used) port->write(buf, used);
        used = 0;
    }
};

static void writePduChar(char c, void* ctx) {
    PduWriter* w = static_cast<PduWriter*>(ctx);
    w->buf[w->used++] = (uint8_t)c;
    if (w->used == w->size) w->flush();
}

bool SimcomA76xx::sendSMS(const char* phoneNumber, const char* message,
                          uint32_t validityMinutes, SmsEncoding encoding,
                          SmsInfo* info) {
    SmsInfo text = SmsPdu::analyze(message, encoding);
    if (info) *info = text;
    if (!_serial) { setError(ModemError::NOT_STARTED); return false; }
    if (!SmsPdu::validNumber(phoneNumber)) {
        setError(ModemError::INVALID_ARGUMENT);
        return false;
    }
    if (!_lock(_mutexTimeout + SEND_SMS_DURATION_MS)) return false;

    bool result = false;
    if (sendAT("AT+CMGF=0", "OK", 2000)) {
        char cmd[20];
        snprintf(cmd, sizeof(cmd), "AT+CMGS=%u",
                 (unsigned)SmsPdu::tpduLength(phoneNumber, text, validityMinutes));
        if (sendAT(cmd, "> ", 5000)) {
            uint8_t   chunk[PDU_CHUNK_SIZE];
            PduWriter writer = {_serial, 0, chunk, PDU_CHUNK_SIZE};
            SmsPdu::write(writePduChar, &writer, phoneNumber, message, text,
                          validityMinutes);
            writer.flush();
            _serial->write(26); // Ctrl+Z to send
            result = waitForResponse("OK", SEND_SMS_RESULT_TIMEOUT_MS);
        } else {
            _serial->write(27); // ESC: abandon a prompt that may still come
            if (_lastError.error == ModemError::TIMEOUT) setError(ModemError::NO_PROMPT);
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
