#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <LittleFS.h>

// Poziomy logowania
enum LogLevel {
  LOG_DEBUG = 0,
  LOG_INFO = 1,
  LOG_WARN = 2,
  LOG_ERROR = 3
};

// Tryb przechowywania logów
enum LogStorageMode {
  STORAGE_RAM_ONLY = 0,     // Tylko RAM (szybko, bez zapisu do flash)
  STORAGE_FLASH = 1         // RAM + LittleFS (trwałe, ale zużywa flash)
};

class Logger {
private:
  static const int MAX_LOG_LINES = 200;     // Zwiększone do 200 linii w RAM
  String logBuffer[MAX_LOG_LINES];
  int bufferHead;
  int bufferCount;
  LogLevel minLevel;
  LogStorageMode storageMode;
  
  void writeToFile(const String& entry);
  void rotateLogs();
  String getLogFileName(int index);
  
public:
  Logger();
  void begin(LogLevel level = LOG_INFO, LogStorageMode mode = STORAGE_RAM_ONLY);
  
  void log(LogLevel level, const char* tag, const char* format, ...);
  void debug(const char* tag, const char* format, ...);
  void info(const char* tag, const char* format, ...);
  void warn(const char* tag, const char* format, ...);
  void error(const char* tag, const char* format, ...);
  
  String getRecentLogs(int lines = 200);
  String getAllLogs();        // Z RAM (bo flash tylko w trybie FLASH)
  void clearLogs();
  void setLogLevel(LogLevel level);
  void setStorageMode(LogStorageMode mode);
  
  // Informacje o stanie
  int getBufferSize() { return bufferCount; }
  LogStorageMode getStorageMode() { return storageMode; }
  
  String getTimestamp();

  LogLevel getLogLevel() { return minLevel; }
};

extern Logger logger;

// Makra dla łatwego użycia
#define LOG_DEBUG(tag, ...) logger.debug(tag, __VA_ARGS__)
#define LOG_INFO(tag, ...)  logger.info(tag, __VA_ARGS__)
#define LOG_WARN(tag, ...)  logger.warn(tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) logger.error(tag, __VA_ARGS__)

#endif