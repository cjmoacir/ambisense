#include "alerts.h"

#include <Preferences.h>
#include <math.h>

#include "buzzer_control.h"
#include "config.h"
#include "logger.h"

namespace {

struct AlertInfo {

  const char *id;

  const char *label;
};

const AlertInfo INFO[ALERT_COUNT] = {{"co2", "CO2"},
                                     {"mq", "Gases (analogico)"},
                                     {"temp", "Temperatura BME680"},
                                     {"ntc", "Temperatura NTC"},
                                     {"hum", "Umidade"},
                                     {"gas", "Gas VOC"},
                                     {"press", "Pressao absoluta"},
                                     {"do", "Gases (digital)"}};

// Padrões: CO2 e MQ-2 ligados com os limites já usados pelo painel; os demais
// começam desligados, com faixas sugeridas que o usuário pode ajustar.
const AlertConfig DEFAULTS[ALERT_COUNT] = {
    {true, NAN, 1000.0f},
    {true, NAN, 1800.0f},
    {false, 19.0f, 26.0f},
    {false, 19.0f, 26.0f},
    {false, 40.0f, 70.0f},
    {false, 20.0f, NAN},
    {false, NAN, NAN},
    // Saída digital do MQ-2: valor 1 = acionada; dispara acima de 0.
    {false, NAN, 0.0f}};

struct AlertState {

  uint8_t outCount;

  uint8_t inCount;

  bool active;
};

AlertConfig config[ALERT_COUNT];

AlertState state[ALERT_COUNT];

// Alertas disparados que ainda aguardam o buzzer; um bit por alerta.
volatile uint8_t pendingMask = 0;

Preferences preferences;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void makeKey(char *buffer, size_t size, char prefix, uint8_t index) {

  snprintf(buffer, size, "%c%u", prefix, static_cast<unsigned>(index));
}

bool validRange(const AlertConfig &candidate) {

  if (isfinite(candidate.minValue) && isfinite(candidate.maxValue) &&
      candidate.minValue > candidate.maxValue) {

    return false;
  }

  return true;
}

}  // namespace

void alertsBegin() {

  preferences.begin("alertas", false);

  char key[8];

  for (uint8_t index = 0; index < ALERT_COUNT; index++) {

    AlertConfig loaded = DEFAULTS[index];

    makeKey(key, sizeof(key), 'e', index);
    loaded.enabled = preferences.getBool(key, DEFAULTS[index].enabled);

    makeKey(key, sizeof(key), 'n', index);
    loaded.minValue = preferences.getFloat(key, DEFAULTS[index].minValue);

    makeKey(key, sizeof(key), 'x', index);
    loaded.maxValue = preferences.getFloat(key, DEFAULTS[index].maxValue);

    config[index] = validRange(loaded) ? loaded : DEFAULTS[index];
    state[index] = {0, 0, false};
  }

  loggerWrite(LOG_INFO, F("Alertas"), F("alertsBegin"), F("Faixas de alerta carregadas"));
}

void alertsUpdate(const Mq2Reading &mq2, const ThermistorReading &thermistor,
                  const Bme680Reading &bme, const Co2Reading &co2) {

  float values[ALERT_COUNT];
  bool valid[ALERT_COUNT];

  valid[0] = co2.valid;
  values[0] = co2.ppm;

  valid[1] = true;
  values[1] = mq2.analogValue;

  valid[2] = bme.valid;
  values[2] = bme.temperature;

  valid[3] = thermistor.valid;
  values[3] = thermistor.temperature;

  valid[4] = bme.valid;
  values[4] = bme.humidity;

  valid[5] = bme.valid;
  values[5] = bme.gasKohm;

  valid[6] = bme.valid;
  values[6] = bme.pressureHpa;

  // Saída digital do MQ-2 como 0/1: 1 quando o comparador do módulo dispara.
  const bool digitalTriggered = MQ2_DIGITAL_ALARM_ACTIVE_LOW ? !mq2.digitalValue : mq2.digitalValue;
  valid[7] = true;
  values[7] = digitalTriggered ? 1.0f : 0.0f;

  // Sensores analógicos e de gás oscilam logo após ligar; não alarma nesse período.
  const bool startupGrace = millis() < ALERT_STARTUP_GRACE_MS;

  for (uint8_t index = 0; index < ALERT_COUNT; index++) {

    bool fired = false;
    bool cleared = false;

    portENTER_CRITICAL(&mux);

    const AlertConfig current = config[index];
    AlertState &item = state[index];

    if (!current.enabled) {

      item.active = false;
      item.outCount = 0;
      item.inCount = 0;
    }

    else if (startupGrace) {

      // Durante a carência as contagens ficam zeradas: a confirmação só
      // começa depois dela.
      item.outCount = 0;
      item.inCount = 0;
    }

    else if (valid[index] && isfinite(values[index])) {

      const float value = values[index];

      const bool outside = (isfinite(current.minValue) && value < current.minValue) ||
                           (isfinite(current.maxValue) && value > current.maxValue);

      if (outside) {

        item.inCount = 0;

        if (item.outCount < 255) {

          item.outCount++;
        }

        // Dispara uma única vez ao entrar na faixa de alerta.
        if (!item.active && item.outCount >= ALERT_CONFIRM_SAMPLES) {

          item.active = true;
          pendingMask |= static_cast<uint8_t>(1U << index);
          fired = true;
        }
      }

      else {

        item.outCount = 0;

        if (item.inCount < 255) {

          item.inCount++;
        }

        // Rearma depois de várias leituras seguidas dentro da faixa.
        if (item.active && item.inCount >= ALERT_CLEAR_SAMPLES) {

          item.active = false;
          cleared = true;
        }
      }
    }

    portEXIT_CRITICAL(&mux);

    if (fired) {

      loggerWrite(LOG_WARN, F("Alertas"), F("alertsUpdate"),
                  String(INFO[index].label) + F(" fora da faixa: ") + String(values[index], 1));
    }

    if (cleared) {

      loggerWrite(LOG_INFO, F("Alertas"), F("alertsUpdate"),
                  String(INFO[index].label) + F(" voltou para a faixa"));
    }
  }
}

void alertsLoop() {

  if (pendingMask == 0 || buzzerIsBusy()) {

    return;
  }

  uint8_t index = 0xFF;

  portENTER_CRITICAL(&mux);

  for (uint8_t candidate = 0; candidate < ALERT_COUNT; candidate++) {

    if (pendingMask & (1U << candidate)) {

      pendingMask &= static_cast<uint8_t>(~(1U << candidate));
      index = candidate;

      break;
    }
  }

  portEXIT_CRITICAL(&mux);

  if (index == 0xFF) {

    return;
  }

  buzzerRequestBeeps(ALERT_BEEP_COUNT, ALERT_BEEP_ON_MS, ALERT_BEEP_GAP_MS, INFO[index].label);
}

const char *alertId(uint8_t index) {

  return index < ALERT_COUNT ? INFO[index].id : "";
}

bool alertIndexFromId(const String &id, uint8_t &index) {

  for (uint8_t candidate = 0; candidate < ALERT_COUNT; candidate++) {

    if (id == INFO[candidate].id) {

      index = candidate;

      return true;
    }
  }

  return false;
}

AlertConfig alertGetConfig(uint8_t index) {

  if (index >= ALERT_COUNT) {

    return {false, NAN, NAN};
  }

  portENTER_CRITICAL(&mux);
  const AlertConfig copy = config[index];
  portEXIT_CRITICAL(&mux);

  return copy;
}

bool alertIsActive(uint8_t index) {

  return index < ALERT_COUNT && state[index].active;
}

bool alertSetConfig(uint8_t index, const AlertConfig &newConfig) {

  if (index >= ALERT_COUNT || !validRange(newConfig)) {

    return false;
  }

  char key[8];

  makeKey(key, sizeof(key), 'e', index);
  preferences.putBool(key, newConfig.enabled);

  makeKey(key, sizeof(key), 'n', index);
  preferences.putFloat(key, newConfig.minValue);

  makeKey(key, sizeof(key), 'x', index);
  preferences.putFloat(key, newConfig.maxValue);

  portENTER_CRITICAL(&mux);

  config[index] = newConfig;
  state[index] = {0, 0, false};
  pendingMask &= static_cast<uint8_t>(~(1U << index));

  portEXIT_CRITICAL(&mux);

  loggerWrite(LOG_INFO, F("Alertas"), F("alertSetConfig"),
              String(INFO[index].label) + (newConfig.enabled ? " ligado" : " desligado"));

  return true;
}
