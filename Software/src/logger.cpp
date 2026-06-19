#include "logger.h"
#include "ntp_manager.h"

#ifndef MAX_LOG_LINES
#define MAX_LOG_LINES 200
#endif

extern NTPManager ntp;

Logger::Logger() {
  bufferHead = 0;
  bufferCount = 0;
  minLevel = LOG_INFO;
  storageMode = STORAGE_RAM_ONLY;
  
  // Inicjalizuj wszystkie wpisy bufora
  for (int i = 0; i < MAX_LOG_LINES; i++) {
    logBuffer[i] = "";
  }
}

void Logger::begin(LogLevel level, LogStorageMode mode) {
  minLevel = level;
  storageMode = mode;
  
  // Jeśli tryb FLASH, przygotuj system plików
  if (storageMode == STORAGE_FLASH) {
    if (!LittleFS.begin(true)) {
      Serial.println("Logger: Błąd montowania LittleFS - wyłączam zapis do flash");
      storageMode = STORAGE_RAM_ONLY;
    } else {
      // Utwórz folder logów jeśli nie istnieje
      if (!LittleFS.exists("/logs")) {
        LittleFS.mkdir("/logs");
      }
      
      // Załaduj ostatnie logi z pliku do bufora (opcjonalnie)
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
  
  // Próba pobrania aktualnego czasu z RTC
  // Uwaga: getLocalTime(..., 0) nie czeka - natychmiast zwraca true/false
  if (getLocalTime(&timeinfo, 0)) {
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
  }
  
  // Fallback: czas od uruchomienia
  unsigned long uptime = millis() / 1000;
  char buffer[30];
  snprintf(buffer, sizeof(buffer), "[UPTIME %lud %02lu:%02lu:%02lu]", 
           uptime / 86400,
           (uptime % 86400) / 3600,
           (uptime % 3600) / 60,
           uptime % 60);
  return String(buffer);
}

// ========== JEDYNA DEFINICJA LOG() ==========
void Logger::log(LogLevel level, const char* tag, const char* format, ...) {
  if (level < minLevel) return;
  
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  
  const char* levelStr[] = {"DBG", "INF", "WRN", "ERR"};
  
  String entry = getTimestamp() + " [" + String(levelStr[level]) + "] [" + String(tag) + "] " + String(message);
  
  // Dodaj do bufora RAM
  logBuffer[bufferHead] = entry;
  bufferHead = (bufferHead + 1) % MAX_LOG_LINES;
  if (bufferCount < MAX_LOG_LINES) bufferCount++;
  
  // Wypisz na serial
  Serial.println(entry);
  
  // Zapisz do pliku TYLKO w trybie FLASH
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
  
  // Sprawdź rozmiar pliku
  file = LittleFS.open(getLogFileName(0), "r");
  if (file && file.size() > 10240) {  // 10KB
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
    
    // Pomiń puste wpisy
    if (logBuffer[idx].length() > 0) {
      result += logBuffer[idx] + "\n";
    }
  }
  
  return result;
}

String Logger::getAllLogs() {
  if (storageMode == STORAGE_FLASH) {
    // W trybie FLASH – zbierz wszystkie pliki
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
    // W trybie RAM – tylko bufor
    return getRecentLogs(MAX_LOG_LINES);
  }
}

void Logger::clearLogs() {
  // Wyczyść bufor RAM
  bufferHead = 0;
  bufferCount = 0;
  
  // Jeśli tryb FLASH, usuń pliki
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

// Definicja globalnego loggera
Logger logger;