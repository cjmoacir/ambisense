#pragma once
#include <Arduino.h>

enum LogLevel {

  LOG_INFO,

  LOG_WARN,

  LOG_ERROR,

  LOG_DATA
};

void loggerBegin(uint32_t baudRate);

void loggerWrite(LogLevel level, const String &module, const String &function,
                 const String &message);
