/**
 * @file test_analyze.cpp
 * @brief Encoding choice, capacity, truncation, UTF-8 decoding, numbers,
 *        validity encoding.
 */

#include "test_harness.h"

static std::string rep(const char* s, int n) { std::string r; while (n--) r += s; return r; }

int main() {
    printf("test_analyze\n");

    SECTION("AUTO: GSM 7-bit when everything fits, UCS-2 otherwise");
    {
        CHECK(SmsPdu::analyze("Freezer alarm: -12.5 C").encoding == SmsEncoding::GSM7);
        CHECK(SmsPdu::analyze("K\xC3\xBChlraum").encoding == SmsEncoding::GSM7);   // ü
        CHECK(SmsPdu::analyze("Fagyaszt\xC3\xB3").encoding == SmsEncoding::UCS2);  // ó
        CHECK(SmsPdu::analyze("H\xC5\xB1t\xC5\x91").encoding == SmsEncoding::UCS2); // ű ő
        CHECK(SmsPdu::analyze("`").encoding == SmsEncoding::UCS2);                  // not in GSM
        CHECK(SmsPdu::analyze("").encoding == SmsEncoding::GSM7);
        CHECK(SmsPdu::analyze(nullptr).used == 0);
    }

    SECTION("forced encodings");
    {
        SmsInfo i = SmsPdu::analyze("abc", SmsEncoding::UCS2);
        CHECK(i.encoding == SmsEncoding::UCS2 && i.used == 3 && i.capacity == 70);
        i = SmsPdu::analyze("\xC5\x91", SmsEncoding::GSM7);                     // ő -> '?'
        CHECK(i.encoding == SmsEncoding::GSM7 && i.used == 1);
        CHECK_STR(pdu("+36301234567", "\xC5\x91", 0, SmsEncoding::GSM7).substr(26), "013F");
    }

    SECTION("GSM 7-bit capacity: 160 septets");
    {
        std::string s = rep("a", 160);
        SmsInfo i = SmsPdu::analyze(s.c_str());
        CHECK(i.used == 160 && !i.truncated && i.inputUsed == 160);
        s += "b";
        i = SmsPdu::analyze(s.c_str());
        CHECK(i.used == 160 && i.truncated && i.inputUsed == 160);
    }

    SECTION("GSM 7-bit: an escape pair is never split");
    {
        std::string s = rep("a", 159) + "\xE2\x82\xAC";                        // 159 + €(2)
        SmsInfo i = SmsPdu::analyze(s.c_str());
        CHECK(i.used == 159 && i.truncated && i.inputUsed == 159);
        s = rep("a", 158) + "\xE2\x82\xAC";
        i = SmsPdu::analyze(s.c_str());
        CHECK(i.used == 160 && !i.truncated);
        std::string p = pdu("+36301234567", s.c_str());
        CHECK(p.size() == 2 + 2u * (1 + 1 + 1 + 1 + 6 + 1 + 1 + 1 + 140));      // 160 septets = 140 octets
    }

    SECTION("UCS-2 capacity: 70 units; a surrogate pair is never split");
    {
        std::string s = rep("\xC5\x91", 70);
        SmsInfo i = SmsPdu::analyze(s.c_str());
        CHECK(i.used == 70 && !i.truncated);
        s += "\xC5\x91";
        i = SmsPdu::analyze(s.c_str());
        CHECK(i.used == 70 && i.truncated && i.inputUsed == 140);
        s = rep("\xC5\x91", 69) + "\xF0\x9F\x98\x80";                            // 69 + emoji(2)
        i = SmsPdu::analyze(s.c_str());
        CHECK(i.used == 69 && i.truncated && i.inputUsed == 138);
    }

    SECTION("truncated text is cut at a character boundary in the PDU too");
    {
        std::string s = rep("\xC5\x91", 71);
        std::string p = pdu("+36301234567", s.c_str());
        CHECK_STR(p.substr(22, 6), "00088C");                                  // UDL 140
        CHECK(p.size() == 28 + 280);
    }

    SECTION("invalid UTF-8");
    {
        const char* p;
        p = "\xC3";        CHECK(SmsPdu::nextCodepoint(p) == 0xFFFD);          // truncated sequence
        p = "\xC0\xAF";    CHECK(SmsPdu::nextCodepoint(p) == 0xFFFD);          // overlong '/'
        p = "\xED\xA0\x80"; CHECK(SmsPdu::nextCodepoint(p) == 0xFFFD);         // surrogate
        p = "\xF4\x90\x80\x80"; CHECK(SmsPdu::nextCodepoint(p) == 0xFFFD);     // > U+10FFFF
        p = "\xFF";        CHECK(SmsPdu::nextCodepoint(p) == 0xFFFD);
        p = "\xC3\xA9";    CHECK(SmsPdu::nextCodepoint(p) == 0xE9 && *p == '\0');
        // A broken byte costs one character and does not swallow the next
        SmsInfo i = SmsPdu::analyze("\xC3" "A");
        CHECK(i.encoding == SmsEncoding::UCS2 && i.used == 2);
        CHECK_STR(pdu("+36301234567", "\xC3" "A").substr(26), "04FFFD0041");
    }

    SECTION("numbers: optional '+', then 1..20 digits");
    {
        CHECK(SmsPdu::validNumber("+36301234567"));
        CHECK(SmsPdu::validNumber("+1"));
        CHECK(SmsPdu::validNumber("+12345678901234567890"));
        CHECK(SmsPdu::validNumber("06301234567"));                             // national
        CHECK(SmsPdu::validNumber("1777"));                                    // short code
        CHECK(SmsPdu::validNumber("12345678901234567890"));
        CHECK(!SmsPdu::validNumber("+123456789012345678901"));                 // 21 digits
        CHECK(!SmsPdu::validNumber("123456789012345678901"));
        CHECK(!SmsPdu::validNumber("+36 30 123 4567"));
        CHECK(!SmsPdu::validNumber("+36-30"));
        CHECK(!SmsPdu::validNumber("06/30"));
        CHECK(!SmsPdu::validNumber("++3630"));
        CHECK(!SmsPdu::validNumber("3630+"));
        CHECK(!SmsPdu::validNumber("*100#"));
        CHECK(!SmsPdu::validNumber("+"));
        CHECK(!SmsPdu::validNumber(""));
        CHECK(!SmsPdu::validNumber(nullptr));
        CHECK_STR(pdu("06 30", "x"), "<invalid>");
    }

    SECTION("validity: rounded up to what the octet can express");
    {
        CHECK(SmsPdu::validityOctet(1) == 0);                // 5 min
        CHECK(SmsPdu::validityOctet(5) == 0);
        CHECK(SmsPdu::validityOctet(6) == 1);                // 10 min
        CHECK(SmsPdu::validityOctet(60) == 11);              // 1 h
        CHECK(SmsPdu::validityOctet(720) == 143);            // 12 h
        CHECK(SmsPdu::validityOctet(721) == 144);            // 12 h 30
        CHECK(SmsPdu::validityOctet(1440) == 167);           // 24 h
        CHECK(SmsPdu::validityOctet(1441) == 168);           // 2 days
        CHECK(SmsPdu::validityOctet(4 * 1440) == 170);       // 4 days (0xAA)
        CHECK(SmsPdu::validityOctet(30 * 1440) == 196);      // 30 days
        CHECK(SmsPdu::validityOctet(30 * 1440 + 1) == 197);  // 5 weeks
        CHECK(SmsPdu::validityOctet(SmsPdu::MAX_VALIDITY_MINUTES) == 255);
        CHECK(SmsPdu::validityOctet(0xFFFFFFFFUL / 2) == 255);
    }

    SECTION("the whole GSM 7-bit default alphabet round-trips");
    {
        // Every table entry except the escape maps back to its own code.
        int ok = 0;
        for (int c = 0; c < 128; c++) {
            if (c == 0x1B) continue;
            // find a code point that maps to c by brute force over the BMP
            for (uint32_t cp = 0; cp < 0x0400; cp++) {
                if (SmsPdu::gsm7Code(cp) == c) { ok++; break; }
            }
        }
        CHECK(ok == 127);
    }

    return finish("test_analyze");
}
