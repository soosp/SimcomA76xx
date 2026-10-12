/**
 * @file test_modem.cpp
 * @brief sendSMS() against a simulated modem: the AT dialogue, the PDU on
 *        the wire, and the failure paths.
 */

#include <Arduino.h>
#include "../src/SimcomA76xx.h"
#include "test_harness.h"
#include <deque>
#include <vector>

/// Scripted modem: answers each command line, records everything written.
class FakeModem : public Stream {
public:
    std::string            wire;        ///< Everything the library wrote
    std::vector<std::string> commands;  ///< Command lines (without \r)
    std::string            pdu;         ///< Text written after the prompt, up to Ctrl+Z
    bool promptOk      = true;          ///< Answer AT+CMGS with "> "
    bool sendOk        = true;          ///< Answer the PDU with OK
    bool cmgfOk        = true;
    bool promptSilent  = false;         ///< AT+CMGS gets no answer at all
    const char* sendError = "+CMS ERROR: 500";   ///< Final line when !sendOk
    const char* cmgfError = "ERROR";             ///< Final line when !cmgfOk
    bool cmeeSet       = false;         ///< AT+CMEE=1 was received
    int  cereg         = 1;             ///< <stat> reported by AT+CEREG?
    int  creg          = 1;             ///< <stat> reported by AT+CREG?
    int  registerAfter = 0;             ///< CEREG turns 1 after this many queries
    bool lateOk        = false;         ///< Query answers: the final OK comes a moment later
    int  regQueries    = 0;
    int  writeCalls    = 0;             ///< write() calls (a queued port: one item each)
    int  pduWriteCalls = 0;             ///< ... of them while the PDU was being sent

    int available() override {
        // lateOk: the held-back OK appears only after one empty poll, as on
        // a real UART where it is still on the wire
        if (_rx.empty() && !_late.empty()) {
            if (_latePolls++ >= 1) {
                for (char c : _late) _rx.push_back(c);
                _late.clear();
                _latePolls = 0;
            }
        }
        return (int)_rx.size();
    }
    int read() override {
        if (_rx.empty()) return -1;
        int c = (uint8_t)_rx.front(); _rx.pop_front(); return c;
    }
    /// Unsolicited start-up lines, as after power-on.
    void boot() { reply("\r\nRDY\r\n\r\n+CPIN: READY\r\n\r\nSMS DONE\r\n\r\nPB DONE\r\n"); }

    size_t write(const uint8_t* b, size_t n) override {
        writeCalls++;
        if (_inPdu) pduWriteCalls++;
        for (size_t i = 0; i < n; i++) put(b[i]);
        return n;
    }
    size_t write(uint8_t c) override {
        writeCalls++;
        if (_inPdu) pduWriteCalls++;
        return put(c);
    }

private:
    size_t put(uint8_t c) {
        wire.push_back((char)c);
        if (_inPdu) {
            if (c == 26) {
                _inPdu = false;
                if (sendOk) reply("\r\n+CMGS: 7\r\n\r\nOK\r\n");
                else { reply("\r\n"); reply(sendError); reply("\r\n"); }
            }
            else if (c == 27) { _inPdu = false; }
            else pdu.push_back((char)c);
            return 1;
        }
        if (c == '\r') {
            commands.push_back(_line);
            if (_line == "AT+CMGF=0") {
                if (cmgfOk) reply("\r\nOK\r\n");
                else { reply("\r\n"); reply(cmgfError); reply("\r\n"); }
            }
            else if (_line == "AT+CMEE=1") { cmeeSet = true; reply("\r\nOK\r\n"); }
            else if (_line == "AT+CEREG?") {
                regQueries++;
                if (registerAfter > 0 && regQueries >= registerAfter) cereg = 1;
                char b[48];
                if (lateOk) {
                    snprintf(b, sizeof(b), "\r\n+CEREG: 0,%d\r\n", cereg);
                    _late += "\r\nOK\r\n";
                } else {
                    snprintf(b, sizeof(b), "\r\n+CEREG: 0,%d\r\n\r\nOK\r\n", cereg);
                }
                reply(b);
            }
            else if (_line == "AT+CREG?") {
                char b[48];
                snprintf(b, sizeof(b), "\r\n+CREG: 0,%d\r\n\r\nOK\r\n", creg);
                reply(b);
            }
            else if (_line == "AT+CNBP?")
                reply("\r\n+CNBP: 0x0000000000000180,0x00000000080800C5\r\n\r\nOK\r\n");
            else if (_line.rfind("AT+CMGS=", 0) == 0) {
                if (promptSilent) {}
                else if (promptOk) { reply("\r\n> "); _inPdu = true; }
                else          reply("\r\nERROR\r\n");
            }
            _line.clear();
        } else if (c != 27) {
            _line.push_back((char)c);
        }
        return 1;
    }

    void reply(const char* s) { while (*s) _rx.push_back(*s++); }
    std::deque<char> _rx;
    std::string      _late;
    int              _latePolls = 0;
    std::string      _line;
    bool             _inPdu = false;
};

int main() {
    printf("test_modem\n");

    SECTION("PDU mode dialogue, reference PDU on the wire");
    {
        FakeModem fm;
        SimcomA76xx modem;
        modem.begin(fm);
        SmsInfo info;
        CHECK(modem.sendSMS("+46708251358", "hellohello", 4 * 1440, SmsEncoding::AUTO, &info));
        CHECK(fm.commands.size() == 2);
        CHECK_STR(fm.commands[0], "AT+CMGF=0");
        CHECK_STR(fm.commands[1], "AT+CMGS=23");
        CHECK_STR(fm.pdu, "0011000B916407281553F80000AA0AE8329BFD4697D9EC37");
        CHECK(info.encoding == SmsEncoding::GSM7 && info.used == 10 && !info.truncated);
    }

    SECTION("old two-argument call still works (network default validity)");
    {
        FakeModem fm;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(modem.sendSMS("+46708251358", "hellohello"));
        CHECK_STR(fm.commands[1], "AT+CMGS=22");
        CHECK_STR(fm.pdu, "0001000B916407281553F800000AE8329BFD4697D9EC37");
    }

    SECTION("UCS-2 message length in AT+CMGS");
    {
        FakeModem fm;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(modem.sendSMS("+36301234567", "H\xC5\xB1t\xC5\x91"));
        // 1+1+1+1+6+1+1+1 header octets + 8 UD octets = 21
        CHECK_STR(fm.commands[1], "AT+CMGS=21");
        CHECK_STR(fm.pdu, "0001000B916303214365F7000808" "0048017100740151");
    }

    SECTION("longest PDU goes out in a few write() calls, not one per digit");
    {
        FakeModem fm;
        SimcomA76xx modem;
        modem.begin(fm);
        // 70 UCS-2 characters to a 20-digit number: 159 octets, 318 hex digits
        std::string text;
        for (int i = 0; i < 70; i++) text += "\xC5\x91";
        SmsInfo info;
        CHECK(modem.sendSMS("+12345678901234567890", text.c_str(), 1440,
                            SmsEncoding::AUTO, &info));
        CHECK(info.encoding == SmsEncoding::UCS2 && info.used == 70 && !info.truncated);
        CHECK_STR(fm.commands[1], "AT+CMGS=158");
        CHECK(fm.pdu.size() == 318);
        // 64-digit pieces: 5 writes for the PDU, then Ctrl+Z (the 6th call
        // is counted while the PDU is still open). SafeSerial's default
        // 30-item TX queue takes it all.
        CHECK(fm.pduWriteCalls == 6);
        CHECK(fm.pdu.substr(fm.pdu.size() - 4) == "0151");
    }

    SECTION("invalid number: refused, modem untouched, info still filled");
    {
        FakeModem fm;
        SimcomA76xx modem;
        modem.begin(fm);
        SmsInfo info;
        info.used = 999;
        CHECK(!modem.sendSMS("06 30 123 4567", "abc", 0, SmsEncoding::AUTO, &info));
        CHECK(fm.wire.empty());
        CHECK(info.used == 3);
    }

    SECTION("short code without '+'");
    {
        FakeModem fm;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(modem.sendSMS("1777", "EGYENLEG"));
        CHECK_STR(fm.commands[1], "AT+CMGS=16");   // 8 septets -> 7 octets
        CHECK_STR(fm.pdu.substr(0, 14), "00010004817177");
    }

    SECTION("no prompt: ESC is sent, false returned");
    {
        FakeModem fm;
        fm.promptOk = false;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendSMS("+36301234567", "abc"));
        CHECK(!fm.wire.empty() && fm.wire.back() == 27);
        CHECK(fm.pdu.empty());
    }

    SECTION("network refuses: false");
    {
        FakeModem fm;
        fm.sendOk = false;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendSMS("+36301234567", "abc"));
    }

    SECTION("CMGF refused: nothing else is sent");
    {
        FakeModem fm;
        fm.cmgfOk = false;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendSMS("+36301234567", "abc"));
        CHECK(fm.commands.size() == 1);
    }

    SECTION("getSupportedBands(): 64-bit hex masks (portable parser)");
    {
        FakeModem fm;
        SimcomA76xx modem;
        modem.begin(fm);
        SupportedBands b;
        CHECK(modem.getSupportedBands(&b));
        CHECK(b.gsmMask == 0x180ULL);
        CHECK(b.lteMask == 0x080800C5ULL);
        CHECK(b.gsm900 && b.gsm1800);
        CHECK(b.lteB1 && b.lteB3 && b.lteB7 && b.lteB8 && b.lteB20 && b.lteB28);
    }

    SECTION("isRegistered(): states that can send SMS");
    {
        CHECK(SimcomA76xx::isRegistered(REG_HOME));
        CHECK(SimcomA76xx::isRegistered(REG_ROAMING));
        CHECK(SimcomA76xx::isRegistered(REG_SMS_ONLY_HOME));
        CHECK(SimcomA76xx::isRegistered(REG_SMS_ONLY_ROAMING));
        CHECK(!SimcomA76xx::isRegistered(REG_SEARCHING));
        CHECK(!SimcomA76xx::isRegistered(REG_NOT_REGISTERED));
        CHECK(!SimcomA76xx::isRegistered(REG_DENIED));
        CHECK(!SimcomA76xx::isRegistered(REG_UNKNOWN));
        CHECK(!SimcomA76xx::isRegistered(REG_EMERGENCY_ONLY));
        CHECK(!SimcomA76xx::isRegistered(REG_ERROR));
    }

    SECTION("getRegistrationStatus() parses +CEREG");
    {
        FakeModem fm;
        fm.cereg = 6;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(modem.getRegistrationStatus() == REG_SMS_ONLY_HOME);
    }

    SECTION("isNetworkReady(): LTE registered");
    {
        FakeModem fm;
        fm.cereg = 1; fm.creg = 2;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(modem.isNetworkReady());
    }

    SECTION("isNetworkReady(): fell back to GSM (CREG registered, CEREG not)");
    {
        FakeModem fm;
        fm.cereg = 2; fm.creg = 5;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(modem.isNetworkReady());
    }

    SECTION("isNetworkReady(): still searching");
    {
        FakeModem fm;
        fm.cereg = 2; fm.creg = 2;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.isNetworkReady());
    }

    SECTION("waitForNetwork(): returns once registered");
    {
        FakeModem fm;
        fm.cereg = 2; fm.creg = 2; fm.registerAfter = 5;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(modem.waitForNetwork(60000));
        CHECK(fm.regQueries == 5);
    }

    SECTION("waitForNetwork(): times out");
    {
        FakeModem fm;
        fm.cereg = 2; fm.creg = 0;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.waitForNetwork(5000));
    }

    SECTION("lastError(): NONE after success, also after an earlier failure");
    {
        FakeModem fm;
        fm.sendOk = false;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendSMS("+36301234567", "abc"));
        CHECK(modem.lastError().error == ModemError::CMS_ERROR);
        fm.sendOk = true;
        CHECK(modem.sendSMS("+36301234567", "abc"));
        CHECK(modem.lastError().error == ModemError::NONE);
        CHECK(modem.lastError().code == -1);
    }

    SECTION("lastError(): numeric +CMS ERROR code");
    {
        FakeModem fm;
        fm.sendOk = false;
        fm.sendError = "+CMS ERROR: 1";
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendSMS("+36301234567", "abc"));
        ModemResult r = modem.lastError();
        CHECK(r.error == ModemError::CMS_ERROR && r.code == 1);
        CHECK(SimcomA76xx::isPermanentSmsError(r.code));
    }

    SECTION("lastError(): transient +CMS ERROR (network timeout, 332)");
    {
        FakeModem fm;
        fm.sendOk = false;
        fm.sendError = "+CMS ERROR: 332";
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendSMS("+36301234567", "abc"));
        CHECK(modem.lastError().code == 332);
        CHECK(!SimcomA76xx::isPermanentSmsError(modem.lastError().code));
    }

    SECTION("lastError(): verbose report (CMEE=2) gives code -1");
    {
        FakeModem fm;
        fm.sendOk = false;
        fm.sendError = "+CMS ERROR: Network timeout";
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendSMS("+36301234567", "abc"));
        CHECK(modem.lastError().error == ModemError::CMS_ERROR);
        CHECK(modem.lastError().code == -1);
    }

    SECTION("lastError(): +CME ERROR on AT+CMGF");
    {
        FakeModem fm;
        fm.cmgfOk = false;
        fm.cmgfError = "+CME ERROR: 10";
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendSMS("+36301234567", "abc"));
        CHECK(modem.lastError().error == ModemError::CME_ERROR);
        CHECK(modem.lastError().code == 10);
    }

    SECTION("lastError(): prompt refused (ERROR) vs. prompt never came");
    {
        FakeModem fm;
        fm.promptOk = false;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendSMS("+36301234567", "abc"));
        CHECK(modem.lastError().error == ModemError::AT_ERROR);

        FakeModem fm2;
        fm2.promptSilent = true;
        SimcomA76xx modem2;
        modem2.begin(fm2);
        CHECK(!modem2.sendSMS("+36301234567", "abc"));
        CHECK(modem2.lastError().error == ModemError::NO_PROMPT);
        CHECK(!fm2.wire.empty() && fm2.wire.back() == 27);
    }

    SECTION("lastError(): bad number and missing begin() never reach the modem");
    {
        FakeModem fm;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendSMS("+36 30", "abc"));
        CHECK(modem.lastError().error == ModemError::INVALID_ARGUMENT);
        CHECK(fm.wire.empty());

        SimcomA76xx idle;
        CHECK(!idle.sendSMS("+36301234567", "abc"));
        CHECK(idle.lastError().error == ModemError::NOT_STARTED);
    }

    SECTION("lastError(): sendAT() without an answer is a TIMEOUT");
    {
        FakeModem fm;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(!modem.sendAT("AT+UNKNOWN", "OK", 100));
        CHECK(modem.lastError().error == ModemError::TIMEOUT);
    }

    SECTION("waitForReady(): PB DONE, then numeric error reports (AT+CMEE=1)");
    {
        FakeModem fm;
        fm.boot();
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(modem.waitForReady(20000));
        CHECK(fm.cmeeSet);
        CHECK(modem.lastError().error == ModemError::NONE);

        FakeModem silent;
        SimcomA76xx modem2;
        modem2.begin(silent);
        CHECK(!modem2.waitForReady(2000));
        CHECK(modem2.lastError().error == ModemError::TIMEOUT);
        CHECK(!silent.cmeeSet);
    }

    SECTION("isPermanentSmsError(): destination / PDU refused only");
    {
        const int16_t permanent[] = {1, 8, 10, 21, 28, 29, 30, 304};
        for (int16_t c : permanent) CHECK(SimcomA76xx::isPermanentSmsError(c));
        const int16_t transient[] = {-1, 0, 27, 38, 41, 42, 47, 331, 332, 500};
        for (int16_t c : transient) CHECK(!SimcomA76xx::isPermanentSmsError(c));
    }

    SECTION("multiline query consumes its final OK, also when it arrives late");
    {
        FakeModem fm;
        fm.lateOk = true;
        SimcomA76xx modem;
        modem.begin(fm);
        CHECK(modem.getRegistrationStatus() == REG_HOME);
        // The next command gets no answer: it must not take the query's OK
        CHECK(!modem.sendAT("AT+UNKNOWN", "OK", 100));
        CHECK(modem.lastError().error == ModemError::TIMEOUT);
    }

    SECTION("analyzeSMS() needs no modem");
    {
        SmsInfo i = SimcomA76xx::analyzeSMS("Fagyaszt\xC3\xB3");
        CHECK(i.encoding == SmsEncoding::UCS2 && i.capacity == 70);
    }

    return finish("test_modem");
}
