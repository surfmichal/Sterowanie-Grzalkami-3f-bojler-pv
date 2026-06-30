#include "http_data_client.h"
#include "globals.h"
#include <WiFi.h>

HttpDataClient::HttpDataClient() {
  apiUrl = "http://192.168.0.251:8080/api/data";
  lastFetchTime = 0;
  fetchInterval = 5000;
  timeout = 5000;
  maxRetries = 3;
  retryDelay = 1000;
  enabled = false;
}

void HttpDataClient::begin(bool enabled, const String& url, 
                           unsigned long interval, unsigned long timeout,
                           uint8_t maxRetries, unsigned long retryDelay) {
  this->enabled = enabled;
  this->apiUrl = url;
  this->fetchInterval = interval;
  this->timeout = timeout;
  this->maxRetries = maxRetries;
  this->retryDelay = retryDelay;
  
  if (enabled) {
    Serial.printf("🌐 HTTP Data Client: %s (interval: %dms, timeout: %dms)\n", 
                  apiUrl.c_str(), fetchInterval, timeout);
  } else {
    Serial.println("🌐 HTTP Data Client: WYŁĄCZONY");
  }
}

void HttpDataClient::begin() {
  // Sprawdź czy HTTP jest aktywnym źródłem
  if (activeDataSource != SOURCE_HTTP) {
    Serial.println("🌐 HTTP: pomijam inicjalizację - inne źródło danych");
    enabled = false;
    return;
  }

  begin(http_data_cfg.enabled, 
        http_data_cfg.addr,
        http_data_cfg.interval,
        http_data_cfg.timeout,
        http_data_cfg.max_retries,
        http_data_cfg.retry_delay);
}

void HttpDataClient::setEnabled(bool enabled) {
  this->enabled = enabled;
}

void HttpDataClient::setUrl(const String& url) {
  this->apiUrl = url;
}

void HttpDataClient::setFetchInterval(unsigned long intervalMs) {
  this->fetchInterval = intervalMs;
}

bool HttpDataClient::parseResponse(String json, HttpData& data) {
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    lastError = "JSON parse error: " + String(error.c_str());
    return false;
  }
  
  const char* status = doc["status"] | "Offline";
  data.connected = (strcmp(status, "Online") == 0);
  
  if (data.connected) {
    data.gridVoltage1 = doc["grid"]["v1"] | 0.0;
    data.gridVoltage2 = doc["grid"]["v2"] | 0.0;
    data.gridVoltage3 = doc["grid"]["v3"] | 0.0;
    data.gridCurrent1 = doc["grid"]["a1"] | 0.0;
    data.gridCurrent2 = doc["grid"]["a2"] | 0.0;
    data.gridCurrent3 = doc["grid"]["a3"] | 0.0;
    data.totalPower = doc["total_power"] | 0.0;
    
    data.pv1_voltage = doc["pv"]["p1_voltage"] | 0.0;
    data.pv1_current = doc["pv"]["p1_current"] | 0.0;
    data.pv1_power = doc["pv"]["p1_power"] | 0.0;
    data.pv2_voltage = doc["pv"]["p2_voltage"] | 0.0;
    data.pv2_current = doc["pv"]["p2_current"] | 0.0;
    data.pv2_power = doc["pv"]["p2_power"] | 0.0;
    data.totalPVPower = doc["pv"]["total_power"] | 0.0;
    
    data.dailyEnergy = doc["daily_energy"] | 0.0;
    data.totalEnergy = doc["total_energy"] | 0;
    data.innerTemp = doc["temp_inner"] | 0.0;
    data.moduleTemp = doc["temp_module"] | 0.0;
    data.lastUpdateTime = doc["last_update"] | "";
    
    data.timestamp = millis();
    return true;
  } else {
    data.lastUpdateTime = doc["komunikat"] | "";
    return false;
  }
}

bool HttpDataClient::fetchData(HttpData& data) {
  if (!enabled) {
    lastError = "HTTP Data Client disabled";
    return false;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    lastError = "WiFi not connected";
    data.connected = false;
    return false;
  }
  
  for (uint8_t attempt = 0; attempt < maxRetries; attempt++) {
    if (attempt > 0) {
      delay(retryDelay);
    }
    
    HTTPClient http;
    http.begin(apiUrl);
    http.setTimeout(timeout);
    
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      http.end();
      
      bool success = parseResponse(payload, data);
      if (success) {
        lastError = "";
        return true;
      }
      lastError = "Parse error";
    } else {
      lastError = "HTTP error: " + String(httpCode);
    }
    
    http.end();
  }
  
  data.connected = false;
  return false;
}

bool HttpDataClient::fetchDataAsync() {
  if (!enabled) {
    // Jeśli HTTP jest wyłączone, ustaw inverterData na offline
    inverterData.httpConnected = false;
    return false;
  }
  
  HttpData data;
  bool success = fetchData(data);
  
  if (success) {
    inverterData.httpConnected = true;
    inverterData.gridVoltage1 = data.gridVoltage1;
    inverterData.gridVoltage2 = data.gridVoltage2;
    inverterData.gridVoltage3 = data.gridVoltage3;
    inverterData.gridCurrent1 = data.gridCurrent1;
    inverterData.gridCurrent2 = data.gridCurrent2;
    inverterData.gridCurrent3 = data.gridCurrent3;
    inverterData.power = data.totalPower;
    inverterData.pv1_voltage = data.pv1_voltage;
    inverterData.pv1_current = data.pv1_current;
    inverterData.pv1_power = data.pv1_power;
    inverterData.pv2_voltage = data.pv2_voltage;
    inverterData.pv2_current = data.pv2_current;
    inverterData.pv2_power = data.pv2_power;
    inverterData.total_pv_power = data.totalPVPower;
    inverterData.dailyEnergy = data.dailyEnergy;
    inverterData.totalEnergy = data.totalEnergy;
    inverterData.innerTemp = data.innerTemp;
    inverterData.moduleTemp = data.moduleTemp;
    inverterData.timestamp = millis();
    return true;
  } else {
    inverterData.httpConnected = false;
    return false;
  }
}

String HttpDataClient::getLastError() {
  return lastError;
}