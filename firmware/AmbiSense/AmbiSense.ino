#include <math.h>

#include "config.h"
#include "mq2_sensor.h"
#include "thermistor_sensor.h"
#include "buzzer_control.h"
#include "bme680_sensor.h"
#include "mhz19e_sensor.h"
#include "ds3231_rtc.h"
#include "relay_control.h"
#include "logger.h"
#include "storage_logger.h"
#include "wifi_time.h"
#include "alerts.h"  // ALERTAS_PATCH

// Referência da tarefa que lê sensores e grava o histórico.
static TaskHandle_t acquisitionTaskHandle = nullptr;

// Janela de verificação após ligar: sensores digitais devem estabilizar neste período.
constexpr uint32_t STARTUP_SENSOR_CHECK_MS = 60UL * 1000UL;

// Registra o uso da Flash pelo firmware e a capacidade física do ESP32-S3.
// A capacidade da partição FATFS é exibida separadamente no dashboard.
static void logFirmwareMemoryInfo() {

  const uint32_t firmwareBytes = ESP.getSketchSize();
  const uint32_t availableFirmwareBytes = ESP.getFreeSketchSpace();
  const uint32_t flashBytes = ESP.getFlashChipSize();

  String message = F("Firmware: ");
  message += firmwareBytes / 1024;
  message += F(" KB | Espaco livre para firmware: ");
  message += availableFirmwareBytes / 1024;
  message += F(" KB | Flash total: ");
  message += flashBytes / 1024 / 1024;
  message += F(" MB");

  loggerWrite(LOG_INFO, F("Sistema"), F("logFirmwareMemoryInfo"), message);
}

// Suspende a coleta durante o portal Wi‑Fi para deixá-lo como única função ativa.
static void setPortalMode(bool active) {

  if (acquisitionTaskHandle == nullptr) {

    return;
  }

  if (active) {

    vTaskSuspend(acquisitionTaskHandle);
  }

  else {

    vTaskResume(acquisitionTaskHandle);
  }
}

// Nunca reinicia o ESP32 por falha de sensor: um único BME680/MH-Z19E inválido
// na leitura do 60º segundo (comum durante o aquecimento) derrubava a placa e
// o dashboard. Em vez disso, só tenta reinicializar o BME680 a cada 60 s
// enquanto ele estiver inválido; o painel já exibe "sensor com falha".
static void recoverStartupSensors(const Bme680Reading &bme, const Co2Reading &co2) {

  static uint32_t lastCheckAt = 0;

  if (millis() < STARTUP_SENSOR_CHECK_MS || millis() - lastCheckAt < STARTUP_SENSOR_CHECK_MS) {

    return;
  }

  lastCheckAt = millis();

  if (!co2.valid) {

    loggerWrite(LOG_WARN, F("Sistema"), F("recoverStartupSensors"),
                F("MH-Z19E sem resposta valida"));
  }

  if (!bme.valid) {

    loggerWrite(LOG_WARN, F("Sistema"), F("recoverStartupSensors"),
                F("BME680 invalido; reinicializando o sensor"));

    bme680Begin();
  }
}

// Lê todos os sensores, atualiza a exaustão e salva uma leitura completa.
static void printMeasurements() {

  // A primeira leitura após ligar precisa ser persistida, mesmo sem variação.
  static bool firstReadingAfterBoot = true;

  const Mq2Reading mq2 = mq2Read();
  const ThermistorReading thermistor = thermistorRead();
  const Bme680Reading bme = bme680Read();
  const Co2Reading co2 = mhz19Read();

  recoverStartupSensors(bme, co2);

  relayUpdate(mq2.alarm, co2.valid, co2.ppm, bme.temperature);

  // Compara cada leitura com a faixa de alerta configurada no dashboard.
  alertsUpdate(mq2, thermistor, bme, co2);

  String message = F("MQ2_AO=");
  message += mq2.analogValue;
  message += F(" MQ2_DO=");

  message += mq2.digitalValue;
  message += F(" MHZ_CO2=");

  if (co2.valid) {

    message += co2.ppm;
  }

  else {

    message += F("ERRO");
  }

  if (bme.valid) {

    message += F(" BME_Temp=");
    message += String(bme.temperature, 1);
    message += F(" C BME_Umid=");
    message += String(bme.humidity, 1);
    message += F(" % BME_Pressao=");
    message += String(bme.pressureHpa, 1);
    message += F(" hPa BME_Gas=");
    message += String(bme.gasKohm, 1);
    message += F(" kOhm");
  }

  else {

    message += F(" BME680=ERRO");
  }

  if (thermistor.valid) {

    message += F(" TERM_Temp=");
    message += String(thermistor.temperature, 1);
    message += F(" C");
  }

  else {

    message += F(" TERMISTOR=ERRO");
  }

  message += F(" Exaustao_forcada=");
  message += relayIsOn() ? F("ON") : F("OFF");

  message += F(" Buzzer=");
  message += buzzerIsOn() ? F("ON") : F("OFF");

  loggerWrite(LOG_DATA, F("AmbiSense"), F("printMeasurements"), message);

  // O histórico guarda uma linha a cada 2 s com a média das leituras válidas
  // desses 2 s (duas leituras de 1 s). 43.200 linhas = 24 h.
  static uint8_t windowCount = 0;
  static long mqSum = 0;
  static int mqDigital = 0;
  static uint8_t co2N = 0;
  static long co2Sum = 0;
  static uint8_t bmeN = 0;
  static float bmeT = 0, bmeH = 0, bmeP = 0, bmeG = 0;
  static uint8_t ntcN = 0;
  static float ntcT = 0;

  windowCount++;
  mqSum += mq2.analogValue;
  mqDigital |= mq2.digitalValue;

  if (co2.valid) {

    co2N++;
    co2Sum += co2.ppm;
  }

  if (bme.valid) {

    bmeN++;
    bmeT += bme.temperature;
    bmeH += bme.humidity;
    bmeP += bme.pressureHpa;
    bmeG += bme.gasKohm;
  }

  if (thermistor.valid) {

    ntcN++;
    ntcT += thermistor.temperature;
  }

  if (windowCount >= 2) {

    Mq2Reading avgMq = mq2;
    avgMq.analogValue = static_cast<int>(lroundf(static_cast<float>(mqSum) / windowCount));
    avgMq.digitalValue = mqDigital ? 1 : 0;

    Co2Reading avgCo2 = co2;
    avgCo2.valid = co2N > 0;
    avgCo2.ppm = co2N > 0 ? static_cast<uint16_t>(lroundf(static_cast<float>(co2Sum) / co2N)) : 0;

    Bme680Reading avgBme = bme;
    avgBme.valid = bmeN > 0;

    if (bmeN > 0) {

      avgBme.temperature = bmeT / bmeN;
      avgBme.humidity = bmeH / bmeN;
      avgBme.pressureHpa = bmeP / bmeN;
      avgBme.gasKohm = bmeG / bmeN;
    }

    ThermistorReading avgNtc = thermistor;
    avgNtc.valid = ntcN > 0;

    if (ntcN > 0) {

      avgNtc.temperature = ntcT / ntcN;
    }

    storageLoggerAppend(avgMq, avgNtc, avgBme, avgCo2, relayIsOn(), buzzerIsOn(),
                        firstReadingAfterBoot);

    firstReadingAfterBoot = false;

    windowCount = 0;
    mqSum = 0;
    mqDigital = 0;
    co2N = 0;
    co2Sum = 0;
    bmeN = 0;
    bmeT = bmeH = bmeP = bmeG = 0;
    ntcN = 0;
    ntcT = 0;
  }
}

// Tarefa FreeRTOS independente responsável pelo ciclo de aquisição.
static void acquisitionTask(void *parameter) {

  (void)parameter;

  // Parte lenta do armazenamento aqui, fora do setup(): o dashboard já está
  // no ar enquanto o histórico é indexado.
  storageLoggerIndex();

  // Leitura a cada 1 s exato; a cada 2 leituras grava-se a média (43.200 linhas = 24 h).
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {

    printMeasurements();

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));
  }
}

void setup() {

  // Inicialização dos serviços necessários antes de iniciar a coleta contínua.

  loggerBegin(SERIAL_BAUD);

  ds3231Begin();

  // Diagnóstico: mostra o que o DS3231 devolveu no boot (hora ausente no log).
  {
    const RtcDateTime rtc = ds3231Now();

    loggerWrite(LOG_INFO, F("Sistema"), F("setup"),
                String(F("DS3231 detectado=")) + (ds3231Available() ? F("sim") : F("nao")) + F(" leitura=") + rtc.day + F("/") + rtc.month + F("/") + rtc.year + F(" ") + rtc.hour + F(":") + rtc.minute + F(":") + rtc.second);
  }

  // O dashboard é a prioridade: o Wi-Fi começa a conectar em segundo plano
  // (sem bloquear) e o servidor HTTP sobe no loop() assim que houver IP.
  buzzerBegin();

  wifiTimeBegin();

  // O FATFS é montado enquanto o Wi-Fi conecta.
  storageLoggerBegin();

  logFirmwareMemoryInfo();

  // Diagnóstico: tempo de cada inicialização (o setup chegou a levar 10 s).
  uint32_t stepStartedAt = millis();

  const auto logStep = [&stepStartedAt](const __FlashStringHelper *name) {
    loggerWrite(LOG_INFO, F("Sistema"), F("setup"),
                String(name) + F(": ") + (millis() - stepStartedAt) + F(" ms"));

    stepStartedAt = millis();
  };

  mq2Begin();
  thermistorBegin();
  logStep(F("MQ-2 e NTC"));

  bme680Begin();
  logStep(F("BME680"));

  mhz19Begin();
  logStep(F("MH-Z19E"));

  relayBegin(wifiTimeRelayDelaySeconds());
  logStep(F("Rele"));

  alertsBegin();
  logStep(F("Alertas"));

  loggerWrite(LOG_INFO, F("AmbiSense"), F("setup"), F("Inicializacao concluida"));

  xTaskCreatePinnedToCore(acquisitionTask, "AmbiSenseColeta", 12288, nullptr, 1,
                          &acquisitionTaskHandle, 1);

  wifiTimeSetPortalStateCallback(setPortalMode);
}

void loop() {

  // O loop principal atende somente Wi‑Fi, portal e dashboard.

  wifiTimeLoop();

  buzzerLoop();

  // Toca o padrão sonoro dos alertas disparados pela coleta.
  alertsLoop();

  delay(1);
}
