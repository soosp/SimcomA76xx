#pragma once
/**
 * @file Arduino.h (host test stub)
 * @brief Just enough of the Arduino core to compile SimcomA76xx.cpp on a PC.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#define OUTPUT 1
#define LOW    0
#define HIGH   1

/// Simulated clock: every call advances it, so timeouts always expire.
inline unsigned long millis() { static unsigned long t = 0; return t += 1; }
inline void delay(unsigned long) {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}

#include "Stream.h"
