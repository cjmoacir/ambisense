#include "ds3231_rtc.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

static constexpr uint8_t DS3231_ADDRESS = 0x68;
static bool available = false;
static uint32_t lastProbeAt = 0;

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

  Wire.begin(DS3231_SDA_PIN, DS3231_SCL_PIN);

  // Até 3 tentativas: logo após um reset o DS3231 pode demorar a responder.
  for (uint8_t attempt = 0; attempt < 3 && !available; attempt++) {

    Wire.beginTransmission(DS3231_ADDRESS);

    available = Wire.endTransmission() == 0;
    lastProbeAt = millis();

    if (!available) {

      delay(20);
    }
  }
}

static void retryConnectionIfNeeded() {

  if (available || millis() - lastProbeAt < 5000) {
    return;
  }

  lastProbeAt = millis();
  Wire.beginTransmission(DS3231_ADDRESS);
  available = Wire.endTransmission() == 0;
}

bool ds3231Available() {
  return available;
}

RtcDateTime ds3231Now() {

  retryConnectionIfNeeded();

  RtcDateTime result{0, 0, 0, 0, 0, 0};
  if (!available) {

    return result;
  }

  BusLock lock;

  Wire.beginTransmission(DS3231_ADDRESS);

  Wire.write(0x00);

  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(DS3231_ADDRESS, (uint8_t)7) != 7) {

    return result;
  }

  const uint8_t seconds = Wire.read();
  const uint8_t minutes = Wire.read();
  const uint8_t hours = Wire.read();

  Wire.read();  // dia da semana

  const uint8_t day = Wire.read();
  const uint8_t month = Wire.read();
  const uint8_t year = Wire.read();

  result.second = bcdToDec(seconds & 0x7F);

  result.minute = bcdToDec(minutes & 0x7F);

  result.hour = bcdToDec(hours & 0x3F);

  result.day = bcdToDec(day & 0x3F);

  result.month = bcdToDec(month & 0x1F);

  result.year = 2000 + bcdToDec(year);

  if (result.year < 2020 || result.month < 1 || result.month > 12 ||
      result.day < 1 || result.day > 31 || result.hour > 23 ||
      result.minute > 59 || result.second > 59) {
    return RtcDateTime{0, 0, 0, 0, 0, 0};
  }

  return result;
}

bool ds3231Set(const RtcDateTime &dateTime) {

  retryConnectionIfNeeded();

  if (!available)
    return false;

  BusLock lock;

  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(0x00);
  Wire.write(decToBcd(dateTime.second));
  Wire.write(decToBcd(dateTime.minute));
  Wire.write(decToBcd(dateTime.hour));
  Wire.write(1);
  Wire.write(decToBcd(dateTime.day));
  Wire.write(decToBcd(dateTime.month));
  Wire.write(decToBcd(dateTime.year - 2000));

  const bool timeWritten = Wire.endTransmission() == 0;

  // Limpa o flag OSF (registrador de status 0x0F): indica que o oscilador
  // parou e a hora anterior não era confiável.
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(0x0F);
  Wire.write(0x00);

  const bool statusCleared = Wire.endTransmission() == 0;

  return timeWritten && statusCleared;
}

void ds3231PrintDiagnostic() {

  Serial.println(F("[I2C DS] scanner iniciado em SDA=11 SCL=12"));
  uint8_t foundDevices = 0;

  if (busMutex) {
    xSemaphoreTake(busMutex, portMAX_DELAY);
  }

  for (uint8_t address = 1; address < 127; address++) {

    Wire.beginTransmission(address);

    if (Wire.endTransmission() == 0) {

      Serial.printf("[I2C DS] dispositivo encontrado: 0x%02X\n", address);
      foundDevices++;
    }
  }

  if (busMutex) {
    xSemaphoreGive(busMutex);
  }

  if (foundDevices == 0) {
    Serial.println(F("[I2C DS] nenhum dispositivo respondeu"));
  } else {
    Serial.printf("[I2C DS] total encontrado: %u\n", foundDevices);
  }
}
