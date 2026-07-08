#include "http_data_client.h"
#include "globals.h"
#include <WiFi.h>

// ========== KONSTRUKTOR ==========
HttpDataClient::HttpDataClient() {
  apiUrl = "http://192.168.0.251:8080/api/data";
  lastFetchTime = 0;
  fetchInterval = 5000;
  timeout = 5000;
  maxRetries = 3;
  retryDelay = 1000;
  //enabled = false;
  lastError = "";
  consecutiveFailures = 0;
}

// ========== INICJALIZACJA Z PARAMETRAMI ==========
void HttpDataClient::begin(const String& url, unsigned long interval, 
                            unsigned long timeout, uint8_t maxRetries, 
                            unsigned long retryDelay) {  
  this->apiUrl = url;
  this->fetchInterval = interval;
  this->timeout = timeout;
  this->maxRetries = maxRetries;
  this->retryDelay = retryDelay;
    
  Serial.printf("🌐 HTTP Data Client: %s (interval: %dms, timeout: %dms)\n", 
                  apiUrl.c_str(), fetchInterval, timeout);  
}

// ========== INICJALIZACJA BEZ PARAMETRÓW ==========
void HttpDataClient::begin() {
  if (activeDataSource != SOURCE_HTTP) {
    Serial.println("🌐 HTTP: pomijam inicjalizację - inne źródło danych");    
    return;
  }
 
  apiUrl = String(httpDataCfg.addr);
  fetchInterval = httpDataCfg.interval;
  timeout = httpDataCfg.timeout;
  maxRetries = httpDataCfg.maxRetries;
  retryDelay = httpDataCfg.retryDelay;
  
  Serial.printf("🌐 HTTP Data Client: %s (interval: %dms, timeout: %dms, retries: %d)\n", 
                apiUrl.c_str(), fetchInterval, timeout, maxRetries);
}

// ========== PARSOWANIE ODPOWIEDZI ==========
bool HttpDataClient::parseResponse(String json, HttpData& data) {
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    lastError = "JSON parse error: " + String(error.c_str());
    Serial.printf("❌ [HTTP] Błąd parsowania JSON: %s\n", error.c_str());
    return false;
  }
  
  // Sprawdź status
  const char* status = doc["status"] | "Offline";
  data.connected = (strcmp(status, "Online") == 0);
  
  if (data.connected) {
    // ✅ Inwerter ONLINE – parsuj wszystkie dane
    Serial.printf("✅ [HTTP] Inwerter ONLINE\n");
    
    data.gridVoltage1 = doc["Ua"] | 0.0;
    data.gridVoltage2 = doc["Ub"] | 0.0;
    data.gridVoltage3 = doc["Uc"] | 0.0;
    data.gridCurrent1 = doc["Ia"] | 0.0;
    data.gridCurrent2 = doc["Ib"] | 0.0;
    data.gridCurrent3 = doc["Ic"] | 0.0;
    data.totalPower = doc["Power"] | 0.0;
    
    data.pv1_voltage = doc["Upv1"] | 0.0;
    data.pv1_current = doc["Ipv1"] | 0.0;
    data.pv1_power = doc["Power_Pv1"] | 0.0;
    data.pv2_voltage = doc["Upv2"] | 0.0;
    data.pv2_current = doc["Ipv2"] | 0.0;
    data.pv2_power = doc["Power_Pv2"] | 0.0;
    data.totalPVPower = data.pv1_power + data.pv2_power;
    
    data.dailyEnergy = doc["Today_Production"] | 0.0;
    data.totalEnergy = doc["Total_Production"] | 0;
    data.innerTemp = doc["Themp_Inner"] | 0.0;
    data.moduleTemp = doc["Themp_Module"] | 0.0;
    data.lastUpdateTime = doc["DataTime"] | "";
    
    data.timestamp = millis();
    consecutiveFailures = 0;
    
    // Wyświetl podstawowe dane
    Serial.printf("   📊 Napięcia: Ua=%.1fV, Ub=%.1fV, Uc=%.1fV | Moc=%.0fW\n",
                  data.gridVoltage1, data.gridVoltage2, data.gridVoltage3, data.totalPower);
    return true;
    
  } else {
    // ⚠️ Inwerter OFFLINE – nie parsuj danych, tylko komunikat
    String komunikat = doc["komunikat"] | "Brak komunikatu";
    Serial.printf("⚠️ [HTTP] Inwerter OFFLINE: %s\n", komunikat.c_str());
    
    data.connected = false;
    data.lastUpdateTime = komunikat;
    data.timestamp = millis();
    consecutiveFailures++;
    
    // 🔥 WAŻNE: ustaw wszystkie wartości na 0, żeby nie było śmieci
    data.gridVoltage1 = 0;
    data.gridVoltage2 = 0;
    data.gridVoltage3 = 0;
    data.gridCurrent1 = 0;
    data.gridCurrent2 = 0;
    data.gridCurrent3 = 0;
    data.totalPower = 0;
    data.pv1_voltage = 0;
    data.pv1_current = 0;
    data.pv1_power = 0;
    data.pv2_voltage = 0;
    data.pv2_current = 0;
    data.pv2_power = 0;
    data.totalPVPower = 0;
    data.dailyEnergy = 0;
    data.totalEnergy = 0;
    data.innerTemp = 0;
    data.moduleTemp = 0;
    
    return false;  // ← zwróć false, bo nie ma danych
  }
}

// ========== POBIERANIE DANYCH ==========
bool HttpDataClient::fetchData(HttpData& data) {
  if (activeDataSource != SOURCE_HTTP) {
    lastError = "HTTP not active";
    return false;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    lastError = "WiFi not connected";
    data.connected = false;
    return false;
  }
  
  // 🔥 Zmniejszamy ilość retry do 1 gdy inwerter offline (oszczędność)
  uint8_t maxAttempts = (consecutiveFailures > 5) ? 1 : maxRetries;
  
  Serial.printf("📡 [HTTP] Próba pobrania danych z: %s (próba 1/%d)\n", 
                apiUrl.c_str(), maxAttempts);
  
  for (uint8_t attempt = 0; attempt < maxAttempts; attempt++) {
    if (attempt > 0) {
      Serial.printf("   ⏳ Retry %d/%d (delay: %dms)\n", attempt + 1, maxAttempts, retryDelay);
      delay(retryDelay);
    }
    
    HTTPClient http;
    http.begin(apiUrl);
    http.setTimeout(timeout);
    
    unsigned long startTime = millis();
    int httpCode = http.GET();
    unsigned long elapsed = millis() - startTime;
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      http.end();
      
      Serial.printf("   ✅ HTTP %d (czas: %dms)\n", httpCode, elapsed);
      
      bool success = parseResponse(payload, data);
      if (success) {
        lastError = "";
        return true;
      }
      lastError = "Parse error";
      // Jeśli inwerter offline, nie próbuj ponownie
      if (!data.connected) {
        Serial.printf("   ⏭️ Inwerter offline, pomijam dalsze próby\n");
        return false;
      }
    } else {
      http.end();
      
      // Szczegółowy log błędu HTTP
      switch(httpCode) {
        case -1:
          Serial.printf("   ❌ HTTP %d: Connection refused (serwer nie odpowiada)\n", httpCode);
          lastError = "Connection refused";
          break;
        case -2:
          Serial.printf("   ❌ HTTP %d: Connection timeout (przekroczono czas oczekiwania)\n", httpCode);
          lastError = "Connection timeout";
          break;
        case -3:
          Serial.printf("   ❌ HTTP %d: DNS lookup failed (nieprawidłowy adres URL)\n", httpCode);
          lastError = "DNS lookup failed";
          break;
        case -4:
          Serial.printf("   ❌ HTTP %d: SSL connection failed\n", httpCode);
          lastError = "SSL connection failed";
          break;
        case -5:
          Serial.printf("   ❌ HTTP %d: Send header failed\n", httpCode);
          lastError = "Send header failed";
          break;
        case -6:
          Serial.printf("   ❌ HTTP %d: Send payload failed\n", httpCode);
          lastError = "Send payload failed";
          break;
        case -7:
          Serial.printf("   ❌ HTTP %d: Connection lost\n", httpCode);
          lastError = "Connection lost";
          break;
        case -11:
          Serial.printf("   ❌ HTTP %d: Could not connect to host\n", httpCode);
          lastError = "Could not connect";
          break;
        case 0:
          Serial.printf("   ❌ HTTP 0: No response (serwer nie odpowiada lub timeout)\n");
          lastError = "No response";
          break;
        case 404:
          Serial.printf("   ❌ HTTP %d: Endpoint not found (sprawdź URL)\n", httpCode);
          lastError = "404 Not Found";
          break;
        case 500:
          Serial.printf("   ❌ HTTP %d: Server internal error\n", httpCode);
          lastError = "500 Internal Server Error";
          break;
        default:
          Serial.printf("   ❌ HTTP %d: Błąd połączenia\n", httpCode);
          lastError = "HTTP error: " + String(httpCode);
          break;
      }
    }
  }
  
  data.connected = false;
  Serial.printf("❌ [HTTP] Wszystkie %d prób nieudane. Ostatni błąd: %s\n", maxAttempts, lastError.c_str());
  consecutiveFailures++;
  return false;
}

// ========== ASYNCHRONICZNE POBIERANIE ==========
bool HttpDataClient::fetchDataAsync() {
  if (activeDataSource != SOURCE_HTTP) {
    inverterData.connected = false;
    return false;
  }
  
  HttpData data;
  bool success = fetchData(data);
  
  if (success) {
    inverterData.connected = true;
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
    inverterData.total_pv_power = data.pv1_power + data.pv2_power;  
    inverterData.dailyEnergy = data.dailyEnergy;
    inverterData.totalEnergy = data.totalEnergy;
    inverterData.innerTemp = data.innerTemp;
    inverterData.moduleTemp = data.moduleTemp;
    inverterData.timestamp = millis();
    return true;
  } else {
    inverterData.connected = false;
    return false;
  }
}

// ========== PRZEŁADOWANIE KONFIGURACJI ==========
void HttpDataClient::reloadConfig() {
  // Sprawdź czy HTTP jest aktywnym źródłem
  if (activeDataSource != SOURCE_HTTP) {
    Serial.println("🌐 HTTP: reload pominięty - inne źródło danych");
    return;
  }

  // Przeładuj konfigurację z globalnej struktury
  apiUrl = String(httpDataCfg.addr);
  fetchInterval = httpDataCfg.interval;
  timeout = httpDataCfg.timeout;
  maxRetries = httpDataCfg.maxRetries;
  retryDelay = httpDataCfg.retryDelay;
  
  Serial.printf("🔄 HTTP Data Client przeładowany: %s (interval: %dms)\n", 
                apiUrl.c_str(), fetchInterval);
}

String HttpDataClient::getLastError() {
  return lastError;
}

int HttpDataClient::getConsecutiveFailures() {
  return consecutiveFailures;
}