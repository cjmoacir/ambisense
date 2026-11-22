#include "wifi_time.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <FFat.h>
#include <math.h>
#include <time.h>
#include "config.h"
#include "buzzer_control.h"
#include "bme680_sensor.h"
#include "ds3231_rtc.h"
#include "logger.h"
#include "storage_logger.h"
#include "alerts.h"
#include "dashboard_routes.h"
#include "storage_routes.h"
#include "dashboard_html.h"

static bool connected = false;

static WiFiManager wifiManager;

static WebServer storageServer(80);

static bool storageServerStarted = false;

static bool storageRoutesRegistered = false;

static bool storageServerStartRequested = false;

static Preferences preferences;

static String ntpServer1;

static String ntpServer2;

static String ntpServer3;

static long utcOffsetSeconds = UTC_OFFSET_SECONDS;

static int daylightOffsetSeconds = DAYLIGHT_OFFSET_SECONDS;

static uint32_t relayDelaySeconds = 0;

// Intervalo do dashboard, persistido no ESP32 e aplicado em cada nova visita.
static uint32_t dashboardRefreshIntervalMs = 2000;

static WiFiManagerParameter *portalParameters = nullptr;

static String portalHtml;

static uint32_t buttonPressedAt = 0;

static bool portalActive = false;

static bool buttonArmed = true;

static void (*portalStateCallback)(bool active) = nullptr;

// Intervalo usado somente se o RTC ainda não possuir uma hora válida.
constexpr uint32_t NTP_FALLBACK_RETRY_INTERVAL_MS = 30UL * 60UL * 1000UL;

static bool ntpWifiConnected = false;
static bool ntpConfigured = false;
static uint32_t nextNtpAttemptAt = 0;

// O SNTP leva alguns segundos para responder depois do configTime(): confere a
// cada 2 s por até 40 s antes de desistir e esperar o próximo marco de 30 min.
static uint8_t ntpWaitCount = 0;
constexpr uint8_t NTP_WAIT_MAX_CHECKS = 20;

static void sendStoragePage() {

  // Valores mantidos em memória pelo storage_logger (sem varrer a FAT).
  size_t used = 0;
  size_t total = 0;

  storageLoggerUsage(used, total);

  // Envia o HTML (na flash, em pedaços) definido em dashboard_html.h.
  sendDashboardPage(storageServer, dashboardRefreshIntervalMs, used, total);
}

static bool isValidDashboardRefreshInterval(uint32_t intervalMs) {

  switch (intervalMs) {

    case 2000:
    case 5000:
    case 10000:
    case 25000:
    case 60000:
      return true;

    default:
      return false;
  }
}

static void saveDashboardRefreshInterval() {

  if (!storageServer.hasArg("interval")) {

    storageServer.send(400, "text/plain", "Intervalo ausente");
    return;
  }

  const uint32_t intervalMs = storageServer.arg("interval").toInt();

  if (!isValidDashboardRefreshInterval(intervalMs)) {

    storageServer.send(400, "text/plain", "Intervalo invalido");
    return;
  }

  dashboardRefreshIntervalMs = intervalMs;
  preferences.putUInt("dash_interval", dashboardRefreshIntervalMs);

  loggerWrite(LOG_INFO, F("Dashboard"), F("saveDashboardRefreshInterval"),
              String(F("Atualizacao definida para ")) + dashboardRefreshIntervalMs + F(" ms"));

  storageServer.send(200, "text/plain", String(dashboardRefreshIntervalMs));
}

static String dashboardUtcOffsetLabel() {

  const long totalOffset = utcOffsetSeconds + daylightOffsetSeconds;
  const long absoluteOffset = totalOffset < 0 ? -totalOffset : totalOffset;
  char label[16];

  snprintf(label, sizeof(label), "UTC%c%02ld:%02ld", totalOffset < 0 ? '-' : '+',
           absoluteOffset / 3600L, (absoluteOffset % 3600L) / 60L);

  return String(label);
}

static String dashboardNtpServersLabel() {

  String servers;

  const auto appendServer = [&servers](const String &server) {
    if (server.isEmpty()) {

      return;
    }

    if (!servers.isEmpty()) {

      servers += F(", ");
    }

    servers += server;
  };

  appendServer(ntpServer1);
  appendServer(ntpServer2);
  appendServer(ntpServer3);

  return servers.isEmpty() ? String(F("não configurado")) : servers;
}

static void sendDashboardClockApi() {

  RtcDateTime dateTime{};
  bool valid = false;
  String source = F("NTP");

  // Estado do RTC para diagnóstico no painel.
  String rtcState = F("não detectado");

  if (ds3231Available()) {

    rtcState = F("sem hora válida");

    dateTime = ds3231Now();
    valid = dateTime.year >= 2020 && dateTime.month >= 1 && dateTime.month <= 12 &&
            dateTime.day >= 1 && dateTime.day <= 31 && dateTime.hour <= 23 &&
            dateTime.minute <= 59 && dateTime.second <= 59;

    if (valid) {

      source = F("DS3231");
      rtcState = F("ok");
    }
  }

  if (!valid) {

    struct tm systemTime;

    if (getLocalTime(&systemTime, 0)) {

      dateTime.year = systemTime.tm_year + 1900;
      dateTime.month = systemTime.tm_mon + 1;
      dateTime.day = systemTime.tm_mday;
      dateTime.hour = systemTime.tm_hour;
      dateTime.minute = systemTime.tm_min;
      dateTime.second = systemTime.tm_sec;
      valid = true;
    }
  }

  if (!valid) {

    storageServer.send(503, "application/json; charset=utf-8",
                       String(F("{\"valid\":false,\"rtc\":\"")) + rtcState + F("\"}"));
    return;
  }

  char dateTimeText[24];
  snprintf(dateTimeText, sizeof(dateTimeText), "%02u/%02u/%04u %02u:%02u:%02u", dateTime.day,
           dateTime.month, dateTime.year, dateTime.hour, dateTime.minute, dateTime.second);

  const String response = String(F("{\"valid\":true,\"time\":\"")) + dateTimeText +
                          F("\",\"source\":\"") + source + F("\",\"rtc\":\"") + rtcState +
                          F("\",\"timezone\":\"") + dashboardUtcOffsetLabel() + F("\",\"dst\":\"") +
                          (daylightOffsetSeconds == 0 ? F("Não") : F("Sim")) + F("\",\"ntp\":\"") +
                          dashboardNtpServersLabel() + F("\"}");

  storageServer.send(200, "application/json; charset=utf-8", response);
}

static void sendLatestReadingApi() {

  File file = FFat.open(STORAGE_FILE_PATH, FILE_READ);

  if (!file) {
    storageServer.send(404, "text/plain", "");
    return;
  }

  String reversedLine;
  uint32_t position = file.size();

  while (position > 0) {

    position--;
    file.seek(position, SeekSet);

    const int character = file.read();

    if (character == '\n') {

      if (reversedLine.length() > 0) {

        break;
      }
    }

    else if (character != '\r') {

      reversedLine += static_cast<char>(character);
    }
  }

  file.close();

  String last;
  last.reserve(reversedLine.length());

  for (int index = static_cast<int>(reversedLine.length()) - 1; index >= 0; index--) {

    last += reversedLine[index];
  }

  last.trim();
  storageServer.send(200, "text/plain; charset=utf-8", last);
}

struct HistoryStream {

  String buffer;

  bool first;
};

static void historyLine(const String &line, void *context) {

  HistoryStream *stream = static_cast<HistoryStream *>(context);

  if (!stream->first) {

    stream->buffer += ',';
  }

  stream->first = false;

  stream->buffer += '"';
  stream->buffer += line;
  stream->buffer += '"';

  if (stream->buffer.length() > 1200) {

    storageServer.sendContent(stream->buffer);
    stream->buffer = "";
  }
}

// Últimas leituras em JSON. "limite" = quantas leituras olhar para trás (até
// 43200 = 24 h a cada 2 s) e "pontos" = máximo de leituras devolvidas; quando
// há mais leituras que pontos, devolve leituras brutas espaçadas.
static void sendHistoryApi() {

  uint32_t limit = 1;

  if (storageServer.hasArg("limite")) {

    const long requested = storageServer.arg("limite").toInt();

    if (requested > 0 && requested <= 43200) {

      limit = static_cast<uint32_t>(requested);
    }
  }

  uint16_t points = limit <= 300 ? static_cast<uint16_t>(limit) : 600;

  if (storageServer.hasArg("pontos")) {

    const long requested = storageServer.arg("pontos").toInt();

    if (requested > 0 && requested <= 1200) {

      points = static_cast<uint16_t>(requested);
    }
  }

  storageServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  storageServer.send(200, "application/json; charset=utf-8", "");
  storageServer.sendContent(F("{\"total\":"));
  storageServer.sendContent(String(storageLoggerTotalLines()));
  storageServer.sendContent(F(",\"lines\":["));

  HistoryStream stream;
  stream.first = true;
  stream.buffer.reserve(1500);

  const uint32_t readStartedAt = millis();

  storageLoggerReadWindow(limit, points, historyLine, &stream);

  const uint32_t readElapsed = millis() - readStartedAt;

  // Diagnóstico: leituras longas do histórico aparecem na serial.
  if (readElapsed > 1000) {

    loggerWrite(LOG_WARN, F("Dashboard"), F("sendHistoryApi"),
                String(F("Historico limite=")) + limit + F(" pontos=") + points + F(" levou ") +
                    readElapsed + F(" ms"));
  }

  stream.buffer += F("]}");
  storageServer.sendContent(stream.buffer);
  storageServer.sendContent("");
}

static bool csvChunk(const uint8_t *data, size_t length, void *context) {

  (void)context;

  if (!storageServer.client().connected()) {

    return false;
  }

  storageServer.sendContent(reinterpret_cast<const char *>(data), length);

  return true;
}

static void sendStorageCsv() {

  if (!FFat.exists(STORAGE_FILE_PATH)) {

    storageServer.send(404, "text/plain", "leituras.csv nao encontrado");
    return;
  }

  storageServer.sendHeader("Content-Disposition", "attachment; filename=leituras.csv");
  storageServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  storageServer.send(200, "text/csv; charset=utf-8", "");

  storageLoggerStreamAll(csvChunk, nullptr);

  storageServer.sendContent("");
}

static void lastLine(const String &line, void *context) {

  String *page = static_cast<String *>(context);

  *page += line;
  *page += '\n';
}

static void sendLastReadings() {

  String page = F("<!doctype html><html><meta charset='utf-8'><title>Ultimas "
                  "leituras</title><body><h1>Ultimas leituras</h1><pre>");

  storageLoggerReadWindow(10, 10, lastLine, &page);

  page += F("</pre><p><a href='/'>Voltar</a></p></body></html>");
  storageServer.send(200, "text/html; charset=utf-8", page);
}

static void formatStorageFromServer() {

  if (!storageServer.hasArg("confirmacao") || storageServer.arg("confirmacao") != "APAGAR") {

    storageServer.send(400, "text/plain", "Digite APAGAR para confirmar");
    return;
  }

  const bool formatted = storageLoggerFormat();

  if (formatted) {

    buzzerRequestBeeps(1, 200, 0, "HISTORICO_APAGADO");
  }

  storageServer.send(formatted ? 200 : 500, "text/plain",
                     formatted ? "Historico apagado e FATFS recriado" : "Falha ao formatar FATFS");
}

// ALERTAS_PATCH: leitura e gravação das faixas de alerta sonoro.
static String alertNumberJson(float value) {

  return isfinite(value) ? String(value, 2) : String(F("null"));
}

static void sendAlertsApi() {

  String response = F("{\"alerts\":[");

  for (uint8_t index = 0; index < ALERT_COUNT; index++) {

    const AlertConfig config = alertGetConfig(index);

    if (index > 0) {

      response += ',';
    }

    response += F("{\"id\":\"");
    response += alertId(index);
    response += F("\",\"enabled\":");
    response += config.enabled ? F("true") : F("false");
    response += F(",\"min\":");
    response += alertNumberJson(config.minValue);
    response += F(",\"max\":");
    response += alertNumberJson(config.maxValue);
    response += F(",\"active\":");
    response += alertIsActive(index) ? F("true") : F("false");
    response += '}';
  }

  response += F("]}");
  storageServer.send(200, "application/json", response);
}

// Campo vazio significa "sem limite"; texto que não é número é recusado.
static bool parseAlertLimit(const String &text, float &value) {

  String clean = text;
  clean.trim();
  clean.replace(',', '.');

  if (clean.length() == 0) {

    value = NAN;

    return true;
  }

  char *end = nullptr;
  const float parsed = strtof(clean.c_str(), &end);

  if (end == clean.c_str() || *end != '\0' || !isfinite(parsed)) {

    return false;
  }

  value = parsed;

  return true;
}

static void saveAlertFromDashboard() {

  uint8_t index = 0;

  if (!storageServer.hasArg("id") || !alertIndexFromId(storageServer.arg("id"), index)) {

    storageServer.send(400, "text/plain", "Alerta invalido");
    return;
  }

  AlertConfig config{};
  config.enabled = storageServer.arg("enabled") == "1";

  if (!parseAlertLimit(storageServer.arg("min"), config.minValue) ||
      !parseAlertLimit(storageServer.arg("max"), config.maxValue)) {

    storageServer.send(400, "text/plain", "Limite invalido");
    return;
  }

  if (config.enabled && !isfinite(config.minValue) && !isfinite(config.maxValue)) {

    storageServer.send(400, "text/plain", "Defina ao menos um limite");
    return;
  }

  if (!alertSetConfig(index, config)) {

    storageServer.send(400, "text/plain", "O minimo nao pode ser maior que o maximo");
    return;
  }

  sendAlertsApi();
}

static void startStorageServer() {

  // Sem economia de energia do Wi-Fi: evita respostas lentas ou perdidas.
  WiFi.setSleep(false);


  if (!storageRoutesRegistered) {

    storageServer.on(DashboardRoutes::HOME, HTTP_GET, sendStoragePage);
    storageServer.on(StorageRoutes::DOWNLOAD, HTTP_GET, sendStorageCsv);
    storageServer.on(DashboardRoutes::LATEST_API, HTTP_GET, sendLatestReadingApi);
    storageServer.on(DashboardRoutes::CLOCK_API, HTTP_GET, sendDashboardClockApi);
    storageServer.on(DashboardRoutes::HISTORY_API, HTTP_GET, sendHistoryApi);
    storageServer.on(DashboardRoutes::REFRESH_INTERVAL, HTTP_POST, saveDashboardRefreshInterval);
    storageServer.on(DashboardRoutes::ALERTS_API, HTTP_GET, sendAlertsApi);
    storageServer.on(DashboardRoutes::ALERTS_API, HTTP_POST, saveAlertFromDashboard);
    storageServer.on(StorageRoutes::LATEST_LINES, HTTP_GET, sendLastReadings);
    storageServer.on(StorageRoutes::FORMAT, HTTP_POST, formatStorageFromServer);

    storageRoutesRegistered = true;
  }
  storageServer.begin();
  storageServerStarted = true;

  loggerWrite(LOG_INFO, F("Dashboard"), F("startStorageServer"),
              String(F("HTTP ativo em http://")) + WiFi.localIP().toString() + F("/"));
}

static void ledOff() {

#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
#endif
}

// Calcula a espera até o próximo marco de hora cheia ou meia hora.
static uint32_t millisecondsUntilNextHalfHour(uint8_t minute, uint8_t second) {

  const uint32_t elapsedSeconds = (minute % 30UL) * 60UL + second;
  const uint32_t remainingSeconds = 30UL * 60UL - elapsedSeconds;

  return remainingSeconds * 1000UL;
}

// Agenda a próxima tentativa usando o DS3231; se ele falhar, usa 30 minutos.
static void scheduleNextNtpAttemptFromRtc(uint32_t now) {

  if (ds3231Available()) {

    const RtcDateTime rtcTime = ds3231Now();

    if (rtcTime.minute <= 59 && rtcTime.second <= 59) {

      nextNtpAttemptAt = now + millisecondsUntilNextHalfHour(rtcTime.minute, rtcTime.second);

      return;
    }
  }

  nextNtpAttemptAt = now + NTP_FALLBACK_RETRY_INTERVAL_MS;
}

// Tenta atualizar o DS3231 sem interromper a coleta de sensores.
// Após a primeira tentativa, usa somente os marcos HH:00 e HH:30.
static void updateTimeFromNtpInBackground() {

  connected = WiFi.status() == WL_CONNECTED;

  if (!connected) {

    ntpWifiConnected = false;
    ntpConfigured = false;
    ntpWaitCount = 0;

    return;
  }

  if (!ntpWifiConnected) {

    ntpWifiConnected = true;
    nextNtpAttemptAt = 0;
    buzzerRequestBeeps(1, 200, 0, "WIFI_CONECTADO");
  }

  const uint32_t now = millis();

  if (static_cast<int32_t>(now - nextNtpAttemptAt) < 0) {

    return;
  }

  if (!ntpConfigured) {

    configTime(utcOffsetSeconds, daylightOffsetSeconds, ntpServer1.c_str(), ntpServer2.c_str(),
               ntpServer3.c_str());

    ntpConfigured = true;
  }

  struct tm localTime;

  if (!getLocalTime(&localTime, 0)) {

    if (ntpWaitCount < NTP_WAIT_MAX_CHECKS) {

      ntpWaitCount++;
      nextNtpAttemptAt = now + 2000;

      return;
    }

    ntpWaitCount = 0;

    scheduleNextNtpAttemptFromRtc(now);

    loggerWrite(LOG_WARN, F("NTP"), F("updateTimeFromNtpInBackground"),
                F("Sem internet; nova tentativa no proximo marco de 30 minutos"));

    return;
  }

  RtcDateTime dateTime;
  dateTime.year = localTime.tm_year + 1900;
  dateTime.month = localTime.tm_mon + 1;
  dateTime.day = localTime.tm_mday;
  dateTime.hour = localTime.tm_hour;
  dateTime.minute = localTime.tm_min;
  dateTime.second = localTime.tm_sec;

  ntpWaitCount = 0;

  // Se o RTC não respondeu no boot, tenta detectá-lo de novo antes de gravar.
  if (!ds3231Available()) {

    ds3231Begin();
  }

  const bool rtcWritten = ds3231Set(dateTime);

  // Diagnóstico: confere o que o DS3231 devolve logo após a gravação.
  {
    const RtcDateTime back = ds3231Now();

    loggerWrite(LOG_INFO, F("NTP"), F("updateTimeFromNtpInBackground"),
                String(F("DS3231 gravacao=")) + (rtcWritten ? F("ok") : F("falhou")) +
                    F(" leitura de volta=") + back.day + F("/") + back.month + F("/") + back.year +
                    F(" ") + back.hour + F(":") + back.minute + F(":") + back.second);
  }

  buzzerRequestBeeps(1, 40, 0, "RELOGIO_SINCRONIZADO");
  nextNtpAttemptAt = now + millisecondsUntilNextHalfHour(localTime.tm_min, localTime.tm_sec);

  loggerWrite(LOG_INFO, F("NTP"), F("updateTimeFromNtpInBackground"),
              F("Hora sincronizada; proxima conferencia em hora cheia ou meia hora"));
}

static String selectedOption(const char *value, const char *selected) {

  return String(value) == selected ? F(" selected") : F("");
}

static void savePortalParameters() {

  if (wifiManager.server->hasArg("storage_format_confirm") &&
      wifiManager.server->arg("storage_format_confirm") == "APAGAR") {

    if (storageLoggerFormat()) {

      buzzerRequestBeeps(1, 200, 0, "HISTORICO_APAGADO");

      loggerWrite(LOG_WARN, F("WiFi"), F("savePortalParameters"),
                  F("FATFS formatado pelo portal WiFi"));
    }
  }

  if (wifiManager.server->hasArg("ntp1"))
    ntpServer1 = wifiManager.server->arg("ntp1");

  if (wifiManager.server->hasArg("ntp2"))
    ntpServer2 = wifiManager.server->arg("ntp2");

  if (wifiManager.server->hasArg("ntp3"))
    ntpServer3 = wifiManager.server->arg("ntp3");

  if (wifiManager.server->hasArg("utc"))
    utcOffsetSeconds = wifiManager.server->arg("utc").toInt();

  if (wifiManager.server->hasArg("dst"))
    daylightOffsetSeconds = wifiManager.server->arg("dst").toInt();

  if (wifiManager.server->hasArg("relay_delay"))
    relayDelaySeconds = wifiManager.server->arg("relay_delay").toInt();

  preferences.putString("ntp1", ntpServer1);
  preferences.putString("ntp2", ntpServer2);
  preferences.putString("ntp3", ntpServer3);
  preferences.putLong("utc", utcOffsetSeconds);
  preferences.putInt("dst", daylightOffsetSeconds);

  preferences.putUInt("relay_delay", relayDelaySeconds);
}

static void loadPortalParameters() {

  preferences.begin("ambisense", false);

  ntpServer1 = preferences.getString("ntp1", NTP_SERVER_1);
  ntpServer2 = preferences.getString("ntp2", NTP_SERVER_2);
  ntpServer3 = preferences.getString("ntp3", NTP_SERVER_3);
  utcOffsetSeconds = preferences.getLong("utc", UTC_OFFSET_SECONDS);
  daylightOffsetSeconds = preferences.getInt("dst", DAYLIGHT_OFFSET_SECONDS);

  relayDelaySeconds = preferences.getUInt("relay_delay", 0);
  dashboardRefreshIntervalMs = preferences.getUInt("dash_interval", 2000);

  if (!isValidDashboardRefreshInterval(dashboardRefreshIntervalMs)) {

    dashboardRefreshIntervalMs = 2000;
  }
}

static void addPortalParameters() {

  const String savedSsid = WiFi.SSID();

  const String savedPassword = wifiManager.getWiFiPass();

  portalHtml =
      String(F("<hr><h3>Configuracao de horario</h3>")) +
      F("<label>Servidor NTP 1</label><input name='ntp1' value='") + ntpServer1 + F("'>") +
      F("<label>Servidor NTP 2</label><input name='ntp2' value='") + ntpServer2 + F("'>") +
      F("<label>Servidor NTP 3</label><input name='ntp3' value='") + ntpServer3 + F("'>") +
      F("<label>Fuso horario</label><select name='utc'>") + F("<option value='-43200'") +
      selectedOption("-43200", String(utcOffsetSeconds).c_str()) +
      F(">UTC-12 Ilha Baker (EUA)</option>") + F("<option value='-39600'") +
      selectedOption("-39600", String(utcOffsetSeconds).c_str()) +
      F(">UTC-11 Pago Pago (Samoa Americana)</option>") + F("<option value='-36000'") +
      selectedOption("-36000", String(utcOffsetSeconds).c_str()) +
      F(">UTC-10 Honolulu (EUA)</option>") + F("<option value='-34200'") +
      selectedOption("-34200", String(utcOffsetSeconds).c_str()) +
      F(">UTC-9:30 Taiohae (Polinesia Francesa)</option>") + F("<option value='-32400'") +
      selectedOption("-32400", String(utcOffsetSeconds).c_str()) +
      F(">UTC-9 Anchorage (EUA)</option>") + F("<option value='-28800'") +
      selectedOption("-28800", String(utcOffsetSeconds).c_str()) +
      F(">UTC-8 Los Angeles (EUA), Vancouver (Canada)</option>") + F("<option value='-25200'") +
      selectedOption("-25200", String(utcOffsetSeconds).c_str()) +
      F(">UTC-7 Denver (EUA)</option>") + F("<option value='-21600'") +
      selectedOption("-21600", String(utcOffsetSeconds).c_str()) +
      F(">UTC-6 Chicago (EUA), Cidade do Mexico (Mexico)</option>") + F("<option value='-18000'") +
      selectedOption("-18000", String(utcOffsetSeconds).c_str()) +
      F(">UTC-5 Nova York (EUA), Toronto (Canada), Lima (Peru)</option>") +
      F("<option value='-14400'") + selectedOption("-14400", String(utcOffsetSeconds).c_str()) +
      F(">UTC-4 Santiago (Chile), Caracas (Venezuela)</option>") + F("<option value='-10800'") +
      selectedOption("-10800", String(utcOffsetSeconds).c_str()) +
      F(">UTC-3 Sao Paulo (Brasil), Buenos Aires (Argentina), Montevideu (Uruguai)</option>") +
      F("<option value='-7200'") + selectedOption("-7200", String(utcOffsetSeconds).c_str()) +
      F(">UTC-2 Fernando de Noronha (Brasil)</option>") + F("<option value='-3600'") +
      selectedOption("-3600", String(utcOffsetSeconds).c_str()) +
      F(">UTC-1 Ponta Delgada (Portugal), Praia (Cabo Verde)</option>") + F("<option value='0'") +
      selectedOption("0", String(utcOffsetSeconds).c_str()) +
      F(">UTC Londres (Reino Unido), Lisboa (Portugal), Reykjavik (Islandia)</option>") +
      F("<option value='3600'") + selectedOption("3600", String(utcOffsetSeconds).c_str()) +
      F(">UTC+1 Paris (Franca), Berlim (Alemanha), Roma (Italia)</option>") +
      F("<option value='7200'") + selectedOption("7200", String(utcOffsetSeconds).c_str()) +
      F(">UTC+2 Cairo (Egito), Atenas (Grecia), Joanesburgo (Africa do Sul)</option>") +
      F("<option value='10800'") + selectedOption("10800", String(utcOffsetSeconds).c_str()) +
      F(">UTC+3 Moscou (Russia), Riad (Arabia Saudita), Nairobi (Quenia)</option>") +
      F("<option value='14400'") + selectedOption("14400", String(utcOffsetSeconds).c_str()) +
      F(">UTC+4 Dubai (Emirados Arabes Unidos), Baku (Azerbaijao)</option>") +
      F("<option value='16200'") + selectedOption("16200", String(utcOffsetSeconds).c_str()) +
      F(">UTC+4:30 Cabul (Afeganistao)</option>") + F("<option value='18000'") +
      selectedOption("18000", String(utcOffsetSeconds).c_str()) +
      F(">UTC+5 Karachi (Paquistao), Tashkent (Uzbequistao)</option>") +
      F("<option value='19800'") + selectedOption("19800", String(utcOffsetSeconds).c_str()) +
      F(">UTC+5:30 Nova Deli (India), Colombo (Sri Lanka)</option>") + F("<option value='20700'") +
      selectedOption("20700", String(utcOffsetSeconds).c_str()) +
      F(">UTC+5:45 Katmandu (Nepal)</option>") + F("<option value='21600'") +
      selectedOption("21600", String(utcOffsetSeconds).c_str()) +
      F(">UTC+6 Daca (Bangladesh), Almaty (Cazaquistao)</option>") + F("<option value='23400'") +
      selectedOption("23400", String(utcOffsetSeconds).c_str()) +
      F(">UTC+6:30 Yangon (Myanmar)</option>") + F("<option value='25200'") +
      selectedOption("25200", String(utcOffsetSeconds).c_str()) +
      F(">UTC+7 Bangkok (Tailandia), Jacarta (Indonesia)</option>") + F("<option value='28800'") +
      selectedOption("28800", String(utcOffsetSeconds).c_str()) +
      F(">UTC+8 Pequim (China), Singapura (Singapura), Manila (Filipinas)</option>") +
      F("<option value='31500'") + selectedOption("31500", String(utcOffsetSeconds).c_str()) +
      F(">UTC+8:45 Eucla (Australia)</option>") + F("<option value='32400'") +
      selectedOption("32400", String(utcOffsetSeconds).c_str()) +
      F(">UTC+9 Toquio (Japao), Seul (Coreia do Sul)</option>") + F("<option value='34200'") +
      selectedOption("34200", String(utcOffsetSeconds).c_str()) +
      F(">UTC+9:30 Adelaide (Australia)</option>") + F("<option value='36000'") +
      selectedOption("36000", String(utcOffsetSeconds).c_str()) +
      F(">UTC+10 Sydney (Australia)</option>") + F("<option value='37800'") +
      selectedOption("37800", String(utcOffsetSeconds).c_str()) +
      F(">UTC+10:30 Ilha Lord Howe (Australia)</option>") + F("<option value='39600'") +
      selectedOption("39600", String(utcOffsetSeconds).c_str()) +
      F(">UTC+11 Noumea (Nova Caledonia), Honiara (Ilhas Salomao)</option>") +
      F("<option value='43200'") + selectedOption("43200", String(utcOffsetSeconds).c_str()) +
      F(">UTC+12 Auckland (Nova Zelandia), Suva (Fiji)</option>") + F("<option value='45900'") +
      selectedOption("45900", String(utcOffsetSeconds).c_str()) +
      F(">UTC+12:45 Ilhas Chatham (Nova Zelandia)</option>") + F("<option value='46800'") +
      selectedOption("46800", String(utcOffsetSeconds).c_str()) +
      F(">UTC+13 Apia (Samoa), Nuku'alofa (Tonga)</option>") + F("<option value='50400'") +
      selectedOption("50400", String(utcOffsetSeconds).c_str()) +
      F(">UTC+14 Kiritimati (Kiribati)</option>") +
      F("</select><label>Horario de verao</label><select name='dst'>") + F("<option value='0'") +
      selectedOption("0", String(daylightOffsetSeconds).c_str()) + F(">Nao - 0 horas</option>") +
      F("<option value='3600'") + selectedOption("3600", String(daylightOffsetSeconds).c_str()) +
      F(">Sim - +1 hora</option>") +
      F("</select><label>Atraso para ligar a exaustão forçada (segundos)</label><input "
        "type='number' min='0' name='relay_delay' value='") +
      String(relayDelaySeconds) + F("'>") + F("<hr><h3>Armazenamento</h3>") +
      F("<p>Apaga permanentemente todas as leituras salvas no FATFS.</p>") +
      F("<label>Digite APAGAR para formatar o armazenamento</label>") +
      F("<input name='storage_format_confirm' value='' autocomplete='off'>") + F("<script>") +
      F("window.setTimeout(function(){") +
      F("var s=document.querySelector('input[name=\\'s\\']');") +
      F("var p=document.querySelector('input[name=\\'p\\']');") + F("if(s){s.value='") + savedSsid +
      F("';}") + F("if(p){p.type='text';p.value='") + savedPassword + F("';}") +
      F("document.querySelectorAll('a,button,input[type=submit]').forEach(function(e){") +
      F("if(e.innerText==='Configure WiFi'||e.value==='Configure WiFi'){") +
      F("if(e.innerText)e.innerText='Configure';") +
      F("if(e.value)e.value='Configure';}});},500);") + F("</script>");

  portalParameters = new WiFiManagerParameter(portalHtml.c_str());
  wifiManager.addParameter(portalParameters);
  wifiManager.setSaveParamsCallback(savePortalParameters);
}

void wifiTimeBegin() {

  // Botao ligado entre GPIO1 e 3V3, com resistor externo de 10 kOhm para GND.
  // O pull-down interno mantém o nível baixo estável se o botão não estiver pressionado.
  pinMode(WIFI_RESET_PIN, INPUT_PULLDOWN);

  ledOff();

  loadPortalParameters();

  // Aplica o fuso horário já no boot: depois de um reset o relógio do sistema
  // guarda a hora em UTC, e sem isso as primeiras linhas saem 3 h adiantadas.
  configTime(utcOffsetSeconds, daylightOffsetSeconds, ntpServer1.c_str(), ntpServer2.c_str(),
             ntpServer3.c_str());

  addPortalParameters();

  wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SECONDS);

  wifiManager.setConnectTimeout(30);

  WiFi.mode(WIFI_STA);

  WiFi.setSleep(false);

  WiFi.setAutoReconnect(true);

  loggerWrite(LOG_INFO, F("WiFi"), F("wifiTimeBegin"), String(F("MAC WiFi: ")) + WiFi.macAddress());

  if (wifiManager.getWiFiSSID().length() > 0) {

    // Já existe rede salva: conecta em segundo plano, sem bloquear o boot.
    // O loop() sobe o dashboard assim que houver IP e tenta reconectar a
    // cada 30 s se o roteador estiver fora; o portal só abre pelo botão.
    WiFi.begin();

    loggerWrite(LOG_INFO, F("WiFi"), F("wifiTimeBegin"),
                String(F("Conectando em segundo plano a ")) + wifiManager.getWiFiSSID());
  }

  else {

    // Primeira configuração: sem rede salva, abre o portal e aguarda.
    connected = wifiManager.autoConnect(WIFI_PORTAL_NAME);
  }

  storageServerStartRequested = true;
}

void wifiTimeLoop() {

  updateTimeFromNtpInBackground();

  // Se o Wi-Fi caiu (ou não existia no boot), tenta reconectar a cada 30 s
  // com as credenciais salvas, sem abrir o portal.
  static uint32_t lastReconnectAttemptAt = 0;

  if (!portalActive && WiFi.status() != WL_CONNECTED &&
      millis() - lastReconnectAttemptAt >= 30000UL) {

    lastReconnectAttemptAt = millis();

    if (WiFi.SSID().length() > 0) {

      WiFi.reconnect();
    }
  }

  // Inicia o dashboard sempre que houver Wi-Fi e o servidor ainda não estiver
  // ativo, mesmo que a conexão só volte depois do boot.
  if ((storageServerStartRequested || !storageServerStarted) && !portalActive &&
      WiFi.status() == WL_CONNECTED) {

    storageServerStartRequested = false;

    startStorageServer();
  }

  if (storageServerStarted && WiFi.status() == WL_CONNECTED && !portalActive) {

    const uint32_t handleStartedAt = millis();

    storageServer.handleClient();

    // Diagnóstico: um atendimento lento do painel aparece na serial.
    const uint32_t handleElapsed = millis() - handleStartedAt;

    if (handleElapsed > 1000) {

      loggerWrite(LOG_WARN, F("WiFi"), F("handleClient"),
                  String(F("Atendimento do painel demorou ")) + handleElapsed + F(" ms"));
    }
  }

  const bool buttonPressed = digitalRead(WIFI_RESET_PIN) == (WIFI_BUTTON_ACTIVE_HIGH ? HIGH : LOW);

  if (buttonPressed) {

    if (buttonPressedAt == 0) {

      buttonPressedAt = millis();
    }

    if (buttonArmed && !portalActive && millis() - buttonPressedAt >= WIFI_PORTAL_HOLD_MS) {

      portalActive = true;

      buttonArmed = false;

      if (portalStateCallback != nullptr) {

        portalStateCallback(true);
      }

      if (storageServerStarted) {

        storageServer.stop();

        storageServerStarted = false;
      }

      loggerWrite(LOG_INFO, F("WiFi"), F("wifiTimeLoop"),
                  F("Botao pressionado por 5 segundos; AmbiSense-Setup ativado"));

      wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SECONDS);

      const bool confirmed = wifiManager.startConfigPortal(WIFI_PORTAL_NAME);

      if (confirmed) {

        loggerWrite(LOG_INFO, F("WiFi"), F("startConfigPortal"),
                    String("SSID gravado: ") + wifiManager.getWiFiSSID());

        loggerWrite(LOG_INFO, F("WiFi"), F("startConfigPortal"),
                    String("Senha gravada: ") + wifiManager.getWiFiPass());

        loggerWrite(LOG_INFO, F("WiFi"), F("startConfigPortal"),
                    F("Nova rede confirmada; senha protegida"));

        loggerWrite(LOG_INFO, F("Sistema"), F("wifiTimeLoop"), F("ESP sera reiniciado"));

        ledOff();

        delay(500);

        ESP.restart();
      }

      else {

        loggerWrite(LOG_WARN, F("WiFi"), F("startConfigPortal"),
                    F("Portal encerrado sem confirmar nova rede"));

        if (portalStateCallback != nullptr) {

          portalStateCallback(false);
        }

        storageServerStartRequested = WiFi.status() == WL_CONNECTED;
      }

      portalActive = false;
    }
  }

  else {

    const uint32_t buttonPressDuration = buttonPressedAt == 0 ? 0 : millis() - buttonPressedAt;

    if (buttonArmed && buttonPressDuration >= WIFI_RESTART_HOLD_MS &&
        buttonPressDuration < WIFI_PORTAL_HOLD_MS) {

      loggerWrite(LOG_INFO, F("Sistema"), F("wifiTimeLoop"),
                  F("Botao solto apos 0,5 segundo; reiniciando ESP32"));

      delay(100);
      ESP.restart();
    }

    buttonPressedAt = 0;

    buttonArmed = true;
  }
}

bool wifiTimeConnected() {

  return connected;
}

uint32_t wifiTimeRelayDelaySeconds() {

  return relayDelaySeconds;
}

void wifiTimeSetPortalStateCallback(void (*callback)(bool active)) {

  portalStateCallback = callback;
}
