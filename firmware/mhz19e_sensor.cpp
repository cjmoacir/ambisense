#include "mhz19e_sensor.h"
#include "config.h"

static HardwareSerial sensorSerial(1);

static uint8_t checksum(const uint8_t *frame) {

  uint8_t sum = 0;

  for (uint8_t i = 1; i < 8; i++) {

    sum += frame[i];
  }

  return static_cast<uint8_t>(0xFF - sum + 1);
}

void mhz19Begin() {

  sensorSerial.begin(MHZ19_BAUD, SERIAL_8N1, MHZ19_RX_PIN, MHZ19_TX_PIN);
}

Co2Reading mhz19Read() {

  const uint8_t request[9] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79};

  while (sensorSerial.available()) {

    sensorSerial.read();
  }

  sensorSerial.write(request, sizeof(request));
  sensorSerial.flush();

  uint8_t response[9] = {};
  uint8_t count = 0;
  const uint32_t started = millis();

  while (count < sizeof(response) && millis() - started < 250) {

    if (sensorSerial.available()) {

      response[count++] = sensorSerial.read();
    }

    else {

      // Cede a CPU enquanto espera: sem isso, um sensor mudo ocupava o núcleo
      // por 250 ms a cada segundo e atrasava o servidor do dashboard.
      vTaskDelay(1);
    }
  }

  if (count != sizeof(response) || response[0] != 0xFF || response[1] != 0x86 ||
      response[8] != checksum(response)) {

    return {false, 0};
  }

  return {true, static_cast<uint16_t>(response[2]) * 256 + response[3]};
}
