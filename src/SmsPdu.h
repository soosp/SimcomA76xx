/**
 * @file SmsPdu.h
 * @author Péter Soós
 * @brief SMS-SUBMIT PDU encoder (3GPP TS 23.040 / 23.038): UTF-8 in,
 *        GSM 7-bit or UCS-2 out, chosen automatically.
 * @copyright MIT License
 *
 * -----------------------------------------------------------------------------
 * WHY PDU MODE
 * -----------------------------------------------------------------------------
 *
 * In text mode the character set is whatever AT+CSCS says, and none of the
 * choices sends GSM 7-bit text reliably: "IRA" is ASCII only (no é, ö, ü),
 * and "GSM" means raw GSM bytes, where '@' is 0x00, 'Ξ' is 0x1A — the Ctrl+Z
 * that ends the message — and the extension characters need an 0x1B escape,
 * all handled differently by different modems. In PDU mode the message is
 * built here, bit for bit, and the modem only forwards it.
 *
 * -----------------------------------------------------------------------------
 * ENCODING
 * -----------------------------------------------------------------------------
 *
 * AUTO picks GSM 7-bit when every character is in the GSM default alphabet or
 * its extension table (€ [ ] { } ~ ^ \ | and form feed, which take two
 * septets), and UCS-2 otherwise. One SMS holds 160 septets or 70 UTF-16
 * units; a character outside the BMP (an emoji) takes two units. Longer text
 * is cut at a character boundary — never in the middle of an escape sequence
 * or a surrogate pair — and the cut is reported. Invalid UTF-8 becomes '?'
 * (GSM 7-bit) or U+FFFD (UCS-2).
 *
 * -----------------------------------------------------------------------------
 * OUTPUT
 * -----------------------------------------------------------------------------
 *
 * The PDU is written as hex digits through a callback, one character at a
 * time, so no buffer for it is needed (about 350 characters at most). Call
 * analyze() first for the TPDU length that AT+CMGS needs; write() then emits
 * exactly that PDU, prefixed with the "00" that selects the SIM's SMSC.
 *
 * No Arduino dependency: the whole encoder builds and is tested on a PC.
 * No dynamic allocation; tables live in flash on AVR.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Flash tables: pgm_read_word() wherever the core provides it (needed on AVR,
// and on ESP8266, where flash allows only aligned 32-bit access); plain reads
// in host builds.
#if defined(ARDUINO)
    #include <Arduino.h>
#endif
#if defined(pgm_read_word)
    #define SMSPDU_READ_U16(p) pgm_read_word(p)
#else
    #define SMSPDU_READ_U16(p) (*(p))
#endif
#ifndef PROGMEM
    #define PROGMEM
#endif

/**
 * @brief Character encoding of an SMS.
 */
enum class SmsEncoding : uint8_t {
    AUTO = 0, ///< GSM 7-bit if every character fits, UCS-2 otherwise
    GSM7 = 1, ///< GSM 7-bit default alphabet; other characters become '?'
    UCS2 = 2  ///< UCS-2 (UTF-16)
};

/**
 * @brief What an SMS text turns into.
 */
struct SmsInfo {
    SmsEncoding encoding;  ///< Encoding used (never AUTO)
    uint16_t    used;      ///< Septets (GSM 7-bit) or UTF-16 units (UCS-2) sent
    uint16_t    capacity;  ///< 160 or 70
    uint16_t    inputUsed; ///< Bytes of the UTF-8 input that fit
    bool        truncated; ///< Input did not fit and was cut
};

/**
 * @brief SMS-SUBMIT PDU encoder.
 */
class SmsPdu {
public:
    /// Septets in one GSM 7-bit SMS.
    static const uint16_t GSM7_CAPACITY = 160;
    /// UTF-16 units in one UCS-2 SMS.
    static const uint16_t UCS2_CAPACITY = 70;
    /// Longest destination number, in digits (a leading '+' not counted).
    static const uint8_t  MAX_NUMBER_DIGITS = 20;
    /// Longest validity period that can be encoded: 63 weeks, in minutes.
    static const uint32_t MAX_VALIDITY_MINUTES = 63UL * 7 * 24 * 60;

    /// Receives the PDU one hex digit at a time.
    typedef void (*Sink)(char c, void* ctx);

    // =========================================================================
    // ANALYSIS
    // =========================================================================

    /**
     * @brief Decide the encoding and how much of @p utf8 fits in one SMS.
     *
     * @param utf8  Message text, UTF-8, null-terminated (nullptr = empty).
     * @param enc   AUTO, or a forced encoding.
     */
    static SmsInfo analyze(const char* utf8, SmsEncoding enc = SmsEncoding::AUTO) {
        if (utf8 == nullptr) utf8 = "";
        if (enc == SmsEncoding::AUTO) enc = _fitsGsm7(utf8) ? SmsEncoding::GSM7 : SmsEncoding::UCS2;

        SmsInfo info;
        info.encoding  = enc;
        info.capacity  = (enc == SmsEncoding::GSM7) ? GSM7_CAPACITY : UCS2_CAPACITY;
        info.used      = 0;
        info.inputUsed = 0;
        info.truncated = false;

        const char* p = utf8;
        while (*p) {
            const char* start = p;
            uint32_t cp = nextCodepoint(p);
            uint16_t cost = (enc == SmsEncoding::GSM7) ? _gsm7Cost(cp) : (cp > 0xFFFF ? 2 : 1);
            if (info.used + cost > info.capacity) { info.truncated = true; p = start; break; }
            info.used += cost;
        }
        info.inputUsed = (uint16_t)(p - utf8);
        return info;
    }

    // =========================================================================
    // NUMBER AND VALIDITY
    // =========================================================================

    /**
     * @brief Check a destination number: an optional '+', then 1..20 digits,
     *        nothing else.
     *
     * With '+' the number is sent as international (type 0x91); without it as
     * "unknown" (0x81), which the network interprets as if dialled — the form
     * for national numbers and operator short codes.
     */
    static bool validNumber(const char* number) {
        if (number == nullptr) return false;
        size_t n = 0;
        for (const char* p = _digits(number); *p; p++, n++) {
            if (*p < '0' || *p > '9') return false;
        }
        return n >= 1 && n <= MAX_NUMBER_DIGITS;
    }

    /**
     * @brief Relative validity period octet (TS 23.040 9.2.3.12.1) for at
     *        least @p minutes.
     *
     * Rounded up to the next value the field can express: 5-minute steps to
     * 12 h, 30-minute steps to 24 h, days to 30 days, weeks to 63 weeks
     * (longer is clamped).
     *
     * @param minutes  Validity in minutes, > 0.
     */
    static uint8_t validityOctet(uint32_t minutes) {
        if (minutes <= 5)          return 0;
        if (minutes <= 12UL * 60)  return (uint8_t)(_ceilDiv(minutes, 5) - 1);
        if (minutes <= 24UL * 60)  return (uint8_t)(143 + _ceilDiv(minutes - 12UL * 60, 30));
        if (minutes <= 30UL * 1440) {
            uint32_t days = _ceilDiv(minutes, 1440);
            return (uint8_t)(166 + (days < 2 ? 2 : days));
        }
        uint32_t weeks = _ceilDiv(minutes, 10080);
        if (weeks < 5)  weeks = 5;
        if (weeks > 63) weeks = 63;
        return (uint8_t)(192 + weeks);
    }

    // =========================================================================
    // PDU
    // =========================================================================

    /**
     * @brief TPDU length in octets, as AT+CMGS=<length> expects it (the SMSC
     *        octet is not counted).
     *
     * @param number           Destination, validNumber() must hold.
     * @param info             Result of analyze() for the text to send.
     * @param validityMinutes  0: no validity field (network default).
     */
    static uint16_t tpduLength(const char* number, const SmsInfo& info,
                               uint32_t validityMinutes) {
        uint8_t digits = (uint8_t)strlen(_digits(number));
        uint16_t len = 1 + 1                 // first octet, message reference
                     + 1 + 1 + (digits + 1) / 2 // address length, type, digits
                     + 1 + 1;                // PID, DCS
        if (validityMinutes > 0) len += 1;
        len += 1;                            // UDL
        len += _udOctets(info);
        return len;
    }

    /**
     * @brief Emit the PDU as hex digits, SMSC prefix "00" included.
     *
     * @param sink             Receives each hex digit.
     * @param ctx              Passed to @p sink.
     * @param number           Destination, validNumber() must hold.
     * @param utf8             The same text that was passed to analyze().
     * @param info             Result of analyze().
     * @param validityMinutes  0: no validity field (network default).
     * @return false if the number is invalid (nothing emitted).
     */
    static bool write(Sink sink, void* ctx, const char* number, const char* utf8,
                      const SmsInfo& info, uint32_t validityMinutes) {
        if (!validNumber(number)) return false;
        if (utf8 == nullptr) utf8 = "";
        Out o(sink, ctx);

        o.byte(0x00);                                   // SMSC: use the SIM's
        o.byte(validityMinutes > 0 ? 0x11 : 0x01);      // SMS-SUBMIT, VPF relative / none
        o.byte(0x00);                                   // message reference: modem assigns

        const char* d = _digits(number);
        uint8_t digits = (uint8_t)strlen(d);
        o.byte(digits);
        // ISDN numbering plan; type international with '+', unknown without
        o.byte(number[0] == '+' ? 0x91 : 0x81);
        for (uint8_t i = 0; i < digits; i += 2) {
            uint8_t lo = (uint8_t)(d[i] - '0');
            uint8_t hi = (i + 1 < digits) ? (uint8_t)(d[i + 1] - '0') : 0x0F;
            o.byte((uint8_t)((hi << 4) | lo));
        }

        o.byte(0x00);                                   // PID
        o.byte(info.encoding == SmsEncoding::UCS2 ? 0x08 : 0x00);  // DCS
        if (validityMinutes > 0) o.byte(validityOctet(validityMinutes));

        if (info.encoding == SmsEncoding::UCS2) {
            o.byte((uint8_t)(info.used * 2));          // UDL: octets
            const char* p   = utf8;
            const char* end = utf8 + info.inputUsed;
            while (p < end) {
                uint32_t cp = nextCodepoint(p);
                if (cp > 0xFFFF) {
                    cp -= 0x10000;
                    _u16(o, (uint16_t)(0xD800 | (cp >> 10)));
                    _u16(o, (uint16_t)(0xDC00 | (cp & 0x3FF)));
                } else {
                    _u16(o, (uint16_t)cp);
                }
            }
        } else {
            o.byte((uint8_t)info.used);                // UDL: septets
            Packer pk(o);
            const char* p   = utf8;
            const char* end = utf8 + info.inputUsed;
            while (p < end) {
                uint32_t cp = nextCodepoint(p);
                int16_t  c  = gsm7Code(cp);
                if (c >= 0) { pk.septet((uint8_t)c); continue; }
                c = gsm7ExtCode(cp);
                if (c >= 0) { pk.septet(0x1B); pk.septet((uint8_t)c); continue; }
                pk.septet(0x3F);                       // '?'
            }
            pk.flush();
        }
        return true;
    }

    // =========================================================================
    // CHARACTER HELPERS
    // =========================================================================

    /**
     * @brief Decode one UTF-8 character and advance @p p past it.
     *
     * Overlong forms, surrogates, values above U+10FFFF and broken sequences
     * decode to U+FFFD; a broken sequence consumes one byte.
     */
    static uint32_t nextCodepoint(const char*& p) {
        const uint8_t* s = (const uint8_t*)p;
        uint8_t  b = s[0];
        uint32_t cp;
        uint8_t  extra;
        if (b < 0x80)                { p += 1; return b; }
        else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; extra = 1; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; extra = 2; }
        else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; extra = 3; }
        else                         { p += 1; return 0xFFFD; }
        for (uint8_t i = 1; i <= extra; i++) {
            if ((s[i] & 0xC0) != 0x80) { p += 1; return 0xFFFD; }
            cp = (cp << 6) | (s[i] & 0x3F);
        }
        static const uint32_t MIN[4] = { 0, 0x80, 0x800, 0x10000 };
        if (cp < MIN[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            p += 1;
            return 0xFFFD;
        }
        p += 1 + extra;
        return cp;
    }

    /// GSM 7-bit code of @p cp in the default alphabet, or -1.
    static int16_t gsm7Code(uint32_t cp) {
        if (cp > 0xFFFF) return -1;
        // Fast path: most of printable ASCII maps to itself. '$' (0x24) and
        // '@' (0x40) are the exceptions: the GSM codes 0x24 and 0x40 are ¤
        // and ¡, and '$' and '@' live at 0x02 and 0x00.
        if ((cp >= 0x20 && cp <= 0x5A && cp != 0x24 && cp != 0x40) ||
            (cp >= 0x61 && cp <= 0x7A) || cp == 0x0A || cp == 0x0D) {
            return (int16_t)cp;
        }
        for (uint8_t i = 0; i < 128; i++) {
            if (SMSPDU_READ_U16(&_basicTable()[i]) == cp && i != 0x1B) return i;
        }
        return -1;
    }

    /// Code of @p cp in the GSM 7-bit extension table (after 0x1B), or -1.
    static int16_t gsm7ExtCode(uint32_t cp) {
        switch (cp) {
        case 0x0C:   return 0x0A;   // form feed
        case '^':    return 0x14;
        case '{':    return 0x28;
        case '}':    return 0x29;
        case '\\':   return 0x2F;
        case '[':    return 0x3C;
        case '~':    return 0x3D;
        case ']':    return 0x3E;
        case '|':    return 0x40;
        case 0x20AC: return 0x65;   // €
        default:     return -1;
        }
    }

private:
    // =========================================================================
    // HELPERS
    // =========================================================================

    static uint32_t _ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

    /// The digits of a number: past the '+', if there is one.
    static const char* _digits(const char* number) {
        return number[0] == '+' ? number + 1 : number;
    }

    /// Septets @p cp takes in GSM 7-bit: 1, 2 (extension), or 1 for the '?'
    /// that replaces an unmappable character.
    static uint16_t _gsm7Cost(uint32_t cp) {
        if (gsm7Code(cp) >= 0) return 1;
        if (gsm7ExtCode(cp) >= 0)   return 2;
        return 1;
    }

    static bool _fitsGsm7(const char* utf8) {
        const char* p = utf8;
        while (*p) {
            uint32_t cp = nextCodepoint(p);
            if (gsm7Code(cp) < 0 && gsm7ExtCode(cp) < 0) return false;
        }
        return true;
    }

    static uint16_t _udOctets(const SmsInfo& info) {
        if (info.encoding == SmsEncoding::UCS2) return (uint16_t)(info.used * 2);
        return (uint16_t)((info.used * 7 + 7) / 8);
    }

    /// Hex writer over the sink.
    struct Out {
        Sink  sink;
        void* ctx;
        Out(Sink s, void* c) : sink(s), ctx(c) {}
        void byte(uint8_t b) {
            static const char HEX_DIGITS[] = "0123456789ABCDEF";
            sink(HEX_DIGITS[b >> 4], ctx);
            sink(HEX_DIGITS[b & 0x0F], ctx);
        }
    };

    static void _u16(Out& o, uint16_t v) { o.byte((uint8_t)(v >> 8)); o.byte((uint8_t)v); }

    /// Packs septets LSB-first into octets (TS 23.038 6.1.2.1.1).
    struct Packer {
        Out&     out;
        uint16_t acc;
        uint8_t  bits;
        explicit Packer(Out& o) : out(o), acc(0), bits(0) {}
        void septet(uint8_t s) {
            acc |= (uint16_t)(s & 0x7F) << bits;
            bits += 7;
            while (bits >= 8) {
                out.byte((uint8_t)acc);
                acc >>= 8;
                bits -= 8;
            }
        }
        void flush() {
            if (bits > 0) out.byte((uint8_t)acc);
            acc = 0;
            bits = 0;
        }
    };

    /// GSM 03.38 default alphabet: Unicode code point of each 7-bit code.
    /// 0x1B is the escape to the extension table (no character of its own).
    static const uint16_t* _basicTable() {
        static const uint16_t t[128] PROGMEM = {
            0x0040, 0x00A3, 0x0024, 0x00A5, 0x00E8, 0x00E9, 0x00F9, 0x00EC,
            0x00F2, 0x00C7, 0x000A, 0x00D8, 0x00F8, 0x000D, 0x00C5, 0x00E5,
            0x0394, 0x005F, 0x03A6, 0x0393, 0x039B, 0x03A9, 0x03A0, 0x03A8,
            0x03A3, 0x0398, 0x039E, 0xFFFF, 0x00C6, 0x00E6, 0x00DF, 0x00C9,
            0x0020, 0x0021, 0x0022, 0x0023, 0x00A4, 0x0025, 0x0026, 0x0027,
            0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
            0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
            0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
            0x00A1, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
            0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
            0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
            0x0058, 0x0059, 0x005A, 0x00C4, 0x00D6, 0x00D1, 0x00DC, 0x00A7,
            0x00BF, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
            0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
            0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
            0x0078, 0x0079, 0x007A, 0x00E4, 0x00F6, 0x00F1, 0x00FC, 0x00E0
        };
        return t;
    }
};
