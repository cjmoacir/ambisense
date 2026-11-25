#include "display20x4.h"

#include <WiFi.h>
#include <Wire.h>
#include <math.h>

#include "config.h"
#include "ds3231_rtc.h"
#include "wifi_time.h"

namespace {

constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t LCD_BACKLIGHT = 0x08;
constexpr uint8_t LCD_ENABLE = 0x04;

bool ready = false;
uint8_t screen = 0;
uint32_t lastScreenAt = 0;
uint32_t lastDrawAt = 0;

bool expanderWrite(uint8_t value) {
  Wire.beginTransmission(LCD_ADDRESS);
  Wire.write(value | LCD_BACKLIGHT);
  return Wire.endTransmission() == 0;
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

void lcdCreateNegationChar() {
  static const uint8_t glyph[8] = {
      0b11111,
      0b00001,
      0b00001,
      0b00001,
      0b00000,
      0b00000,
      0b00000,
      0b00000};

  lcdCommand(0x40);
  for (uint8_t row = 0; row < 8; row++) lcdData(glyph[row]);
  lcdCommand(0x02);
}

void lcdText(const String &text) {
  for (size_t i = 0; i < text.length() && i < 20; i++) {
    // '^' é o marcador interno do caractere personalizado "¬".
    lcdData(text[i] == '^' ? 0 : text[i]);
  }
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

void lcdPrintFrame(const String lines[4]) {
  static const uint8_t offsets[] = {0x00, 0x40, 0x14, 0x54};
  String frame;
  frame.reserve(80);

  for (uint8_t row = 0; row < 4; row++) {
    frame += fit20(lines[row]);
  }

  for (uint8_t row = 0; row < 4; row++) {
    lcdCommand(static_cast<uint8_t>(0x80 | offsets[row]));
    lcdText(frame.substring(row * 20, row * 20 + 20));
  }
}

String valueOrError(bool valid, const String &value) {
  return valid ? value : String("ERRO");
}

float saturationFactor(float temperature) {
  return expf(17.27f * temperature / (237.7f + temperature));
}

bool calculateFeelsLike(const ThermistorReading &thermistor,
                        const Bme680Reading &bme, float &result) {
  if (!bme.valid && !thermistor.valid) return false;

  const float baseTemperature = thermistor.valid ? thermistor.temperature : bme.temperature;
  if (!bme.valid) return false;

  float relativeHumidity = bme.humidity;
  if (thermistor.valid && bme.valid) {
    relativeHumidity = bme.humidity * saturationFactor(bme.temperature) /
                       saturationFactor(thermistor.temperature);
  }

  relativeHumidity = constrain(relativeHumidity, 0.0f, 100.0f);
  result = baseTemperature +
           0.33f * (relativeHumidity / 100.0f * 6.105f * saturationFactor(baseTemperature)) -
           4.0f;
  return isfinite(result);
}

const char *mqStatus(int analogValue) {
  if (analogValue <= 250) return "NORMAL";
  if (analogValue <= 500) return "ATENCAO";
  if (analogValue <= 1000) return "ALTERADO";
  if (analogValue < MQ2_ALARM_LEVEL) return "POLUICAO";
  return "ALARME";
}

const char *co2Status(uint16_t ppm) {
  if (ppm < 800) return "NORMAL";
  if (ppm < 1000) return "ATENCAO";
  if (ppm < 1500) return "VENTILAR";
  if (ppm < 2000) return "ALTO";
  if (ppm < 3000) return "M.ALTO";
  if (ppm < 4000) return "CRITICO";
  return "EMERG.";
}

void drawScreen(const Mq2Reading &mq2, const ThermistorReading &thermistor,
                const Bme680Reading &bme, const Co2Reading &co2, bool relayOn) {
  String lines[4];

  if (screen == 0) {
    lines[0] = "AmbiSense";
    lines[1] = String("Net : ") + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "sem WiFi");
    lines[2] = String("IP  : ") + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "--");
    RtcDateTime now{};
    if (wifiTimeNow(now)) {
      char dateTime[21];
      snprintf(dateTime, sizeof(dateTime), "%02u/%02u/%04u %02u:%02u:%02u",
               now.day, now.month, now.year, now.hour, now.minute, now.second);
      lines[3] = dateTime;
    } else {
      lines[3] = "--/--/---- --:--:--";
    }
  } else if (screen == 1) {
    float feelsLike = NAN;
    const bool feelsLikeValid = calculateFeelsLike(thermistor, bme, feelsLike);
    lines[0] = "Temperatura ^";
    lines[1] = String("  NTC  : ") + valueOrError(thermistor.valid, thermistor.valid ? String(thermistor.temperature, 1) + " C" : "");
    lines[2] = String("  Sens : ") + valueOrError(feelsLikeValid, feelsLikeValid ? String(feelsLike, 1) + " C" : "");
    lines[3] = String("  Caixa: ") + valueOrError(bme.valid, bme.valid ? String(bme.temperature, 1) + " C" : "");
  } else if (screen == 2) {
    lines[0] = String("Patm   : ") + valueOrError(bme.valid, bme.valid ? String(bme.pressureHpa, 1) + " hPa" : "");
    lines[1] = String("R Gas  : ") + valueOrError(bme.valid, bme.valid ? String(bme.gasKohm, 1) + " kOhm" : "");
    lines[2] = String("CO2    : ") + valueOrError(co2.valid, co2.valid ? String(co2.ppm) + " ppm" : "");
    lines[3] = String("          ") + (co2.valid ? co2Status(co2.ppm) : "SEM LEITURA");
  } else {
    lines[0] = "MQ ^";
    lines[1] = String("  A0   : ") + mq2.analogValue + " " + mqStatus(mq2.analogValue);
    lines[2] = String("  D0   : ") + mq2.digitalValue + (mq2.digitalValue == 1 ? " NORMAL" : " ALARME");
    lines[3] = "";
  }

  lcdPrintFrame(lines);
}

}  // namespace

void displayBegin() {
  // Compartilha o mesmo barramento dos sensores: BME680, DS3231 e display.
  Wire.begin(BME680_SDA_PIN, BME680_SCL_PIN);
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
  lcdCreateNegationChar();

  ready = expanderWrite(LCD_BACKLIGHT);
  if (ready) lcdCommand(0x01);
}

void displayUpdate(const Mq2Reading &mq2, const ThermistorReading &thermistor,
                  const Bme680Reading &bme, const Co2Reading &co2,
                  bool relayOn) {
  if (!ready) return;
  const uint32_t now = millis();
  if (now - lastScreenAt >= 5000) {
    lastScreenAt = now;
    screen = static_cast<uint8_t>((screen + 1) % 4);
  }
  if (now - lastDrawAt < 1000) return;
  lastDrawAt = now;
  drawScreen(mq2, thermistor, bme, co2, relayOn);
}
