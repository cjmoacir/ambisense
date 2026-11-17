#include "bme680_sensor.h"
#include <Wire.h>
#include <Adafruit_BME680.h>
#include <math.h>
#include "config.h"

static Adafruit_BME680 sensor;

static bool available = false;

// Estado do aquecedor do gás e último valor medido.
static bool gasHeaterOn = false;
static uint32_t lastGasAt = 0;
static float lastGasKohm = NAN;

void bme680Begin() {

  // Inicia o barramento I2C usado pelo BME680.
  Wire.begin(BME680_SDA_PIN, BME680_SCL_PIN);

  // Tenta encontrar o sensor nos dois endereços I2C possíveis.
  available = sensor.begin(0x76, &Wire) || sensor.begin(0x77, &Wire);

  if (!available) {

    // Encerra a inicialização se o sensor não respondeu.
    return;
  }

  // Define a sobreamostragem da temperatura.
  sensor.setTemperatureOversampling(BME680_OS_8X);

  // Define a sobreamostragem da umidade.
  sensor.setHumidityOversampling(BME680_OS_2X);

  // Define a sobreamostragem da pressão.
  sensor.setPressureOversampling(BME680_OS_4X);

  // Define o filtro digital interno.
  sensor.setIIRFilterSize(BME680_FILTER_SIZE_3);

  // Configura o aquecedor usado pela leitura de gás/VOC.
  sensor.setGasHeater(320, 150);

  gasHeaterOn = true;
  lastGasAt = 0;
}

Bme680Reading bme680Read() {

  // Se o sensor não respondeu no boot (falha intermitente), tenta de novo a
  // cada 5 s em vez de esperar o aviso de 60 s.
  static uint32_t lastRetryAt = 0;

  if (!available && millis() - lastRetryAt >= 5000) {

    lastRetryAt = millis();

    bme680Begin();
  }

  if (!available) {

    return {false, NAN, NAN, NAN, NAN};
  }

  // O aquecedor do gás (320 °C) esquenta o próprio sensor: com leitura a cada
  // 1 s a temperatura chegava a ~5 °C acima do NTC e a umidade saía baixa.
  // O gás é medido só a cada BME680_GAS_INTERVAL_MS; nas demais leituras o
  // aquecedor fica desligado e o último valor de gás é mantido.
  const bool wantGas = lastGasAt == 0 || millis() - lastGasAt >= BME680_GAS_INTERVAL_MS;

  if (wantGas != gasHeaterOn) {

    if (wantGas) {

      sensor.setGasHeater(320, 150);
    }

    else {

      sensor.setGasHeater(0, 0);
    }

    gasHeaterOn = wantGas;
  }

  if (!sensor.performReading()) {

    // Informa falha quando o sensor não conclui a leitura.
    return {false, NAN, NAN, NAN, NAN};
  }

  if (wantGas) {

    lastGasKohm = sensor.gas_resistance / 1000.0f;
    lastGasAt = millis();

    if (lastGasAt == 0) {

      lastGasAt = 1;
    }
  }

  const float rawTemperature = sensor.temperature;
  const float temperature = rawTemperature - BME680_TEMP_OFFSET_C;

  // A umidade relativa depende da temperatura: o ar tem a mesma quantidade de
  // vapor, mas a 'temperatura corrigida' satura com menos, então a umidade
  // relativa real é maior (fórmula de Magnus).
  const auto saturation = [](float celsius) {
    return 6.112f * expf(17.62f * celsius / (243.12f + celsius));
  };

  float humidity = sensor.humidity;

  if (BME680_TEMP_OFFSET_C != 0.0f) {

    humidity = humidity * saturation(rawTemperature) / saturation(temperature);

    if (humidity > 100.0f) {

      humidity = 100.0f;
    }
  }

  // Converte pressão para hPa e a resistência do gás para kOhm antes de retornar.
  return {true, temperature, humidity, sensor.pressure / 100.0f, lastGasKohm};
}
