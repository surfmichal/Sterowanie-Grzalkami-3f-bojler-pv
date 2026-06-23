#include "logger.h"
#include "ntp_manager.h"
#include <cstdarg>

extern NTPManager ntp;

Logger::Logger() {
  bufferHead = 0;
  bufferCount = 0;
  minLevel = LOG_INFO;
  storageMode = STORAGE_RAM_ONLY;
  dedupInterval = LOG_DEDUP_INTERVAL;
  
  for (int i = 0; i < MAX_LOG_LINES; i++) {
    logBuffer[i] = "";
  }
  
  for (int i = 0; i < MAX_DEDUP_HISTORY; i++) {
    dedupHistory[i].active = false;
    dedupHistory[i].message[0] = '\0';
    dedupHistory[i].lastSend = 0;
  }
}

void Logger::begin(LogLevel level, LogStorageMode mode) {
  minLevel = level;
  storageMode = mode;
  
  if (storageMode == STORAGE_FLASH) {
    if (!LittleFS.begin(true)) {
      Serial.println("Logger: Błąd montowania LittleFS - wyłączam zapis do flash");
      storageMode = STORAGE_RAM_ONLY;
    } else {
      if (!LittleFS.exists("/logs")) {
        LittleFS.mkdir("/logs");
      }
      
      File file = LittleFS.open(getLogFileName(0), "r");
      if (file) {
        while (file.available() && bufferCount < MAX_LOG_LINES) {
          String line = file.readStringUntil('\n');
          if (line.length() > 0) {
            logBuffer[bufferHead] = line;
            bufferHead = (bufferHead + 1) % MAX_LOG_LINES;
            bufferCount++;
          }
        }
        file.close();
      }
    }
  }
  
  log(LOG_INFO, "Logger", "Logger zainicjalizowany (tryb: %s)", 
      storageMode == STORAGE_RAM_ONLY ? "RAM tylko" : "RAM+FLASH");
}

String Logger::getTimestamp() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
  }
  
  unsigned long uptime = millis() / 1000;
  char buffer[30];
  snprintf(buffer, sizeof(buffer), "[UPTIME %lud %02lu:%02lu:%02lu]", 
           uptime / 86400,
           (uptime % 86400) / 3600,
           (uptime % 3600) / 60,
           uptime % 60);
  return String(buffer);
}

// ========== DEDUPLIKACJA ==========
int Logger::findDedup(const char* message) {
  for (int i = 0; i < MAX_DEDUP_HISTORY; i++) {
    if (dedupHistory[i].active && strcmp(dedupHistory[i].message, message) == 0) {
      return i;
    }
  }
  return -1;
}

int Logger::getFreeDedupSlot() {
  for (int i = 0; i < MAX_DEDUP_HISTORY; i++) {
    if (!dedupHistory[i].active) {
      return i;
    }
  }
  unsigned long oldest = millis();
  int oldestIndex = 0;
  for (int i = 0; i < MAX_DEDUP_HISTORY; i++) {
    if (dedupHistory[i].lastSend < oldest) {
      oldest = dedupHistory[i].lastSend;
      oldestIndex = i;
    }
  }
  return oldestIndex;
}

void Logger::cleanupDedup() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_DEDUP_HISTORY; i++) {
    if (dedupHistory[i].active && (now - dedupHistory[i].lastSend > dedupInterval * 2)) {
      dedupHistory[i].active = false;
    }
  }
}

bool Logger::shouldSendDedup(const char* message) {
  cleanupDedup();
  int index = findDedup(message);
  unsigned long now = millis();
  
  if (index != -1) {
    if (now - dedupHistory[index].lastSend < dedupInterval) {
      return false;
    }
    dedupHistory[index].lastSend = now;
    return true;
  } else {
    int slot = getFreeDedupSlot();
    strncpy(dedupHistory[slot].message, message, sizeof(dedupHistory[slot].message) - 1);
    dedupHistory[slot].message[sizeof(dedupHistory[slot].message) - 1] = '\0';
    dedupHistory[slot].lastSend = now;
    dedupHistory[slot].active = true;
    return true;
  }
}

// ========== LOG Z DEDUPLIKACJĄ (TYLKO JEDNA DEFINICJA!) ==========
bool Logger::logDedup(LogLevel level, const char* tag, const char* format, ...) {
  if (level < minLevel) return false;
  
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  
  if (!shouldSendDedup(message)) {
    return false;
  }
  
  const char* levelStr[] = {"DBG", "INF", "WRN", "ERR"};
  String entry = getTimestamp() + " [" + String(levelStr[level]) + "] [" + String(tag) + "] " + String(message);
  
  logBuffer[bufferHead] = entry;
  bufferHead = (bufferHead + 1) % MAX_LOG_LINES;
  if (bufferCount < MAX_LOG_LINES) bufferCount++;
  
  Serial.println(entry);
  
  if (storageMode == STORAGE_FLASH) {
    writeToFile(entry);
  }
  
  return true;
}

// ========== GŁÓWNA METODA LOGOWANIA (bez deduplikacji) ==========
void Logger::log(LogLevel level, const char* tag, const char* format, ...) {
  if (level < minLevel) return;
  
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  
  const char* levelStr[] = {"DBG", "INF", "WRN", "ERR"};
  String entry = getTimestamp() + " [" + String(levelStr[level]) + "] [" + String(tag) + "] " + String(message);
  
  logBuffer[bufferHead] = entry;
  bufferHead = (bufferHead + 1) % MAX_LOG_LINES;
  if (bufferCount < MAX_LOG_LINES) bufferCount++;
  
  Serial.println(entry);
  
  if (storageMode == STORAGE_FLASH) {
    writeToFile(entry);
  }
}

// ========== WRAPPERY ==========
void Logger::debug(const char* tag, const char* format, ...) {
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  log(LOG_DEBUG, tag, "%s", message);
}

void Logger::info(const char* tag, const char* format, ...) {
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  log(LOG_INFO, tag, "%s", message);
}

void Logger::warn(const char* tag, const char* format, ...) {
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  log(LOG_WARN, tag, "%s", message);
}

void Logger::error(const char* tag, const char* format, ...) {
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  log(LOG_ERROR, tag, "%s", message);
}

// ========== METODY POMOCNICZE ==========
void Logger::writeToFile(const String& entry) {
  File file = LittleFS.open(getLogFileName(0), "a");
  if (!file) {
    rotateLogs();
    file = LittleFS.open(getLogFileName(0), "a");
    if (!file) return;
  }
  
  file.println(entry);
  file.close();
  
  file = LittleFS.open(getLogFileName(0), "r");
  if (file && file.size() > 10240) {
    file.close();
    rotateLogs();
  } else if (file) {
    file.close();
  }
}

void Logger::rotateLogs() {
  const int MAX_LOG_FILES = 5;
  
  String oldestFile = getLogFileName(MAX_LOG_FILES - 1);
  if (LittleFS.exists(oldestFile)) {
    LittleFS.remove(oldestFile);
  }
  
  for (int i = MAX_LOG_FILES - 2; i >= 0; i--) {
    String oldName = getLogFileName(i);
    String newName = getLogFileName(i + 1);
    if (LittleFS.exists(oldName)) {
      LittleFS.rename(oldName, newName);
    }
  }
}

String Logger::getLogFileName(int index) {
  return "/logs/log_" + String(index) + ".txt";
}

String Logger::getRecentLogs(int lines) {
  String result;
  int count = min(lines, bufferCount);
  
  for (int i = 0; i < count; i++) {
    int idx = (bufferHead - count + i + MAX_LOG_LINES) % MAX_LOG_LINES;
    if (logBuffer[idx].length() > 0) {
      result += logBuffer[idx] + "\n";
    }
  }
  
  return result;
}

String Logger::getAllLogs() {
  if (storageMode == STORAGE_FLASH) {
    String result;
    for (int i = 4; i >= 0; i--) {
      File file = LittleFS.open(getLogFileName(i), "r");
      if (file) {
        while (file.available()) {
          result += file.readStringUntil('\n') + "\n";
        }
        file.close();
      }
    }
    result += getRecentLogs(MAX_LOG_LINES);
    return result;
  } else {
    return getRecentLogs(MAX_LOG_LINES);
  }
}

void Logger::clearLogs() {
  bufferHead = 0;
  bufferCount = 0;
  
  if (storageMode == STORAGE_FLASH) {
    for (int i = 0; i < 5; i++) {
      String fileName = getLogFileName(i);
      if (LittleFS.exists(fileName)) {
        LittleFS.remove(fileName);
      }
    }
  }
  
  log(LOG_INFO, "Logger", "Logi wyczyszczone");
}

void Logger::setLogLevel(LogLevel level) {
  minLevel = level;
}

void Logger::setStorageMode(LogStorageMode mode) {
  storageMode = mode;
  log(LOG_INFO, "Logger", "Zmiana trybu przechowywania na: %s", 
      storageMode == STORAGE_RAM_ONLY ? "RAM tylko" : "RAM+FLASH");
}

void Logger::setDedupInterval(unsigned long intervalMs) {
  dedupInterval = intervalMs;
}

int Logger::getLogCount() {
  return bufferCount;
}

// ========== DEFINICJA GLOBALNEGO LOGGERA ==========
Logger logger;