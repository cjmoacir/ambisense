#include "storage_logger.h"

#include <FFat.h>
#include <time.h>

#include "config.h"
#include "ds3231_rtc.h"
#include "logger.h"

static bool storageAvailable = false;

// Verdadeiro só depois que storageLoggerIndex() terminou (contagem de linhas,
// poda e preenchimento do ring). Antes disso o painel recebe "sem leituras".
static bool storageReady = false;

// Verdadeiro enquanto storageLoggerIndex() preenche o ring (leitura interna).
static bool storageIndexing = false;

// Espaço livre e total mantidos em memória: evita consultar a FAT a cada página.
static size_t cachedFreeBytes = 0;
static size_t cachedTotalBytes = 0;

// Uma leitura bruta a cada 2 s: 1.800 linhas por hora e 43.200 em 24 h.
// O histórico fica em arquivos de uma hora que giram sem copiar dados: o
// arquivo atual (leituras.csv) é renomeado ao encher e o mais antigo é apagado.
constexpr uint32_t SEGMENT_LINES = 1800;

// 24 arquivos fechados + o atual garantem ao menos 43.200 linhas.
constexpr uint8_t MAX_ARCHIVES = 24;

// Se o espaço livre cair abaixo disso, os arquivos mais antigos são removidos.
constexpr size_t MIN_FREE_BYTES = 400UL * 1024UL;

constexpr char STORAGE_TEMP_FILE_PATH[] = "/leituras.tmp";
constexpr char STORAGE_BACKUP_FILE_PATH[] = "/leituras.bak";

struct Archive {

  char name[16];

  uint32_t seq;

  // Arquivo herdado do formato antigo (qualquer quantidade de linhas).
  bool legacy;

  uint32_t lines;
};

static Archive archives[MAX_ARCHIVES + 4];
static uint8_t archiveCount = 0;
static uint32_t nextSeq = 1;

// Quantidade de linhas no arquivo atual, atualizada em memória após cada gravação.
static uint32_t storedLineCount = 0;

static SemaphoreHandle_t storageMutex = nullptr;

// Últimas leituras em memória: o painel consulta a cada segundo e assim não
// disputa o sistema de arquivos com a gravação.
constexpr uint16_t RING_LINES = 300;
constexpr uint16_t RING_LINE_BYTES = 256;

static char *ringBuffer = nullptr;
static uint32_t ringTotal = 0;
static SemaphoreHandle_t ringMutex = nullptr;

static void ringPush(const String &line) {

  if (ringBuffer == nullptr || ringMutex == nullptr) {

    return;
  }

  if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(50)) != pdTRUE) {

    return;
  }

  char *slot = ringBuffer + (ringTotal % RING_LINES) * RING_LINE_BYTES;

  strncpy(slot, line.c_str(), RING_LINE_BYTES - 1);
  slot[RING_LINE_BYTES - 1] = '\0';

  ringTotal++;

  xSemaphoreGive(ringMutex);
}

// Entrega as últimas n leituras a partir da memória; falso se ainda não há n.
static bool ringRead(uint32_t n, StorageLineFn fn, void *context) {

  if (ringBuffer == nullptr || ringMutex == nullptr || n == 0 || n > RING_LINES) {

    return false;
  }

  if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(100)) != pdTRUE) {

    return false;
  }

  const uint32_t available = ringTotal;

  xSemaphoreGive(ringMutex);

  if (available < n) {

    return false;
  }

  char copy[RING_LINE_BYTES];

  for (uint32_t index = available - n; index < available; index++) {

    if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(100)) != pdTRUE) {

      return true;
    }

    // Se a linha já foi sobrescrita, encerra com o que foi entregue.
    const bool valid = ringTotal - index <= RING_LINES;

    if (valid) {

      memcpy(copy, ringBuffer + (index % RING_LINES) * RING_LINE_BYTES, RING_LINE_BYTES);
    }

    xSemaphoreGive(ringMutex);

    if (!valid) {

      return true;
    }

    String line(copy);

    line.trim();

    if (line.length() > 0) {

      fn(line, context);
    }
  }

  return true;
}

static void ringFillCallback(const String &line, void *context) {

  (void)context;

  ringPush(line);
}

static bool lockStorage(uint32_t timeoutMs) {

  if (storageMutex == nullptr) {

    return true;
  }

  return xSemaphoreTake(storageMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

static void unlockStorage() {

  if (storageMutex != nullptr) {

    xSemaphoreGive(storageMutex);
  }
}

static void archivePath(const Archive &item, char *path, size_t size) {

  snprintf(path, size, "/%s", item.name);
}

// Recupera o arquivo de histórico se a energia caiu no meio de um expurgo
// feito pela versão antiga do firmware.
static void recoverPendingPurge() {

  if (FFat.exists(STORAGE_FILE_PATH)) {

    FFat.remove(STORAGE_TEMP_FILE_PATH);
    FFat.remove(STORAGE_BACKUP_FILE_PATH);

    return;
  }

  if (FFat.exists(STORAGE_TEMP_FILE_PATH)) {

    FFat.rename(STORAGE_TEMP_FILE_PATH, STORAGE_FILE_PATH);
    FFat.remove(STORAGE_BACKUP_FILE_PATH);

    return;
  }

  if (FFat.exists(STORAGE_BACKUP_FILE_PATH)) {

    FFat.rename(STORAGE_BACKUP_FILE_PATH, STORAGE_FILE_PATH);
  }
}

static uint32_t countLinesOf(const char *path) {

  File file = FFat.open(path, FILE_READ);

  if (!file) {

    return 0;
  }

  uint32_t count = 0;
  uint8_t buffer[512];
  int lastByte = '\n';

  while (true) {

    const int got = file.read(buffer, sizeof(buffer));

    if (got <= 0) {

      break;
    }

    for (int index = 0; index < got; index++) {

      if (buffer[index] == '\n') {

        count++;
      }
    }

    lastByte = buffer[got - 1];
  }

  file.close();

  // Última linha sem quebra de linha também conta.
  if (lastByte != '\n') {

    count++;
  }

  return count;
}

static void addArchive(const char *name, uint32_t seq, bool legacy, uint32_t lines) {

  if (archiveCount >= sizeof(archives) / sizeof(archives[0])) {

    return;
  }

  uint8_t position = archiveCount;

  while (position > 0 && archives[position - 1].seq > seq) {

    archives[position] = archives[position - 1];
    position--;
  }

  Archive &item = archives[position];

  strncpy(item.name, name, sizeof(item.name) - 1);
  item.name[sizeof(item.name) - 1] = '\0';
  item.seq = seq;
  item.legacy = legacy;
  item.lines = lines;

  archiveCount++;

  if (seq >= nextSeq) {

    nextSeq = seq + 1;
  }
}

// Procura os arquivos de uma hora (S0000001.TXT) e os herdados (L0000001.TXT).
static void scanArchives() {

  archiveCount = 0;
  nextSeq = 1;

  File root = FFat.open("/");

  if (!root) {

    return;
  }

  File entry = root.openNextFile();

  while (entry) {

    String name = entry.name();

    entry.close();

    if (name.startsWith("/")) {

      name.remove(0, 1);
    }

    name.toUpperCase();

    if (name.length() == 12 && (name[0] == 'S' || name[0] == 'L') && name.endsWith(".TXT")) {

      const uint32_t seq = static_cast<uint32_t>(name.substring(1, 8).toInt());
      const bool legacy = name[0] == 'L';

      uint32_t lines = SEGMENT_LINES;

      if (legacy) {

        lines = countLinesOf((String("/") + name).c_str());
      }

      addArchive(name.c_str(), seq, legacy, lines);
    }

    entry = root.openNextFile();
  }

  root.close();
}

static void removeOldestArchive() {

  if (archiveCount == 0) {

    return;
  }

  char path[20];

  archivePath(archives[0], path, sizeof(path));

  File old = FFat.open(path, FILE_READ);

  if (old) {

    cachedFreeBytes += old.size();
    old.close();
  }

  FFat.remove(path);

  for (uint8_t index = 1; index < archiveCount; index++) {

    archives[index - 1] = archives[index];
  }

  archiveCount--;
}

static void pruneArchives() {

  while (archiveCount > MAX_ARCHIVES || (archiveCount > 0 && cachedFreeBytes < MIN_FREE_BYTES)) {

    loggerWrite(LOG_INFO, F("Storage"), F("pruneArchives"),
                String(F("Arquivo antigo removido: ")) + archives[0].name);

    removeOldestArchive();
  }
}

// Fecha o arquivo atual como um arquivo de histórico e abre um novo vazio.
static bool rotateCurrentFile() {

  const bool legacy = storedLineCount != SEGMENT_LINES;
  const uint32_t seq = nextSeq;

  char name[16];
  snprintf(name, sizeof(name), "%c%07lu.TXT", legacy ? 'L' : 'S', static_cast<unsigned long>(seq));

  char path[20];
  snprintf(path, sizeof(path), "/%s", name);

  if (!FFat.rename(STORAGE_FILE_PATH, path)) {

    loggerWrite(LOG_ERROR, F("Storage"), F("rotateCurrentFile"),
                F("Falha ao fechar o arquivo da hora"));

    return false;
  }

  addArchive(name, seq, legacy, storedLineCount);

  File file = FFat.open(STORAGE_FILE_PATH, FILE_WRITE);

  if (!file) {

    loggerWrite(LOG_ERROR, F("Storage"), F("rotateCurrentFile"),
                F("Falha ao criar o novo leituras.csv"));

    storedLineCount = 0;

    return false;
  }

  file.close();

  storedLineCount = 0;

  pruneArchives();

  return true;
}

static bool getTimestamp(char *timestamp, size_t timestampSize) {

  if (ds3231Available()) {

    const RtcDateTime now = ds3231Now();

    if (now.year >= 2020 && now.month >= 1 && now.month <= 12 && now.day >= 1 && now.day <= 31 &&
        now.hour <= 23 && now.minute <= 59 && now.second <= 59) {

      snprintf(timestamp, timestampSize, "%04u-%02u-%02u %02u:%02u:%02u", now.year, now.month,
               now.day, now.hour, now.minute, now.second);

      return true;
    }
  }

  struct tm systemTime;

  if (getLocalTime(&systemTime, 0)) {

    strftime(timestamp, timestampSize, "%Y-%m-%d %H:%M:%S", &systemTime);

    return true;
  }

  snprintf(timestamp, timestampSize, "sem_horario");

  return false;
}

bool storageLoggerBegin() {

  storedLineCount = 0;
  archiveCount = 0;

  if (storageMutex == nullptr) {

    storageMutex = xSemaphoreCreateMutex();
  }

  storageAvailable = FFat.begin(false);

  if (!storageAvailable) {

    loggerWrite(LOG_ERROR, F("Storage"), F("storageLoggerBegin"), F("Falha ao montar FATFS"));

    return false;
  }

  recoverPendingPurge();

  if (!FFat.exists(STORAGE_FILE_PATH)) {

    File file = FFat.open(STORAGE_FILE_PATH, FILE_WRITE);

    if (!file) {

      loggerWrite(LOG_ERROR, F("Storage"), F("storageLoggerBegin"),
                  F("Falha ao criar leituras.csv"));

      storageAvailable = false;

      return false;
    }

    file.flush();

    file.close();
  }

  if (ringBuffer == nullptr) {

    ringBuffer = static_cast<char *>(ps_malloc(static_cast<size_t>(RING_LINES) * RING_LINE_BYTES));

    if (ringBuffer == nullptr) {

      ringBuffer = static_cast<char *>(malloc(static_cast<size_t>(RING_LINES) * RING_LINE_BYTES));
    }

    ringMutex = xSemaphoreCreateMutex();
  }

  ringTotal = 0;

  storageReady = false;

  loggerWrite(LOG_INFO, F("Storage"), F("storageLoggerBegin"),
              String(F("FATFS montado: ")) + STORAGE_FILE_PATH);

  return true;
}

// Parte lenta da inicialização (varre arquivos, conta linhas, poda e enche o
// ring em memória). Roda na tarefa de coleta para não atrasar o dashboard.
void storageLoggerIndex() {

  if (!storageAvailable) {

    return;
  }

  storageIndexing = true;

  const uint32_t t0 = millis();

  cachedTotalBytes = FFat.totalBytes();
  cachedFreeBytes = FFat.freeBytes();

  const uint32_t t1 = millis();

  scanArchives();

  const uint32_t t2 = millis();

  storedLineCount = countLinesOf(STORAGE_FILE_PATH);

  // Arquivo maior que uma hora (formato antigo): vira arquivo de histórico.
  if (storedLineCount > SEGMENT_LINES) {

    rotateCurrentFile();
  }

  pruneArchives();

  const uint32_t t3 = millis();

  ringTotal = 0;

  // Carrega do arquivo as últimas leituras para o painel já ter histórico.
  storageLoggerReadWindow(RING_LINES, RING_LINES, ringFillCallback, nullptr);

  storageIndexing = false;
  storageReady = true;

  loggerWrite(LOG_INFO, F("Storage"), F("storageLoggerIndex"),
              String(F("Indice pronto em ")) + (millis() - t0) + F(" ms (FAT ") + (t1 - t0) +
                  F(" ms, arquivos ") + (t2 - t1) + F(" ms, linhas ") + (t3 - t2) +
                  F(" ms, ring ") + (millis() - t3) + F(" ms)"));
}

static bool formatLocked() {

  storageAvailable = false;
  storageReady = false;
  storedLineCount = 0;
  archiveCount = 0;
  nextSeq = 1;

  FFat.end();

  if (!FFat.format()) {

    loggerWrite(LOG_ERROR, F("Storage"), F("storageLoggerFormat"), F("Falha ao formatar FATFS"));

    return false;
  }

  storageAvailable = FFat.begin(false);

  if (!storageAvailable) {

    loggerWrite(LOG_ERROR, F("Storage"), F("storageLoggerFormat"),
                F("FATFS nao montou apos formatacao"));

    return false;
  }

  File file = FFat.open(STORAGE_FILE_PATH, FILE_WRITE);

  if (!file) {

    storageAvailable = false;

    loggerWrite(LOG_ERROR, F("Storage"), F("storageLoggerFormat"),
                F("Falha ao recriar leituras.csv"));

    return false;
  }

  file.flush();
  file.close();

  storedLineCount = 0;
  cachedTotalBytes = FFat.totalBytes();
  cachedFreeBytes = cachedTotalBytes;
  ringTotal = 0;
  storageReady = true;

  loggerWrite(LOG_WARN, F("Storage"), F("storageLoggerFormat"),
              F("Historico apagado e FATFS recriado"));

  return true;
}

bool storageLoggerAppend(const Mq2Reading &mq2, const ThermistorReading &thermistor,
                         const Bme680Reading &bme, const Co2Reading &co2, bool relayOn,
                         bool buzzerOn, bool initialization) {

  if (!storageAvailable || !storageReady) {

    return false;
  }

  String line;
  line.reserve(224);

  char timestamp[24];

  getTimestamp(timestamp, sizeof(timestamp));

  line += timestamp;
  line += ' ';

  line += (F("MQ2_AO="));
  line += (mq2.analogValue);
  line += (F(" MQ2_DO="));
  line += (mq2.digitalValue);
  line += (F(" MHZ_CO2="));

  if (co2.valid) {

    line += (co2.ppm);
  }

  else {

    line += (F("ERRO"));
  }

  if (bme.valid) {

    line += (F(" BME_Temp="));
    line += String(bme.temperature, 1);
    line += (F(" C BME_Umid="));
    line += String(bme.humidity, 1);
    line += (F(" % BME_Pressao="));
    line += String(bme.pressureHpa, 1);
    line += (F(" hPa BME_Gas="));
    line += String(bme.gasKohm, 1);
    line += (F(" kOhm"));
  }

  else {

    line += (F(" BME680=ERRO"));
  }

  if (thermistor.valid) {

    line += (F(" TERM_Temp="));
    line += String(thermistor.temperature, 1);
    line += (F(" C"));
  }

  else {

    line += (F(" TERMISTOR=ERRO"));
  }

  line += (F(" Exaustao_forcada="));
  line += (relayOn ? F("ON") : F("OFF"));

  line += (F(" Buzzer="));
  line += (buzzerOn ? F("ON") : F("OFF"));

  if (initialization) {

    line += (F(" Evento=INICIALIZACAO"));
  }


  ringPush(line);

  if (!lockStorage(1500)) {

    return false;
  }

  // Arquivo da hora cheio: fecha e começa outro antes de gravar.
  if (storedLineCount >= SEGMENT_LINES) {

    rotateCurrentFile();
  }

  File file = FFat.open(STORAGE_FILE_PATH, FILE_APPEND);

  if (!file) {

    unlockStorage();

    loggerWrite(LOG_ERROR, F("Storage"), F("storageLoggerAppend"),
                F("Falha ao abrir leituras.csv"));

    return false;
  }

  file.print(line);
  file.print("\r\n");

  file.flush();
  file.close();

  storedLineCount++;

  {
    const size_t written = line.length() + 2;

    cachedFreeBytes = cachedFreeBytes > written ? cachedFreeBytes - written : 0;
  }

  unlockStorage();

  return true;
}

bool storageLoggerFormat() {

  if (!lockStorage(5000)) {

    return false;
  }

  const bool ok = formatLocked();

  unlockStorage();

  return ok;
}

uint32_t storageLoggerTotalLines() {

  uint32_t total = storedLineCount;

  for (uint8_t index = 0; index < archiveCount; index++) {

    total += archives[index].lines;
  }

  return total;
}

// Lista os arquivos do mais antigo para o mais novo; o último é o atual.
struct Piece {

  char path[20];

  uint32_t lines;
};

static uint8_t buildPieces(Piece *pieces, uint8_t capacity) {

  uint8_t count = 0;

  for (uint8_t index = 0; index < archiveCount && count < capacity; index++) {

    archivePath(archives[index], pieces[count].path, sizeof(pieces[count].path));
    pieces[count].lines = archives[index].lines;
    count++;
  }

  if (count < capacity) {

    strncpy(pieces[count].path, STORAGE_FILE_PATH, sizeof(pieces[count].path));
    pieces[count].lines = storedLineCount;
    count++;
  }

  return count;
}

static void emitLine(String &line, StorageLineFn fn, void *context) {

  line.trim();

  if (line.length() > 0) {

    fn(line, context);
  }

  line = "";
}

// Posição de byte onde começam as últimas "count" linhas do arquivo.
static uint32_t tailOffset(File &file, uint32_t count) {

  const uint32_t size = file.size();

  if (size == 0 || count == 0) {

    return size;
  }

  // Ignora a quebra de linha final.
  uint32_t end = size;

  file.seek(size - 1, SeekSet);

  if (file.read() == '\n') {

    end--;
  }

  // Lê o arquivo de trás para frente em blocos de 512 bytes. Antes era um
  // seek+read por byte (dezenas de milhares na carga do ring: ~70 s no boot).
  uint8_t block[512];
  uint32_t newlines = 0;

  while (end > 0) {

    const uint32_t start = end > sizeof(block) ? end - sizeof(block) : 0;
    const uint32_t length = end - start;

    file.seek(start, SeekSet);

    if (file.read(block, length) != length) {

      return 0;
    }

    for (int index = static_cast<int>(length) - 1; index >= 0; index--) {

      if (block[index] == '\n') {

        newlines++;

        if (newlines == count) {

          return start + index + 1;
        }
      }
    }

    end = start;
  }

  return 0;
}

static void emitFrom(File &file, uint32_t offset, uint32_t maxLines, StorageLineFn fn,
                     void *context) {

  file.seek(offset, SeekSet);

  String line;
  uint32_t emitted = 0;

  while (file.available() && emitted < maxLines) {

    const int character = file.read();

    if (character == '\n') {

      emitLine(line, fn, context);
      emitted++;
    }

    else if (character != '\r') {

      line += static_cast<char>(character);
    }
  }

  if (line.length() > 0 && emitted < maxLines) {

    emitLine(line, fn, context);
  }
}

bool storageLoggerReadWindow(uint32_t n, uint16_t points, StorageLineFn fn, void *context) {

  if (!storageAvailable || (!storageReady && !storageIndexing) || n == 0 || fn == nullptr) {

    return false;
  }

  // Janelas curtas e exatas saem da memória, sem tocar no sistema de arquivos.
  if (n <= RING_LINES && (points == 0 || points >= n) && ringRead(n, fn, context)) {

    return true;
  }

  if (!lockStorage(3000)) {

    return false;
  }

  Piece pieces[MAX_ARCHIVES + 6];
  const uint8_t count = buildPieces(pieces, MAX_ARCHIVES + 6);

  uint32_t total = 0;

  for (uint8_t index = 0; index < count; index++) {

    total += pieces[index].lines;
  }

  if (n > total) {

    n = total;
  }

  if (n == 0) {

    unlockStorage();

    return true;
  }

  if (points == 0 || points > n) {

    points = n > 65535 ? 65535 : static_cast<uint16_t>(n);
  }

  const uint32_t step = (n + points - 1) / points;

  if (step <= 1) {

    // Leitura exata das últimas n linhas, atravessando arquivos se preciso.
    uint32_t take[MAX_ARCHIVES + 6] = {0};
    uint32_t remaining = n;

    for (int index = count - 1; index >= 0 && remaining > 0; index--) {

      take[index] = pieces[index].lines < remaining ? pieces[index].lines : remaining;
      remaining -= take[index];
    }

    for (uint8_t index = 0; index < count; index++) {

      if (take[index] == 0) {

        continue;
      }

      File file = FFat.open(pieces[index].path, FILE_READ);

      if (!file) {

        continue;
      }

      emitFrom(file, tailOffset(file, take[index]), take[index], fn, context);
      file.close();
    }

    unlockStorage();

    return true;
  }

  // Janela longa: amostra leituras brutas espaçadas (sem média). A posição de
  // cada linha é estimada pelo tamanho médio da linha, sem percorrer o arquivo.
  uint32_t first = total - n;
  uint8_t pieceIndex = 0;
  uint32_t pieceStart = 0;

  // Cada arquivo é aberto uma única vez (antes eram até 600 aberturas por
  // requisição, o que travava o painel em janelas de 6 h a 24 h).
  File file;
  int openedPiece = -1;
  uint32_t openedSize = 0;

  for (uint32_t global = first; global < total - 1; global += step) {

    while (pieceIndex < count - 1 && global >= pieceStart + pieces[pieceIndex].lines) {

      pieceStart += pieces[pieceIndex].lines;
      pieceIndex++;
    }

    const Piece &piece = pieces[pieceIndex];

    if (piece.lines == 0) {

      continue;
    }

    if (openedPiece != pieceIndex) {

      if (file) {

        file.close();
      }

      file = FFat.open(piece.path, FILE_READ);
      openedPiece = pieceIndex;
      openedSize = file ? file.size() : 0;
    }

    if (!file) {

      continue;
    }

    const uint32_t within = global - pieceStart;
    uint32_t offset =
        static_cast<uint32_t>((static_cast<uint64_t>(openedSize) * within) / piece.lines);

    file.seek(offset, SeekSet);

    // Uma única leitura em bloco por amostra (antes eram centenas de
    // chamadas read()/available() de 1 byte, o que tornava 1 h a 24 h lento).
    uint8_t chunk[512];
    const size_t got = file.read(chunk, sizeof(chunk));
    size_t position = 0;

    // Alinha no início da próxima linha inteira.
    if (offset > 0) {

      while (position < got && chunk[position] != '\n') {

        position++;
      }

      position++;
    }

    String line;
    line.reserve(224);

    for (; position < got; position++) {

      if (chunk[position] == '\n') {

        break;
      }

      if (chunk[position] != '\r') {

        line += static_cast<char>(chunk[position]);
      }
    }

    emitLine(line, fn, context);
  }

  if (file) {

    file.close();
  }

  // A última leitura é sempre exata.
  {
    File file = FFat.open(pieces[count - 1].path, FILE_READ);

    if (file) {

      emitFrom(file, tailOffset(file, 1), 1, fn, context);
      file.close();
    }
  }

  unlockStorage();

  return true;
}

bool storageLoggerStreamAll(StorageChunkFn fn, void *context) {

  if (!storageAvailable || !storageReady || fn == nullptr) {

    return false;
  }

  Piece pieces[MAX_ARCHIVES + 6];
  uint8_t count = 0;

  if (!lockStorage(3000)) {

    return false;
  }

  count = buildPieces(pieces, MAX_ARCHIVES + 6);
  unlockStorage();

  static uint8_t buffer[4096];

  for (uint8_t index = 0; index < count; index++) {

    uint32_t position = 0;

    while (true) {

      // O arquivo só fica reservado durante cada leitura, para não atrasar a coleta.
      if (!lockStorage(3000)) {

        return false;
      }

      int got = 0;
      File file = FFat.open(pieces[index].path, FILE_READ);

      if (file) {

        file.seek(position, SeekSet);
        got = file.read(buffer, sizeof(buffer));
        file.close();
      }

      unlockStorage();

      if (got <= 0) {

        break;
      }

      position += got;

      if (!fn(buffer, static_cast<size_t>(got), context)) {

        return false;
      }
    }
  }

  return true;
}

bool storageLoggerUsage(size_t &usedBytes, size_t &totalBytes) {

  totalBytes = cachedTotalBytes;
  usedBytes = cachedTotalBytes > cachedFreeBytes ? cachedTotalBytes - cachedFreeBytes : 0;

  return storageReady;
}
