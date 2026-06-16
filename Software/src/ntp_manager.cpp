#include "ntp_manager.h"
#include "globals.h"
#include <esp_task_wdt.h>

NTPManager::NTPManager() {
  ntpServer = "pool.ntp.org";
  gmtOffsetSec = 3600;    // domyślnie CET (UTC+1)
  daylightOffsetSec = 3600; // czas letni (UTC+2)
  lastSyncTime = 0;
  syncInProgress = false;
  syncStartTime = 0;
}

NTPManager ntp;

bool NTPManager::begin(const char* server, long gmtOffset, int daylightOffset) {
  ntpServer = server;
  gmtOffsetSec = gmtOffset;
  daylightOffsetSec = daylightOffset;
  
  // Tylko skonfiguruj, nie czekaj na synchronizację
  configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);
  Serial.printf("NTP: konfiguracja czasu z serwerem %s (nieblokująca)\n", ntpServer);
  
  // Rozpocznij asynchroniczną synchronizację
  syncInProgress = true;
  syncStartTime = millis();
  
  return true;
}

bool NTPManager::update() {
  // Obsługa asynchronicznej synchronizacji
  if (syncInProgress && !isSynced()) {
    // Timeout po 30 sekundach
    if (millis() - syncStartTime > 30000) {
      syncInProgress = false;
      Serial.println("NTP: timeout synchronizacji (będę próbował później)");
    }
    return false;
  }
  
  // Synchronizacja zakończona sukcesem
  if (syncInProgress && isSynced()) {
    syncInProgress = false;
    updateTimeStruct();
    Serial.printf("✅ NTP zsynchronizowany: %s\n", getDateTimeString().c_str());
    return true;
  }
  
  // Regularna aktualizacja czasu (co minutę)
  static unsigned long lastUpdate = 0;
  if (isSynced() && (millis() - lastUpdate > 60000)) {
    lastUpdate = millis();
    updateTimeStruct();
  }
  
  return isSynced();
}

bool NTPManager::isSynced() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
    return false;
  }
  // Sprawdź czy rok jest sensowny (większy niż 2020)
  return (timeinfo.tm_year + 1900) > 2020;
}

void NTPManager::updateTimeStruct() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 500)) {
    return;
  }
  
  czasNTP.year = timeinfo.tm_year + 1900;
  czasNTP.month = timeinfo.tm_mon + 1;
  czasNTP.day = timeinfo.tm_mday;
  czasNTP.hour = timeinfo.tm_hour;
  czasNTP.minute = timeinfo.tm_min;
  czasNTP.second = timeinfo.tm_sec;
  czasNTP.dayOfWeek = timeinfo.tm_wday + 1;
  czasNTP.dayOfYear = timeinfo.tm_yday;
  czasNTP.timestamp = time(nullptr);
  czasNTP.synced = true;
  
  lastSyncTime = millis();
}

String NTPManager::getTimeString() {
  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", 
           czasNTP.hour, czasNTP.minute, czasNTP.second);
  return String(buffer);
}

String NTPManager::getDateString() {
  char buffer[11];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", 
           czasNTP.year, czasNTP.month, czasNTP.day);
  return String(buffer);
}

String NTPManager::getDateTimeString() {
  return getDateString() + " " + getTimeString();
}

bool NTPManager::isNewDay() {
  static int lastDayOfYear = -1;
  
  if (!isSynced()) return false;
  
  int currentDayOfYear = czasNTP.dayOfYear;
  
  if (lastDayOfYear == -1) {
    lastDayOfYear = currentDayOfYear;
    return false;
  }
  
  if (currentDayOfYear != lastDayOfYear) {
    lastDayOfYear = currentDayOfYear;
    Serial.printf("NTP: nowy dzień! Dzień roku: %d\n", currentDayOfYear);
    return true;
  }
  
  return false;
}

int NTPManager::getHour() { return czasNTP.hour; }
int NTPManager::getMinute() { return czasNTP.minute; }
int NTPManager::getSecond() { return czasNTP.second; }
int NTPManager::getDay() { return czasNTP.day; }
int NTPManager::getMonth() { return czasNTP.month; }
int NTPManager::getYear() { return czasNTP.year; }
int NTPManager::getDayOfWeek() { return czasNTP.dayOfWeek; }
int NTPManager::getDayOfYear() { return czasNTP.dayOfYear; }
unsigned long NTPManager::getTimestamp() { return czasNTP.timestamp; }