#include "buzzer_control.h"

#include "config.h"
#include "logger.h"

static bool enabled = false;
static uint8_t remainingBeeps = 0;
static uint32_t beepDurationMs = 0;
static uint32_t gapDurationMs = 0;
static uint32_t stateChangedAt = 0;
static bool waitingGap = false;
static String currentEvent;

static void applyBuzzerOutput() {

  const bool shouldBeOn = enabled;
  static bool outputOn = false;
  static bool outputKnown = false;

  if (outputKnown && outputOn == shouldBeOn) {

    return;
  }

  outputOn = shouldBeOn;
  outputKnown = true;
  digitalWrite(BUZZER_PIN, (BUZZER_ACTIVE_HIGH ? shouldBeOn : !shouldBeOn) ? HIGH : LOW);

  loggerWrite(LOG_INFO, F("Buzzer"), F("setBuzzer"),
              String(F("Buzzer=")) + (shouldBeOn ? F("ON") : F("OFF")) + F(" Evento=") +
                  currentEvent);
}

static void setBuzzer(bool on) {

  enabled = on;
  applyBuzzerOutput();
}

static void startNextBeep() {

  if (remainingBeeps == 0) {

    return;
  }

  remainingBeeps--;
  waitingGap = false;
  stateChangedAt = millis();
  setBuzzer(true);
}

void buzzerBegin() {

  pinMode(BUZZER_PIN, OUTPUT);

  currentEvent = F("INICIALIZACAO");
  setBuzzer(false);
}

void buzzerStartupBeep() {

  currentEvent = F("INICIALIZACAO");
  setBuzzer(true);
  delay(120);
  setBuzzer(false);
}

void buzzerRequestBeeps(uint8_t count, uint32_t onMs, uint32_t offMs, const char *eventName) {

  if (count == 0 || onMs == 0) {

    return;
  }

  currentEvent = eventName;
  remainingBeeps = count;
  beepDurationMs = onMs;
  gapDurationMs = offMs;
  waitingGap = false;

  startNextBeep();
}

void buzzerRequestDuration(uint32_t durationMs, const char *eventName) {

  buzzerRequestBeeps(1, durationMs, 0, eventName);
}

void buzzerLoop() {

  if (enabled && millis() - stateChangedAt >= beepDurationMs) {

    setBuzzer(false);
    stateChangedAt = millis();
    waitingGap = remainingBeeps > 0;

    return;
  }

  if (waitingGap && millis() - stateChangedAt >= gapDurationMs) {

    startNextBeep();
  }
}

bool buzzerIsOn() {

  return enabled;
}

// ALERTAS_PATCH: usado para esperar o fim de um padrão antes de iniciar outro.
bool buzzerIsBusy() {

  return enabled || waitingGap || remainingBeeps > 0;
}
