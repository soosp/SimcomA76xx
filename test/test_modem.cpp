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
    bool promptOk = true;               ///< Answer AT+CMGS with "> "
    bool sendOk   = true;               ///< Answer the PDU with OK
    bool cmgfOk   = true;

    int available() override { return (int)_rx.size(); }
    int read() override {
        if (_rx.empty()) return -1;
        int c = (uint8_t)_rx.front(); _rx.pop_front(); return c;
    }
    size_t write(uint8_t c) override {
        wire.push_back((char)c);
        if (_inPdu) {
            if (c == 26) { _inPdu = false; reply(sendOk ? "\r\n+CMGS: 7\r\n\r\nOK\r\n" : "\r\n+CMS ERROR: 500\r\n\r\nERROR\r\n"); }
            else if (c == 27) { _inPdu = false; }
            else pdu.push_back((char)c);
            return 1;
        }
        if (c == '\r') {
            commands.push_back(_line);
            if (_line == "AT+CMGF=0")               reply(cmgfOk ? "\r\nOK\r\n" : "\r\nERROR\r\n");
            else if (_line == "AT+CNBP?")
                reply("\r\n+CNBP: 0x0000000000000180,0x00000000080800C5\r\n\r\nOK\r\n");
            else if (_line.rfind("AT+CMGS=", 0) == 0) {
                if (promptOk) { reply("\r\n> "); _inPdu = true; }
                else          reply("\r\nERROR\r\n");
            }
            _line.clear();
        } else if (c != 27) {
            _line.push_back((char)c);
        }
        return 1;
    }

private:
    void reply(const char* s) { while (*s) _rx.push_back(*s++); }
    std::deque<char> _rx;
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

    SECTION("analyzeSMS() needs no modem");
    {
        SmsInfo i = SimcomA76xx::analyzeSMS("Fagyaszt\xC3\xB3");
        CHECK(i.encoding == SmsEncoding::UCS2 && i.capacity == 70);
    }

    return finish("test_modem");
}
