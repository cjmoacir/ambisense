#include "ds3231_rtc.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

static constexpr uint8_t DS3231_ADDRESS = 0x68;
static bool available = false;

static uint8_t decToBcd(uint8_t value) {

  return ((value / 10) << 4) | (value % 10);
}

static uint8_t bcdToDec(uint8_t value) {

  return (value >> 4) * 10 + (value & 0x0F);
}

// O DS3231 é lido pela coleta e pelo loop principal (dashboard/log): o acesso
// ao barramento precisa ser exclusivo.
static SemaphoreHandle_t busMutex = nullptr;

struct BusLock {

  BusLock() {
    if (busMutex)
      xSemaphoreTake(busMutex, portMAX_DELAY);
  }

  ~BusLock() {
    if (busMutex)
      xSemaphoreGive(busMutex);
  }
};

void ds3231Begin() {

  if (busMutex == nullptr) {

    busMutex = xSemaphoreCreateMutex();
  }

  Wire1.begin(DS3231_SDA_PIN, DS3231_SCL_PIN);

  // Até 3 tentativas: logo após um reset o DS3231 pode demorar a responder.
  for (uint8_t attempt = 0; attempt < 3 && !available; attempt++) {

    Wire1.beginTransmission(DS3231_ADDRESS);

    available = Wire1.endTransmission() == 0;

    if (!available) {

      delay(20);
    }
  }
}

bool ds3231Available() {
  return available;
}

RtcDateTime ds3231Now() {

  RtcDateTime result{0, 0, 0, 0, 0, 0};
  if (!available) {

    return result;
  }

  BusLock lock;

  Wire1.beginTransmission(DS3231_ADDRESS);

  Wire1.write(0x00);

  if (Wire1.endTransmission(false) != 0 || Wire1.requestFrom(DS3231_ADDRESS, (uint8_t)7) != 7) {

    return result;
  }

  const uint8_t seconds = Wire1.read();
  const uint8_t minutes = Wire1.read();
  const uint8_t hours = Wire1.read();

  Wire1.read();  // dia da semana

  const uint8_t day = Wire1.read();
  const uint8_t month = Wire1.read();
  const uint8_t year = Wire1.read();

  result.second = bcdToDec(seconds & 0x7F);

  result.minute = bcdToDec(minutes & 0x7F);

  result.hour = bcdToDec(hours & 0x3F);

  result.day = bcdToDec(day & 0x3F);

  result.month = bcdToDec(month & 0x1F);

  result.year = 2000 + bcdToDec(year);

  return result;
}

bool ds3231Set(const RtcDateTime &dateTime) {

  if (!available)
    return false;

  BusLock lock;

  Wire1.beginTransmission(DS3231_ADDRESS);
  Wire1.write(0x00);
  Wire1.write(decToBcd(dateTime.second));
  Wire1.write(decToBcd(dateTime.minute));
  Wire1.write(decToBcd(dateTime.hour));
  Wire1.write(1);
  Wire1.write(decToBcd(dateTime.day));
  Wire1.write(decToBcd(dateTime.month));
  Wire1.write(decToBcd(dateTime.year - 2000));

  const bool timeWritten = Wire1.endTransmission() == 0;

  // Limpa o flag OSF (registrador de status 0x0F): indica que o oscilador
  // parou e a hora anterior não era confiável.
  Wire1.beginTransmission(DS3231_ADDRESS);
  Wire1.write(0x0F);
  Wire1.write(0x00);

  const bool statusCleared = Wire1.endTransmission() == 0;

  return timeWritten && statusCleared;
}
