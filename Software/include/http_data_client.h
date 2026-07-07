#ifndef HTTP_DATA_CLIENT_H
#define HTTP_DATA_CLIENT_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "globals.h"

struct HttpData {
  float gridVoltage1;
  float gridVoltage2;
  float gridVoltage3;
  float gridCurrent1;
  float gridCurrent2;
  float gridCurrent3;
  float totalPower;
  float pv1_voltage;
  float pv1_current;
  float pv1_power;
  float pv2_voltage;
  float pv2_current;
  float pv2_power;
  float totalPVPower;
  float dailyEnergy;
  uint32_t totalEnergy;
  float innerTemp;
  float moduleTemp;
  bool connected;
  unsigned long timestamp;
  String lastUpdateTime;
};

class HttpDataClient {
private:
  String apiUrl;
  unsigned long lastFetchTime;
  unsigned long fetchInterval;
  unsigned long timeout;
  uint8_t maxRetries;
  unsigned long retryDelay;
  String lastError;
  int consecutiveFailures;
  
  bool parseResponse(String json, HttpData& data);
  
public:
  HttpDataClient();
  
  void begin(const String& url, unsigned long interval, unsigned long timeout,
             uint8_t maxRetries, unsigned long retryDelay);
  void begin();  // z domyślnymi z config.json
  
  void setEnabled(bool enabled);
  void setUrl(const String& url);
  void setFetchInterval(unsigned long intervalMs);

  void reloadConfig();
  
  bool fetchData(HttpData& data);
  bool fetchDataAsync();  // zapisuje do globalnej modbusData
  
  String getLastError();
  int getConsecutiveFailures(); 
  bool isEnabled() { return (activeDataSource == SOURCE_HTTP); } 
};

#endif