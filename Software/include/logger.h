#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <LittleFS.h>

// ========== KONFIGURACJA ==========
#define MAX_LOG_LINES 200
#define LOG_DEDUP_INTERVAL 5000   // 5 sekund
#define MAX_DEDUP_HISTORY 30

// ========== POZIOMY LOGOWANIA ==========
enum LogLevel {
  LOG_DEBUG = 0,
  LOG_INFO = 1,
  LOG_WARN = 2,
  LOG_ERROR = 3
};

// ========== TRYBY PRZECHOWYWANIA ==========
enum LogStorageMode {
  STORAGE_RAM_ONLY = 0,
  STORAGE_FLASH = 1
};

// ========== STRUKTURA DLA DEDUPLIKACJI ==========
struct DedupEntry {
  char message[128];
  unsigned long lastSend;
  bool active;
};

// ========== KLASA LOGGER ==========
class Logger {
private:
  String logBuffer[MAX_LOG_LINES];
  int bufferHead;
  int bufferCount;
  LogLevel minLevel;
  LogStorageMode storageMode;
  
  // Deduplikacja
  DedupEntry dedupHistory[MAX_DEDUP_HISTORY];
  unsigned long dedupInterval;
  
  String getTimestamp();
  void writeToFile(const String& entry);
  void rotateLogs();
  String getLogFileName(int index);
  
  // Deduplikacja
  int findDedup(const char* message);
  int getFreeDedupSlot();
  void cleanupDedup();
  bool shouldSendDedup(const char* message);
  void markDedupSent(const char* message);
  
public:
  Logger();
  void begin(LogLevel level = LOG_INFO, LogStorageMode mode = STORAGE_RAM_ONLY);
  
  // Główna metoda logowania
  void log(LogLevel level, const char* tag, const char* format, ...);
  
  // Wrappery
  void debug(const char* tag, const char* format, ...);
  void info(const char* tag, const char* format, ...);
  void warn(const char* tag, const char* format, ...);
  void error(const char* tag, const char* format, ...);
  
  // Metody z deduplikacją
  bool logDedup(LogLevel level, const char* tag, const char* format, ...);
  
  // Zarządzanie logami
  String getRecentLogs(int lines = 50);
  String getAllLogs();
  void clearLogs();
  
  // Konfiguracja
  void setLogLevel(LogLevel level);
  void setStorageMode(LogStorageMode mode);
  void setDedupInterval(unsigned long intervalMs);
  
  // ========== GETTERY (DODAJ) ==========
  LogLevel getLogLevel() const { return minLevel; }
  LogStorageMode getStorageMode() const { return storageMode; }
  unsigned long getDedupInterval() const { return dedupInterval; }
  
  // Statystyki
  int getLogCount();
};

// ========== ZMIENNA GLOBALNA ==========
extern Logger logger;

// ========== MAKRA DLA ŁATWEGO UŻYCIA ==========

// Normalne logi (bez deduplikacji)
#define LOG_DEBUG(tag, format, ...) logger.debug(tag, format, ##__VA_ARGS__)
#define LOG_INFO(tag, format, ...) logger.info(tag, format, ##__VA_ARGS__)
#define LOG_WARN(tag, format, ...) logger.warn(tag, format, ##__VA_ARGS__)
#define LOG_ERROR(tag, format, ...) logger.error(tag, format, ##__VA_ARGS__)

// Logi z deduplikacją (tylko raz na 5 sekund)
#define LOG_INFO_DEDUP(tag, format, ...) logger.logDedup(LOG_INFO, tag, format, ##__VA_ARGS__)
#define LOG_WARN_DEDUP(tag, format, ...) logger.logDedup(LOG_WARN, tag, format, ##__VA_ARGS__)
#define LOG_ERROR_DEDUP(tag, format, ...) logger.logDedup(LOG_ERROR, tag, format, ##__VA_ARGS__)

#endif