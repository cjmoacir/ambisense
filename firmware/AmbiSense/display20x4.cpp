#include "display20x4.h"

#include <WiFi.h>
#include <math.h>

#include "config.h"
#include "ds3231_rtc.h"
#include "wifi_time.h"

namespace {

constexpr uint8_t LCD_SDA_PIN = 8;
constexpr uint8_t LCD_SCL_PIN = 9;
constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t LCD_BACKLIGHT = 0x08;
constexpr uint8_t LCD_ENABLE = 0x04;

bool ready = false;
uint8_t screen = 0;
uint32_t lastScreenAt = 0;
uint32_t lastDrawAt = 0;

void clockHigh() {
  pinMode(LCD_SCL_PIN, INPUT_PULLUP);
}

void clockLow() {
  pinMode(LCD_SCL_PIN, OUTPUT);
  digitalWrite(LCD_SCL_PIN, LOW);
}

void dataHigh() {
  pinMode(LCD_SDA_PIN, INPUT_PULLUP);
}

void dataLow() {
  pinMode(LCD_SDA_PIN, OUTPUT);
  digitalWrite(LCD_SDA_PIN, LOW);
}

void i2cStart() {
  dataHigh();
  clockHigh();
  delayMicroseconds(4);
  dataLow();
  delayMicroseconds(4);
  clockLow();
}

void i2cStop() {
  dataLow();
  clockHigh();
  delayMicroseconds(4);
  dataHigh();
  delayMicroseconds(4);
}

bool i2cWriteByte(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; bit++) {
    if (value & 0x80) dataHigh();
    else dataLow();
    clockHigh();
    delayMicroseconds(3);
    clockLow();
    value <<= 1;
  }

  dataHigh();
  clockHigh();
  delayMicroseconds(3);
  const bool acknowledged = digitalRead(LCD_SDA_PIN) == LOW;
  clockLow();
  return acknowledged;
}

bool expanderWrite(uint8_t value) {
  i2cStart();
  const bool addressAck = i2cWriteByte(static_cast<uint8_t>(LCD_ADDRESS << 1));
  const bool dataAck = i2cWriteByte(value | LCD_BACKLIGHT);
  i2cStop();
  return addressAck && dataAck;
}

void lcdPulse(uint8_t value) {
  expanderWrite(value | LCD_ENABLE);
  delayMicroseconds(1);
  expanderWrite(value & ~LCD_ENABLE);
  delayMicroseconds(50);
}

void lcdWrite4(uint8_t value, bool data) {
  const uint8_t control = data ? 0x01 : 0x00;
  lcdPulse((value & 0xF0) | control);
}

void lcdCommand(uint8_t command) {
  lcdWrite4(command, false);
  lcdWrite4(static_cast<uint8_t>(command << 4), false);
  if (command == 0x01 || command == 0x02) delay(2);
}

void lcdData(uint8_t value) {
  lcdWrite4(value, true);
  lcdWrite4(static_cast<uint8_t>(value << 4), true);
}

void lcdText(const String &text) {
  for (size_t i = 0; i < text.length() && i < 20; i++) lcdData(text[i]);
}

String fit20(String text) {
  if (text.length() > 20) text.remove(20);
  while (text.length() < 20) text += ' ';
  return text;
}

void lcdLine(uint8_t row, const String &text) {
  static const uint8_t offsets[] = {0x00, 0x40, 0x14, 0x54};
  lcdCommand(static_cast<uint8_t>(0x80 | offsets[row]));
  lcdText(fit20(text));
}

String valueOrError(bool valid, const String &value) {
  return valid ? value : String("ERRO");
}

void drawScreen(const Mq2Reading &mq2, const ThermistorReading &thermistor,
                const Bme680Reading &bme, const Co2Reading &co2, bool relayOn) {
  lcdCommand(0x01);

  if (screen == 0) {
    lcdLine(0, "AmbiSense");
    lcdLine(1, String("IP:") + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "sem WiFi"));
    RtcDateTime now{};
    if (wifiTimeNow(now)) {
      char dateTime[21];
      snprintf(dateTime, sizeof(dateTime), "%02u/%02u/%04u %02u:%02u:%02u",
               now.day, now.month, now.year, now.hour, now.minute, now.second);
      lcdLine(2, dateTime);
    } else {
      lcdLine(2, "Hora: aguardando NTP");
    }
    lcdLine(3, WiFi.status() == WL_CONNECTED
                    ? String("WiFi: ") + WiFi.SSID()
                    : "WiFi: desconectado");
  } else if (screen == 1) {
    lcdLine(0, String("CO2: ") + valueOrError(co2.valid, String(co2.ppm)) + (co2.valid ? " ppm" : ""));
    lcdLine(1, String("Temp: ") + valueOrError(bme.valid, bme.valid ? String(bme.temperature, 1) + " C" : ""));
    lcdLine(2, String("Umid: ") + valueOrError(bme.valid, bme.valid ? String(bme.humidity, 1) + " %" : ""));
    lcdLine(3, String("Exaustao: ") + (relayOn ? "ON" : "OFF"));
  } else if (screen == 2) {
    lcdLine(0, String("MQ: ") + mq2.analogValue + " DO:" + mq2.digitalValue);
    lcdLine(1, String("NTC: ") + valueOrError(thermistor.valid, thermistor.valid ? String(thermistor.temperature, 1) + " C" : ""));
    lcdLine(2, String("Press: ") + valueOrError(bme.valid, bme.valid ? String(bme.pressureHpa, 0) + " hPa" : ""));
    lcdLine(3, String("BME: ") + (bme.valid ? "OK" : "ERRO"));
  } else {
    lcdLine(0, "Sensores AmbiSense");
    lcdLine(1, String("CO2: ") + (co2.valid ? "OK" : "ERRO"));
    lcdLine(2, String("MQ: ") + (mq2.alarm ? "ALARME" : "OK"));
    lcdLine(3, String("Rele: ") + (relayOn ? "LIGADO" : "DESLIGADO"));
  }
}

}  // namespace

void displayBegin() {
  pinMode(LCD_SDA_PIN, INPUT_PULLUP);
  pinMode(LCD_SCL_PIN, INPUT_PULLUP);
  delay(50);

  lcdWrite4(0x30, false);
  delay(5);
  lcdWrite4(0x30, false);
  delayMicroseconds(150);
  lcdWrite4(0x30, false);
  lcdWrite4(0x20, false);
  lcdCommand(0x28);  // 4 bits, 2 linhas, matriz 5x8.
  lcdCommand(0x08);  // display desligado durante a configuração.
  lcdCommand(0x01);
  lcdCommand(0x06);
  lcdCommand(0x0C);

  ready = expanderWrite(LCD_BACKLIGHT);
  if (ready) lcdCommand(0x01);
}

void displayUpdate(const Mq2Reading &mq2, const ThermistorReading &thermistor,
                  const Bme680Reading &bme, const Co2Reading &co2,
                  bool relayOn) {
  if (!ready) return;
  const uint32_t now = millis();
  if (now - lastScreenAt >= 4000) {
    lastScreenAt = now;
    screen = static_cast<uint8_t>((screen + 1) % 4);
  }
  if (now - lastDrawAt < 1000) return;
  lastDrawAt = now;
  drawScreen(mq2, thermistor, bme, co2, relayOn);
}
