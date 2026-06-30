#include "web_server.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "temperature_fifo.h"
#include "ntp_manager.h"
#include "logger.h"
#include "statistics.h"
#include "data_manager.h"
#include "http_data_client.h"
#include "globals.h"  

// Deklaracje zewnętrzne
extern InverterData inverterData;
extern ModbusConfig modbusCfg;
extern Ustawienia U;           // ← GŁÓWNA STRUKTURA KONFIGURACJI
extern Zmienne Z;
extern NTPManager ntp;
extern Logger logger;
extern LicznikiCzasu liczniki;
extern TemperatureFIFO tempFIFO;
extern Temperatury T;              // 
extern StycznikState stycznik;     // 

// Zmienne symulacji
extern bool simulationMode;               // 
extern bool simulationModbusConnected;    // 
extern float simVoltage1;                 // 
extern float simVoltage2;                 // 
extern float simVoltage3;                 // 

// ========== KONSTRUKTOR ==========
WebServerManager::WebServerManager(ConfigManager* cfg, WiFiManager* wifiMgr) 
  : server(80), config(cfg), wifi(wifiMgr) { }

// ========== URUCHOMIENIE SERWERA ==========
void WebServerManager::begin() {
  // Strony HTML
  server.on("/", std::bind(&WebServerManager::handleRoot, this));
  server.on("/settings", std::bind(&WebServerManager::handleSettings, this));
  
  // API
  server.on("/api/status", HTTP_GET, std::bind(&WebServerManager::handleApiStatus, this));
  server.on("/api/config", HTTP_GET, std::bind(&WebServerManager::handleApiConfig, this));
  server.on("/api/data", HTTP_GET, std::bind(&WebServerManager::handleApiData, this));
  server.on("/api/heater_config", HTTP_GET, std::bind(&WebServerManager::handleApiHeaterConfig, this));
  server.on("/api/save_wifi", HTTP_POST, std::bind(&WebServerManager::handleApiSaveWiFi, this));
  server.on("/api/save_modbus", HTTP_POST, std::bind(&WebServerManager::handleApiSaveModbus, this));
  server.on("/api/save_heater", HTTP_POST, std::bind(&WebServerManager::handleApiSaveHeater, this));
  server.on("/api/restart", HTTP_POST, std::bind(&WebServerManager::handleApiRestart, this));
  server.on("/api/reset_wifi", HTTP_POST, std::bind(&WebServerManager::handleApiResetWiFi, this));
  server.on("/logs", HTTP_GET, std::bind(&WebServerManager::handleLogsPage, this));
  server.on("/api/logs", HTTP_GET, std::bind(&WebServerManager::handleApiLogs, this));
  server.on("/api/logs/download", HTTP_GET, std::bind(&WebServerManager::handleApiLogsDownload, this));
  server.on("/api/logs/clear", HTTP_POST, std::bind(&WebServerManager::handleApiLogsClear, this));
  server.on("/api/time", HTTP_GET, std::bind(&WebServerManager::handleApiTime, this));
  server.on("/api/temperatures", HTTP_GET, std::bind(&WebServerManager::handleApiTemperatures, this));
  server.on("/api/statistics", HTTP_GET, std::bind(&WebServerManager::handleApiStatistics, this));  
  server.on("/api/temperature/history", HTTP_GET, std::bind(&WebServerManager::handleApiTemperatureHistory, this));
  server.on("/api/temperature/last", HTTP_GET, std::bind(&WebServerManager::handleApiTemperatureLast, this));
  server.on("/api/temperature/summary", HTTP_GET, std::bind(&WebServerManager::handleApiTemperatureSummary, this));
  server.on("/api/logs_config", HTTP_GET, std::bind(&WebServerManager::handleApiLogsConfig, this));
  server.on("/api/save_logs_config", HTTP_POST, std::bind(&WebServerManager::handleApiSaveLogsConfig, this));
  server.on("/api/heater/enable", HTTP_POST, std::bind(&WebServerManager::handleApiHeaterEnable, this));
  server.on("/api/heater/status", HTTP_GET, std::bind(&WebServerManager::handleApiHeaterStatus, this));
  server.on("/api/version", HTTP_GET, std::bind(&WebServerManager::handleApiVersion, this));

  server.on("/api/simulation", HTTP_GET, std::bind(&WebServerManager::handleApiSimulationGet, this));
  server.on("/api/simulation", HTTP_POST, std::bind(&WebServerManager::handleApiSimulationPost, this));
 
  server.onNotFound(std::bind(&WebServerManager::handleNotFound, this));  
  
  server.begin();
  Serial.println("Serwer HTTP uruchomiony na porcie 80");
  Serial.println("Strona główna: http://" + wifi->getLocalIP());
  Serial.println("Strona ustawień: http://" + wifi->getLocalIP() + "/settings");
}

void WebServerManager::handleApiLogsDownload() {
  String allLogs = logger.getAllLogs();
  server.send(200, "text/plain", allLogs);
}

// ========== API: POBIERZ KONFIGURACJĘ LOGÓW ==========
void WebServerManager::handleApiLogsConfig() {
  DynamicJsonDocument doc(256);
  doc["storage_mode"] = (logger.getStorageMode() == STORAGE_RAM_ONLY) ? "ram" : "flash";
  doc["log_level"] = logger.getLogLevel();
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ========== API: ZAPISZ KONFIGURACJĘ LOGÓW ==========
void WebServerManager::handleApiSaveLogsConfig() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"success\":false,\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  
  String storageMode = doc["storage_mode"] | "ram";
  int logLevel = doc["log_level"] | 1;
  
  // Ustaw tryb przechowywania
  if (storageMode == "ram") {
    logger.setStorageMode(STORAGE_RAM_ONLY);
  } else {
    logger.setStorageMode(STORAGE_FLASH);
  }
  
  // Ustaw poziom logowania
  logger.setLogLevel((LogLevel)logLevel);
  
  server.send(200, "application/json", "{\"success\":true}");
}

void WebServerManager::handleApiLogs() {
  String logs = logger.getRecentLogs(500);
  
  DynamicJsonDocument doc(8192);
  JsonArray logsArray = doc.createNestedArray("logs");
  
  // Podziel logi na linie
  int start = 0;
  int end = logs.indexOf('\n');
  while (end > 0) {
    logsArray.add(logs.substring(start, end));
    start = end + 1;
    end = logs.indexOf('\n', start);
  }
  // Dodaj ostatnią linię
  if (start < logs.length()) {
    logsArray.add(logs.substring(start));
  }
  
  doc["size"] = logs.length();
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void WebServerManager::handleApiLogsClear() {
  logger.clearLogs();
  server.send(200, "application/json", "{\"success\":true}");
}

// ========== OBSŁUGA WEWNĄTRZ PĘTLI ==========
void WebServerManager::handle() {
  server.handleClient();
}

// ========== API: WSZYSTKIE TEMPERATURY ==========
void WebServerManager::handleApiTemperatures() {
  DynamicJsonDocument doc(256);
  
  // === BOJLER ===
  if (T.bojler.ok) {
    doc["bojler"]["temp"] = T.bojler.temperatura;
    doc["bojler"]["ok"] = true;
  } else {
    doc["bojler"]["ok"] = false;
    doc["bojler"]["temp"] = nullptr;  // ← null w JSON
  }
  
  // === RADIATOR ===
  if (T.radiator.ok) {
    doc["radiator"]["temp"] = T.radiator.temperatura;
    doc["radiator"]["ok"] = true;
  } else {
    doc["radiator"]["ok"] = false;
    doc["radiator"]["temp"] = nullptr;  // ← null w JSON
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ========== API: KONFIGURACJA GRZAŁEK (GET) ==========
void WebServerManager::handleApiHeaterConfig() {
  DynamicJsonDocument doc(256);
  doc["u_on"] = U.Ugrid_on;
  doc["u_off"] = U.Ugrid_off;
  doc["delay_on_ms"] = U.HeaterDelay_on_ms;
  doc["delay_off_ms"] = U.HeaterDelay_off_ms;
  doc["ContactorDelay_off_ms"] = U.ContactorDelay_off_ms;
  doc["Heater_enabled"] = true;  // Możesz dodać pole do U jeśli potrzebujesz włącz/wyłącz
  doc["bojler_Tmax"] = U.bojlerTmax;
  doc["radiatorT_critical"] = U.radiatorT_critical;
  doc["radiator_Tmax"] = U.radiatorTmax;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ========== API: ZAPIS KONFIGURACJI GRZAŁEK ==========
void WebServerManager::handleApiSaveHeater() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"success\":false,\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  
  // Aktualizacja struktury U
  U.HeaterEnabled = doc["enabled"] | true;  // Domyślnie włączone
  U.Ugrid_on = doc["u_on"] | 253.0;
  U.Ugrid_off = doc["u_off"] | 252.0;
  U.HeaterDelay_on_ms = doc["delay_on_ms"] | 1000;
  U.HeaterDelay_off_ms = doc["delay_off_ms"] | 5000;  
  U.ContactorDelay_off_ms = doc["ContactorDelay_off_ms"] | 5000;
  U.bojlerTmax = doc["t_max"] | 75;
  U.radiatorT_critical = doc["radiatorT_critical"] | false;
  U.radiatorTmax = doc["radiatorTmax"] | 60;
  
  
  // Zapisz do pliku konfiguracyjnego
  config->saveUstawienia();
  
  // Opcjonalnie: poinformuj system grzałek o nowych ustawieniach
  // (jeśli heaterControl używa U bezpośrednio, to nie potrzebuje synchronizacji)
  
  server.send(200, "application/json", "{\"success\":true}");
}

// ========== API: DANE Z FALOWNIKA ==========
void WebServerManager::handleApiData() {
  DynamicJsonDocument doc(2048);
  
  // Dane z falownika (Modbus lub HTTP)
  doc["inverterData"]["mbConnected"] = inverterData.mbConnected;
  doc["inverterData"]["httpConnected"] = inverterData.httpConnected;
  
  if (inverterData.mbConnected || inverterData.httpConnected) {
    doc["inverterData"]["grid"]["v1"] = inverterData.gridVoltage1;
    doc["inverterData"]["grid"]["v2"] = inverterData.gridVoltage2;
    doc["inverterData"]["grid"]["v3"] = inverterData.gridVoltage3;
    doc["inverterData"]["grid"]["a1"] = inverterData.gridCurrent1;
    doc["inverterData"]["grid"]["a2"] = inverterData.gridCurrent2;
    doc["inverterData"]["grid"]["a3"] = inverterData.gridCurrent3;
    doc["inverterData"]["total_power"] = inverterData.power;
    doc["inverterData"]["pv"]["p1_voltage"] = inverterData.pv1_voltage;
    doc["inverterData"]["pv"]["p1_current"] = inverterData.pv1_current;
    doc["inverterData"]["pv"]["p1_power"] = inverterData.pv1_power;
    doc["inverterData"]["pv"]["p2_voltage"] = inverterData.pv2_voltage;
    doc["inverterData"]["pv"]["p2_current"] = inverterData.pv2_current;
    doc["inverterData"]["pv"]["p2_power"] = inverterData.pv2_power;
    doc["inverterData"]["pv"]["total_power"] = inverterData.total_pv_power;
    doc["inverterData"]["temp_inner"] = inverterData.innerTemp;
    doc["inverterData"]["temp_module"] = inverterData.moduleTemp;
    doc["inverterData"]["daily_energy"] = inverterData.dailyEnergy;
    doc["inverterData"]["total_energy"] = inverterData.totalEnergy;
  }
  
  // Konfiguracja Modbus
  JsonObject mbCfg = doc.createNestedObject("modbus_config");
  mbCfg["ip"] = modbusCfg.ip;
  mbCfg["port"] = modbusCfg.port;
  mbCfg["unitId"] = modbusCfg.unitId;
  mbCfg["readInterval"] = modbusCfg.readInterval;
  mbCfg["enabled"] = modbusCfg.enabled;
  
  // Konfiguracja grzałek (ze struktury U)
  JsonObject heatCfg = doc.createNestedObject("heater_config");
  heatCfg["u_on"] = U.Ugrid_on;
  heatCfg["u_off"] = U.Ugrid_off;
  heatCfg["delay_off_ms"] = U.HeaterDelay_off_ms;
  //heatCfg["enabled"] = heater_config.enabled;
  heatCfg["t_max"] = U.bojlerTmax;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ========== API: STATUS ==========
void WebServerManager::handleApiStatus() {
  DynamicJsonDocument doc(512);
  
  doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  doc["ip"] = wifi->getLocalIP();
  
  unsigned long uptime = millis() / 1000;
  doc["uptime"] = String(uptime / 86400) + "d " + 
                  String((uptime % 86400) / 3600) + "h " + 
                  String((uptime % 3600) / 60) + "m " + 
                  String(uptime % 60) + "s";
  
  doc["free_heap"] = ESP.getFreeHeap();
  doc["grzalka1"] = Z.heater1_flag;
  doc["grzalka2"] = Z.heater2_flag;
  doc["grzalka3"] = Z.heater3_flag;

  doc["contactorState"] = stycznik.state;
  
  doc["temp_bojler"] = T.bojler.temperatura;
  doc["temp_max"] = U.bojlerTmax;
  
  // ========== DODANE: WERSJA OPROGRAMOWANIA ==========
  doc["firmware_name"] = FIRMWARE_NAME;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["compile_date"] = COMPILE_DATE;
  doc["compile_time"] = COMPILE_TIME;
  doc["compile_datetime"] = COMPILE_DATETIME;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ========== API: KONFIGURACJA (GET) ==========
void WebServerManager::handleApiConfig() {
  WifiConfig* wifiCfg = config->getWifiConfig();
  
  DynamicJsonDocument doc(512);
  doc["ssid"] = wifiCfg->ssid;
  doc["ip"] = wifiCfg->ip;
  doc["gateway"] = wifiCfg->gate;
  doc["mask"] = wifiCfg->mask;
  doc["dns"] = wifiCfg->dns;
  doc["dhcp"] = wifiCfg->dhcp;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ========== API: ZAPIS KONFIGURACJI WiFi ==========
void WebServerManager::handleApiSaveWiFi() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"success\":false,\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  
  String ssid = doc["ssid"] | "";
  String pass = doc["pass"] | "";
  bool dhcp = doc["dhcp"] | true;
  String ip = doc["ip"] | "";
  String gate = doc["gateway"] | "";
  String mask = doc["mask"] | "";
  String dns = doc["dns"] | "";
  
  if (ssid.length() > 0) {
    config->saveWifiConfig(ssid.c_str(), pass.c_str(), 
                           ip.c_str(), gate.c_str(), mask.c_str(), dns.c_str(),
                           dhcp, true);
    server.send(200, "application/json", "{\"success\":true}");
    
    delay(500);
    ESP.restart();
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"SSID cannot be empty\"}");
  }
}

// ========== API: ZAPIS KONFIGURACJI MODBUS ==========
void WebServerManager::handleApiSaveModbus() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"success\":false,\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  
  strlcpy(modbusCfg.ip, doc["ip"] | "192.168.1.200", sizeof(modbusCfg.ip));
  modbusCfg.port = doc["port"] | 502;
  modbusCfg.unitId = doc["unitId"] | 1;
  modbusCfg.readInterval = doc["readInterval"] | 5000;
  modbusCfg.enabled = doc["enabled"] | true;
  
  config->saveModbusConfig();
  
  server.send(200, "application/json", "{\"success\":true}");
}

// ========== API: RESTART ==========
void WebServerManager::handleApiRestart() {
  server.send(200, "application/json", "{\"success\":true}");
  delay(500);
  ESP.restart();
}

// ========== API: RESET WIFI ==========
void WebServerManager::handleApiResetWiFi() {
  config->saveWifiConfig("", "", "", "", "", "", true, true);
  server.send(200, "application/json", "{\"success\":true}");
  delay(500);
  ESP.restart();
}

// ========== API: STATYSTYKI ==========
void WebServerManager::handleApiStatistics() {
  DynamicJsonDocument doc(512);
  
  // Czas pracy
  doc["dzis_grzalka1"] = liczniki.dzis_grzalka1;
  doc["dzis_grzalka2"] = liczniki.dzis_grzalka2;
  doc["dzis_grzalka3"] = liczniki.dzis_grzalka3;
  doc["total_grzalka1"] = liczniki.total_grzalka1;
  doc["total_grzalka2"] = liczniki.total_grzalka2;
  doc["total_grzalka3"] = liczniki.total_grzalka3;
  
  // Liczba załączeń
  doc["zalaczenia_dzis_grzalka1"] = liczniki.zalaczenia_dzis_grzalka1;
  doc["zalaczenia_dzis_grzalka2"] = liczniki.zalaczenia_dzis_grzalka2;
  doc["zalaczenia_dzis_grzalka3"] = liczniki.zalaczenia_dzis_grzalka3;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ========== API: STATYSTYKI ==========
void WebServerManager::handleApiTemperatureHistory() {
  String json = getTemperatureHistoryJSON();
  server.send(200, "application/json", json);
}

// ========== API: TIME ==========
void WebServerManager::handleApiTime() {
  DynamicJsonDocument doc(256);
  
  // Używamy metod klasy NTPManager
  doc["synced"] = ntp.isSynced();
  doc["date"] = ntp.getDateString();
  doc["time"] = ntp.getTimeString();
  doc["datetime"] = ntp.getDateTimeString();
  doc["timestamp"] = ntp.getTimestamp();
  doc["day_of_year"] = ntp.getDayOfYear();
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);  
}

// ========== STRONA WERSJI FIRMWARE ==========
void WebServerManager::handleApiVersion() {
  DynamicJsonDocument doc(256);
  doc["name"] = FIRMWARE_NAME;
  doc["version"] = FIRMWARE_VERSION;
  doc["compile_date"] = COMPILE_DATE;
  doc["compile_time"] = COMPILE_TIME;
  doc["compile_datetime"] = COMPILE_DATETIME;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}


// ========== STRONA USTAWIEŃ ==========
void WebServerManager::handleSettings() {
  File file = LittleFS.open("/settings.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "404: Not Found - settings.html");
  }
}


// ========== STRONA GŁÓWNA ==========
void WebServerManager::handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "404: Not Found - index.html");
  }
}

// ========== OBSŁUGA LOGGERA ==========
void WebServerManager::handleLogsPage() {
  File file = LittleFS.open("/logger.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "404: Not Found - logger.html");
  }
}

// ========== OBSŁUGA BŁĘDU 404 ==========
void WebServerManager::handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

// ========== API: OSTATNIE N POMIARÓW TEMPERATURY ==========
void WebServerManager::handleApiTemperatureLast() {
  int n = server.arg("n").toInt();
  if (n <= 0 || n > 720) n = 24;  // domyślnie 24 pomiary (48 minut)
  
  String json = getLastNTemperaturesJSON(n);
  server.send(200, "application/json", json);
}

// ========== API: PODSUMOWANIE TEMPERATURY ==========
void WebServerManager::handleApiTemperatureSummary() {
  String json = getTemperatureSummaryJSON();
  server.send(200, "application/json", json);
}

// ========== API: WŁĄCZ/WYŁĄCZ SYSTEM GRZANIA ==========
void WebServerManager::handleApiHeaterEnable() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(64);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  bool enabled = doc["enabled"] | false;
  U.HeaterEnabled = enabled;
  
  // Zapisz do config.json (opcjonalnie)
  config->saveUstawienia();
  
  Serial.printf("🔧 System grzania: %s\n", enabled ? "WŁĄCZONY" : "WYŁĄCZONY");
  LOG_INFO("WebServer", "System grzania: %s", enabled ? "WŁĄCZONY" : "WYŁĄCZONY");
  
  server.send(200, "application/json", "{\"success\":true}");
}

// ========== API: STATUS SYSTEMU GRZANIA ==========
void WebServerManager::handleApiHeaterStatus() {
  DynamicJsonDocument doc(64);
  doc["enabled"] = U.HeaterEnabled;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}


// ========== API: SYMULACJA FALOWNIKA (GET) ==========
void WebServerManager::handleApiSimulationGet() {
  DynamicJsonDocument doc(256);
  doc["enabled"] = simulationMode;
  doc["modbus_connected"] = simulationModbusConnected;  // ← NOWE
  doc["voltage1"] = simVoltage1;
  doc["voltage2"] = simVoltage2;
  doc["voltage3"] = simVoltage3;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ========== API: SYMULACJA FALOWNIKA (POST) ==========
void WebServerManager::handleApiSimulationPost() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  simulationMode = doc["enabled"] | simulationMode;
  simulationModbusConnected = doc["modbus_connected"] | simulationModbusConnected;  // ← NOWE
  simVoltage1 = doc["voltage1"] | simVoltage1;
  simVoltage2 = doc["voltage2"] | simVoltage2;
  simVoltage3 = doc["voltage3"] | simVoltage3;
  
  Serial.printf("🔧 Symulacja: %s, Modbus: %s, napięcia: %.1f, %.1f, %.1f\n",
                simulationMode ? "WŁĄCZONA" : "WYŁĄCZONA",
                simulationModbusConnected ? "Online" : "Offline",
                simVoltage1, simVoltage2, simVoltage3);
  
  server.send(200, "application/json", "{\"success\":true}");
}