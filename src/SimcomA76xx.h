/**
 * @file SimcomA76xx.h
 * @author Péter Soós
 * @brief A library for SimCom A76xx series LTE modems.
 * @copyright MIT License
 */

#pragma once

#include <Arduino.h>
#include <Stream.h>

/**
 * @brief Network selection modes for AT+CNMP command.
 */
enum NetworkMode {
    MODE_ERROR     = -1, ///< Library error (communication failed)
    MODE_AUTOMATIC =  2, ///< Automatic mode (GSM or LTE, modem decides)
    MODE_GSM_ONLY  = 13, ///< GSM only
    MODE_LTE_ONLY  = 38  ///< LTE only
};

/**
 * @brief Current active radio technology reported by the modem.
 */
enum SystemMode {
    SYS_MODE_NO_SERVICE =  0, ///< No network service
    SYS_MODE_GSM        =  1, ///< Currently on GSM
    SYS_MODE_LTE        =  2, ///< Currently on LTE
    SYS_MODE_UNKNOWN    = 99  ///< Unknown state or communication error
};

/**
 * @brief Named constants for the currently active single band.
 */
enum ActiveBand {
    BAND_NONE     =    0, ///< No active band (not connected)
    BAND_LTE_B1   =    1, ///< LTE Band 1  (2100 MHz)
    BAND_LTE_B3   =    3, ///< LTE Band 3  (1800 MHz)
    BAND_LTE_B7   =    7, ///< LTE Band 7  (2600 MHz)
    BAND_LTE_B8   =    8, ///< LTE Band 8  (900 MHz)
    BAND_LTE_B20  =   20, ///< LTE Band 20 (800 MHz)
    BAND_LTE_B28  =   28, ///< LTE Band 28 (700 MHz)
    BAND_GSM_900  =  900, ///< GSM 900 MHz
    BAND_GSM_1800 = 1800  ///< GSM 1800 MHz (DCS)
};

/**
 * @brief Frequency band masks for GSM and LTE.
 * Use these with setAllowedBands() and isBandValid().
 */
namespace Bands {
    /**
     * @brief GSM band bitmasks for use with setAllowedBands().
     */
    enum GSM : uint64_t {
        GSM_NONE = 0x0,   ///< No GSM band
        GSM_900  = 0x80,  ///< GSM 900 MHz
        GSM_1800 = 0x100, ///< GSM 1800 MHz (DCS)
        GSM_ALL  = 0x180  ///< All GSM bands
    };

    /**
     * @brief LTE band bitmasks for use with setAllowedBands() and isBandValid().
     */
    enum LTE : uint64_t {
        LTE_NONE = 0x0,           ///< No LTE band
        B1  = 1ULL << 0,          ///< LTE Band 1  (2100 MHz)
        B3  = 1ULL << 2,          ///< LTE Band 3  (1800 MHz)
        B7  = 1ULL << 6,          ///< LTE Band 7  (2600 MHz)
        B8  = 1ULL << 7,          ///< LTE Band 8  (900 MHz)
        B20 = 1ULL << 19,         ///< LTE Band 20 (800 MHz)
        B28 = 1ULL << 27,         ///< LTE Band 28 (700 MHz)
        ALL = 0xFFFFFFFFFFFFFFFFULL ///< All LTE bands
    };
};

/**
 * @brief Network registration states based on 3GPP TS 27.007 (AT+CEREG).
 */
enum RegistrationStatus {
    REG_NOT_REGISTERED = 0,  ///< Not registered, not searching
    REG_HOME           = 1,  ///< Registered on home network
    REG_SEARCHING      = 2,  ///< Not registered, but currently searching
    REG_DENIED         = 3,  ///< Registration denied
    REG_UNKNOWN        = 4,  ///< Unknown state (e.g. out of coverage)
    REG_ROAMING        = 5,  ///< Registered on a roaming partner network
    REG_ERROR          = -1  ///< Library error (communication failed)
};

/**
 * @brief Parsed band configuration returned by getSupportedBands().
 *
 * Contains both the raw bitmasks and pre-evaluated boolean flags for
 * the most commonly used bands. The raw masks are included for advanced
 * users who need to check bands not yet mapped to boolean flags.
 */
struct SupportedBands {
    uint64_t gsmMask; ///< Raw bitmask of enabled GSM bands
    uint64_t lteMask; ///< Raw bitmask of enabled LTE bands
    bool lteB1;       ///< true if LTE Band 1  (2100 MHz) is enabled
    bool lteB3;       ///< true if LTE Band 3  (1800 MHz) is enabled
    bool lteB7;       ///< true if LTE Band 7  (2600 MHz) is enabled
    bool lteB8;       ///< true if LTE Band 8  (900 MHz)  is enabled
    bool lteB20;      ///< true if LTE Band 20 (800 MHz)  is enabled
    bool lteB28;      ///< true if LTE Band 28 (700 MHz)  is enabled
    bool gsm900;      ///< true if GSM 900 MHz is enabled
    bool gsm1800;     ///< true if GSM 1800 MHz (DCS) is enabled
};

class SimcomA76xx {
private:
    Stream* _serial;
    char _lineBuf[256];
    int8_t _pwrPin;

    /**
     * @brief Reads one line from the serial port into _lineBuf.
     * Strips trailing \\r\\n. Returns false on timeout.
     */
    bool readLine(uint32_t timeout = 1000);

    /**
     * @brief Waits for a specific response line after an AT command was sent.
     * Returns false immediately on ERROR or +CME ERROR responses.
     * @param expected Exact string to match against incoming lines.
     * @param timeout Maximum wait time in milliseconds.
     * @return true if expected line was received, false on error or timeout.
     */
    bool waitForResponse(const char* expected, uint32_t timeout);

    /**
     * @brief Platform-aware delay. Uses vTaskDelay on ESP32, delay() elsewhere.
     * @param ms Delay duration in milliseconds.
     */
    void _wait(uint32_t ms);

    /**
     * @brief Converts a uint64_t band mask to a hex string for AT+CNBP command.
     * @param val Value to convert.
     * @param out Output buffer (at least 20 bytes).
     */
    void uint64ToHex(uint64_t val, char* out);

    /**
     * @brief Extracts the first quoted string from _lineBuf into buf.
     * Used internally to parse AT responses like +COPS: 0,0,"Telekom HU",7.
     * @param buf Buffer to store the extracted string.
     * @param maxLen Maximum length of the buffer including null terminator.
     * @return true if a quoted field was found and copied successfully.
     */
    bool parseQuotedField(char* buf, uint8_t maxLen);

public:
    SimcomA76xx();

    /**
     * @brief Initializes the modem communication.
     * @param serialPort Reference to a Stream object (e.g., Serial2 or SafeSerial).
     * @param pwrPin GPIO pin connected to the modem Power Key. Pass -1 if not used.
     */
    void begin(Stream& serialPort, int8_t pwrPin = -1);

    /**
     * @brief Sends a hardware pulse on the power pin to toggle the modem power state.
     * Has no effect if no power pin was configured in begin().
     */
    void hwRestart();

    /**
     * @brief Blocks until the modem signals it is ready by sending 'PB DONE'.
     * @param timeoutMs Maximum time to wait in milliseconds.
     * @return true if the modem became ready within the timeout, false otherwise.
     */
    bool waitForReady(uint32_t timeoutMs = 20000);

    /**
     * @brief Sends a raw AT command and waits for a specific response.
     * @param command The AT command string to send (e.g. "AT+CMGF=1").
     * @param expectedResponse In normal mode: the exact response line to match (e.g. "OK").
     *                         In multiline mode: a substring prefix to search for (e.g. "+CSQ:").
     * @param timeout Maximum time to wait for the response in milliseconds.
     * @param multiline If true, searches for expectedResponse as a substring and
     *                  leaves the matched line in _lineBuf for the caller to parse.
     * @return true if the expected response was found, false on ERROR or timeout.
     */
    bool sendAT(const char* command, const char* expectedResponse = "OK",
                uint32_t timeout = 1000, bool multiline = false);

    /**
     * @brief Sets the preferred network mode.
     * @param mode One of the NetworkMode enum values (e.g. MODE_LTE_ONLY).
     * @return true if the modem accepted the new mode, false on error.
     */
    bool setAllowedModes(uint8_t mode);

    /**
     * @brief Restricts the modem to specific frequency bands.
     * @param gsmMask GSM band mask built from Bands::GSM constants.
     * @param lteMask LTE band mask built from Bands::LTE constants.
     * @return true if the modem accepted the new band configuration, false on error.
     * @note Band changes may require a radio cycle to take effect. If the new
     *       configuration is not applied, try to call forceReattach() after this method.
     */
    bool setAllowedBands(uint64_t gsmMask, uint64_t lteMask);

    /**
     * @brief Configures the APN for cellular data connectivity.
     * @param apn The Access Point Name string.
     * @param user Optional username for authenticated APNs.
     * @param pwd Optional password for authenticated APNs.
     * @return true if the APN was configured successfully, false on error.
     */
    bool setAPN(const char* apn, const char* user = "", const char* pwd = "");

    /**
     * @brief Reads and parses the current band configuration from the modem.
     * @param bands Pointer to a SupportedBands struct to be filled.
     * @return true if the band configuration was read successfully, false on error.
     */
    bool getSupportedBands(SupportedBands* bands);

    /**
     * @brief Returns the preferred network mode configured in the modem.
     * @return The current NetworkMode, or MODE_ERROR on communication failure.
     */
    NetworkMode getMode();

    /**
     * @brief Returns the radio technology currently in use by the modem.
     * Useful in Automatic mode to detect whether the modem fell back to GSM.
     * @return SYS_MODE_LTE, SYS_MODE_GSM, SYS_MODE_NO_SERVICE, or SYS_MODE_UNKNOWN.
     */
    SystemMode getCurrentMode();

    /**
     * @brief Returns the currently active frequency band.
     * @return An ActiveBand constant (e.g. BAND_LTE_B8), or BAND_NONE if not connected.
     */
    ActiveBand getCurrentBand();

    /**
     * @brief Reads the current signal quality (RSSI) via AT+CSQ.
     * @return Signal strength value 0–31 (higher is better), or 99 if unknown.
     */
    int getSignalQuality();

    /**
     * @brief Checks whether the modem is currently active on a specific LTE band.
     * @param targetBand The LTE band to verify (e.g. Bands::B8).
     * @return true if the modem is currently attached to the specified band.
     */
    bool isBandValid(Bands::LTE targetBand);

    /**
     * @brief Queries the network registration status via AT+CEREG.
     * @return A RegistrationStatus value, or REG_ERROR on communication failure.
     */
    RegistrationStatus getRegistrationStatus();

    /**
     * @brief Retrieves the network operator name in alphanumeric format.
     * @param nameBuf Buffer to store the operator name (e.g. "Telekom HU").
     * @param maxLen Maximum length of the buffer including null terminator.
     * @return true if the operator name was retrieved successfully, false on error.
     */
    bool getOperatorName(char* nameBuf, uint8_t maxLen);

    /**
     * @brief Retrieves the numeric PLMN code (MCC+MNC) of the current operator.
     * @param buf Buffer to store the code (at least 7 bytes recommended, e.g. "21630").
     * @param maxLen Maximum length of the buffer including null terminator.
     * @return true if the operator code was retrieved successfully, false on error.
     */
    bool getOperatorCode(char* buf, uint8_t maxLen);

    /**
     * @brief Retrieves the Cell ID of the current serving base station.
     * @return The Cell ID as a 32-bit integer, or 0 if unavailable.
     */
    uint32_t getCellId();

    /**
     * @brief Sends a plain text SMS message in Text Mode (AT+CMGF=1).
     * @param phoneNumber Destination number in international format (e.g. "+3630...").
     * @param message The message text to send.
     * @return true if the message was accepted by the network, false on error.
     */
    bool sendSMS(const char* phoneNumber, const char* message);

    /**
     * @brief Forces the modem to detach and re-attach to the network (CFUN 0/1).
     * Useful when the modem is stuck or needs to find a new cell.
     * @return true if both radio-off and radio-on commands succeeded, false on error.
     */
    bool forceReattach();
};