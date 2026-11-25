#include "logger.h"
#include "ds3231_rtc.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

static SemaphoreHandle_t loggerMutex = nullptr;

static const __FlashStringHelper *levelName(LogLevel level) {

  switch (level) {
    case LOG_INFO:
      return F("INFO");

    case LOG_WARN:
      return F("WARN");

    case LOG_ERROR:
      return F("ERROR");

    case LOG_DATA:
      return F("DATA");

    default:
      return F("LOG");
  }
}

void loggerBegin(uint32_t baudRate) {

  Serial.begin(baudRate);

  loggerMutex = xSemaphoreCreateMutex();

  delay(300);
}

void loggerWrite(LogLevel level, const String &module, const String &function,
                 const String &message) {

  char timestamp[24] = "--/--/---- --:--:--";

  bool validTimestamp = false;

  if (ds3231Available()) {

    const RtcDateTime now = ds3231Now();

    if (now.year >= 2020 && now.month >= 1 && now.month <= 12 && now.day >= 1 && now.day <= 31 &&
        now.hour <= 23 && now.minute <= 59 && now.second <= 59) {

      snprintf(timestamp, sizeof(timestamp), "%02u-%02u-%04u %02u:%02u:%02u", now.day, now.month,
               now.year, now.hour, now.minute, now.second);

      validTimestamp = true;
    }
  }

  if (!validTimestamp) {

    struct tm systemTime;

    if (getLocalTime(&systemTime, 0)) {

      snprintf(timestamp, sizeof(timestamp), "%02d-%02d-%04d %02d:%02d:%02d", systemTime.tm_mday,
               systemTime.tm_mon + 1, systemTime.tm_year + 1900, systemTime.tm_hour,
               systemTime.tm_min, systemTime.tm_sec);
    }
  }

  if (loggerMutex != nullptr) {
    xSemaphoreTake(loggerMutex, portMAX_DELAY);
  }

  Serial.print(timestamp);
  Serial.print(F("\t"));
  Serial.print(levelName(level));
  Serial.print(F("\t"));
  Serial.print(module);
  Serial.print(F("\t"));
  Serial.print(function);
  Serial.print(F("\t"));
  Serial.println(message);

  if (loggerMutex != nullptr) {
    xSemaphoreGive(loggerMutex);
  }
}
