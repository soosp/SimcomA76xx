/**
 * @file test_pdu.cpp
 * @brief Whole PDUs against reference encodings.
 */

#include "test_harness.h"

int main() {
    printf("test_pdu\n");

    SECTION("reference: 'hellohello' to +46708251358, 4 days validity");
    {
        // The classic example from the GSM 03.40 literature (AT+CMGS=23).
        CHECK_STR(pdu("+46708251358", "hellohello", 4 * 1440),
                  "0011000B916407281553F80000AA0AE8329BFD4697D9EC37");
        SmsInfo i = SmsPdu::analyze("hellohello");
        CHECK(SmsPdu::tpduLength("+46708251358", i, 4 * 1440) == 23);
    }

    SECTION("no validity: network default, no VP octet");
    {
        CHECK_STR(pdu("+46708251358", "hellohello"),
                  "0001000B916407281553F800000AE8329BFD4697D9EC37");
        SmsInfo i = SmsPdu::analyze("hellohello");
        CHECK(SmsPdu::tpduLength("+46708251358", i, 0) == 22);
    }

    SECTION("even number of digits: no F padding");
    {
        CHECK_STR(pdu("+36301234567", "A").substr(0, 22), "0001000B916303214365F7");
        CHECK_STR(pdu("+3630123456", "A").substr(0, 20),  "0001000A916303214365");
    }

    SECTION("number without '+': type unknown (0x81), sent as dialled");
    {
        // national 06301234567: 11 digits, F-padded
        CHECK_STR(pdu("06301234567", "A").substr(0, 22), "0001000B816003214365F7");
        // operator short code 1777
        CHECK_STR(pdu("1777", "A"), "00010004817177" "0000" "01" "41");
        SmsInfo i = SmsPdu::analyze("A");
        CHECK(SmsPdu::tpduLength("1777", i, 0) == 1 + 1 + 1 + 1 + 2 + 1 + 1 + 1 + 1);
    }

    SECTION("UCS-2: Hungarian text");
    {
        // PID 00, DCS 08, UDL 8 octets, big-endian UTF-16
        CHECK_STR(pdu("+36301234567", "Hűtő"),
                  "0001000B916303214365F7" "0008" "08" "0048017100740151");
    }

    SECTION("UCS-2: characters outside the BMP become surrogate pairs");
    {
        // U+1F600 GRINNING FACE = D83D DE00
        CHECK_STR(pdu("+36301234567", "\xF0\x9F\x98\x80!"),
                  "0001000B916303214365F7" "0008" "06" "D83DDE000021");
    }

    SECTION("GSM 7-bit: '@' is septet 0, '$' is 2, '_' is 0x11");
    {
        CHECK_STR(pdu("+36301234567", "@"), "0001000B916303214365F7" "0000" "01" "00");
        CHECK_STR(pdu("+36301234567", "$"), "0001000B916303214365F7" "0000" "01" "02");
        CHECK_STR(pdu("+36301234567", "_"), "0001000B916303214365F7" "0000" "01" "11");
    }

    SECTION("GSM 7-bit: extension characters take an escape and two septets");
    {
        // € = 1B 65 -> packed 9B 32
        CHECK_STR(pdu("+36301234567", "\xE2\x82\xAC"), "0001000B916303214365F7" "0000" "02" "9B32");
        SmsInfo i = SmsPdu::analyze("a[b]");
        CHECK(i.encoding == SmsEncoding::GSM7 && i.used == 6);
    }

    SECTION("GSM 7-bit: accented letters of the default alphabet");
    {
        // é = 0x05, ö = 0x7C, ü = 0x7E, É = 0x1F
        SmsInfo i = SmsPdu::analyze("\xC3\xA9\xC3\xB6\xC3\xBC\xC3\x89");
        CHECK(i.encoding == SmsEncoding::GSM7 && i.used == 4);
        CHECK_STR(pdu("+36301234567", "\xC3\xA9"), "0001000B916303214365F7" "0000" "01" "05");
    }

    SECTION("validity octet appears after DCS");
    {
        CHECK_STR(pdu("+36301234567", "A", 1440), "0011000B916303214365F7" "0000" "A7" "01" "41");
    }

    return finish("test_pdu");
}
