#pragma once

/**
 * @file test_harness.h
 * @brief Assertion helpers for the SmsPdu host tests.
 *
 * SmsPdu.h has no Arduino dependency; it is compiled here as plain C++.
 */

#include "../src/SmsPdu.h"
#include <cstdio>
#include <string>

inline int& testFailures() { static int failures = 0; return failures; }

inline void checkImpl(bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        testFailures()++;
        printf("  FAIL %s:%d  %s\n", file, line, expr);
    }
}

#define CHECK(expr) checkImpl((expr), #expr, __FILE__, __LINE__)
#define SECTION(name) printf("- %s\n", name)

/// Asserts string equality and prints both sides if they differ.
#define CHECK_STR(actual, expected) checkStr((actual), (expected), #actual, __FILE__, __LINE__)
inline void checkStr(const std::string& a, const std::string& e, const char* expr,
                     const char* file, int line) {
    if (a != e) {
        testFailures()++;
        printf("  FAIL %s:%d  %s\n    got      %s\n    expected %s\n",
               file, line, expr, a.c_str(), e.c_str());
    }
}

inline int finish(const char* name) {
    if (testFailures() == 0) printf("%s: OK\n", name);
    else                     printf("%s: %d FAILED\n", name, testFailures());
    return testFailures() == 0 ? 0 : 1;
}

/// The whole PDU as a string, plus a consistency check against tpduLength().
inline std::string pdu(const char* number, const char* text,
                       uint32_t validity = 0, SmsEncoding enc = SmsEncoding::AUTO) {
    std::string out;
    SmsInfo info = SmsPdu::analyze(text, enc);
    bool ok = SmsPdu::write([](char c, void* ctx) { static_cast<std::string*>(ctx)->push_back(c); },
                            &out, number, text, info, validity);
    if (!ok) return "<invalid>";
    // "00" SMSC prefix + 2 hex digits per TPDU octet
    checkImpl(out.size() == 2 + 2u * SmsPdu::tpduLength(number, info, validity),
              "PDU length matches tpduLength()", __FILE__, __LINE__);
    return out;
}

/// The user-data part of a PDU without validity: after the 2-digit UDL.
inline std::string ud(const std::string& pdu, uint8_t numberDigits) {
    // 00 | 01 | 00 | len | 91 | digits | PID | DCS | UDL | UD
    size_t pos = 2 + 2 + 2 + 2 + 2 + ((numberDigits + 1) / 2) * 2 + 2 + 2;
    return pdu.substr(pos);
}
