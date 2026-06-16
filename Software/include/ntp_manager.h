#ifndef NTP_MANAGER_H
#define NTP_MANAGER_H

#include <Arduino.h>
#include <time.h>

class NTPManager {
private:
  const char* ntpServer;
  long gmtOffsetSec;
  int daylightOffsetSec;
  unsigned long lastSyncTime;
  
  // Nowe pola dla asynchronicznej synchronizacji
  bool syncInProgress;
  unsigned long syncStartTime;
  
  void updateTimeStruct();
  
public:
  NTPManager();
  
  // Nieblokująca inicjalizacja
  bool begin(const char* server = "pool.ntp.org", 
             long gmtOffset = 3600, 
             int daylightOffset = 3600);
  
  // Wywołuj regularnie w pętli zadania
  bool update();
  bool isSynced();
  
  // Gettery
  int getHour();
  int getMinute();
  int getSecond();
  int getDay();
  int getMonth();
  int getYear();
  int getDayOfWeek();
  int getDayOfYear();
  unsigned long getTimestamp();
  
  // Formatowanie
  String getTimeString();
  String getDateString();
  String getDateTimeString();
  
  // Sprawdzenie nowego dnia
  bool isNewDay();
};

extern NTPManager ntp;

#endif