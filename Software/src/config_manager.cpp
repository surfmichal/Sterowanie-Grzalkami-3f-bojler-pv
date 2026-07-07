#include "config_manager.h"
#include "globals.h"  
#include <ArduinoJson.h>
#include "logger.h"

const char* ConfigManager::mainConfigPath = "/config.json";
const char* ConfigManager::wlanConfigPath = "/wlan.json";

// Deklaracje zewnętrzne (definicje są w globals.cpp)
extern APConfig ap_config;
extern ModbusConfig modbusCfg;

ConfigManager::ConfigManager() {
  memset(&wifiConfig, 0, sizeof(wifiConfig));
}

bool ConfigManager::begin() {
    Serial.println("Inicjalizacja LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("Błąd montowania LittleFS!");
        return false;
    }
    
    bool mainOk = loadMainConfig();  // ← ładuje WiFi, Modbus, Ustawienia, http_data, data_source
    if (!mainOk) {
        Serial.println("Brak config.json – używam domyślnych");
    }
    
    bool apOk = loadWlanConfig();
    if (!apOk) {
        Serial.println("Brak wlan.json – tworzę domyślny");
        saveAPConfig("ESP_Config", "", true);
    }
    
    return true;  // ← zawsze true jeśli LittleFS działa
}

// ========== OBSŁUGA GŁÓWNEJ KONFIGURACJI ==========
bool ConfigManager::loadMainConfig()
{
    DynamicJsonDocument doc(4096);

    if (!loadConfig(doc))
        return false;

    loadWifiConfig(doc);
    loadModbusConfig(doc);
    loadSettingsConfig(doc);
    loadHttpConfig(doc);
    loadDataSourceConfig(doc);
    loadNtpConfig(doc);

    return true;
}

bool ConfigManager::loadConfig(JsonDocument& doc)
{
    File file = LittleFS.open("/config.json", "r");

    if (!file)
    {
        Serial.println("config.json nie istnieje.");
        return false;
    }

    DeserializationError err = deserializeJson(doc, file);

    file.close();

    if (err)
    {
        Serial.printf("Błąd JSON: %s\n", err.c_str());
        return false;
    }

    return true;
}

bool ConfigManager::saveConfig(const JsonDocument& doc)
{
    File file = LittleFS.open("/config.tmp", "w");

    if (!file)
    {
        Serial.println("Nie można utworzyć config.tmp");
        return false;
    }

    if (serializeJsonPretty(doc, file) == 0)
    {
        file.close();
        LittleFS.remove("/config.tmp");

        Serial.println("Błąd zapisu JSON");
        return false;
    }

    file.flush();
    file.close();

    LittleFS.remove("/config.json");

    if (LittleFS.exists("/config.json"))
    {
        if (!LittleFS.remove("/config.json"))
        {
            Serial.println("Nie można usunąć starego config.json");
            LittleFS.remove("/config.tmp");
            return false;
        }
    }
    LittleFS.rename("/config.tmp", "/config.json");
    return true;
}

bool ConfigManager::updateConfig(std::function<void(JsonDocument&)> updater)
{
    DynamicJsonDocument doc(4096);

    if (!loadConfig(doc))
        return false;

    updater(doc);

    return saveConfig(doc);
}

bool ConfigManager::saveWifiConfig()
{
    return updateConfig([this](JsonDocument& doc)
    {
        JsonObject wifi = doc["wifi"];

        wifi["ip"] = wifiCfg.ip;
        wifi["gate"] = wifiCfg.gate;
        wifi["mask"] = wifiCfg.mask;
        wifi["dns"] = wifiCfg.dns;
        wifi["ssid"] = wifiCfg.ssid;
        wifi["pass"] = wifiCfg.pass;
        wifi["dhcp"] = wifiCfg.dhcp;
        wifi["active"] = wifiCfg.active;
    });
}

bool ConfigManager::saveModbusConfig()
{
    return updateConfig([this](JsonDocument& doc)
    {
        JsonObject modbus = doc["modbus"];

        modbus["ip"] = modbusCfg.ip;
        modbus["port"] = modbusCfg.port;
        modbus["unitId"] = modbusCfg.unitId;
        modbus["readInterval"] = modbusCfg.readInterval;
        modbus["timeout"] = modbusCfg.timeout;
        modbus["max_retries"] = modbusCfg.maxRetries;
        modbus["retry_delay"] = modbusCfg.retryDelay;
    });
}

bool ConfigManager::saveHttpDataConfig()
{
    return updateConfig([this](JsonDocument& doc)
    {
        JsonObject http = doc["http_data"];

        http["addr"] = httpDataCfg.addr;
        http["interval"] = httpDataCfg.interval;
        http["timeout"] = httpDataCfg.timeout;
        http["max_retries"] = httpDataCfg.maxRetries;
        http["retry_delay"] = httpDataCfg.retryDelay;
    });
}

void ConfigManager::loadWifiConfig(JsonDocument& doc) {
    JsonObject http = doc["wifi"];
    if (!http.isNull()) {
        wifiCfg.active = wifi["active"] | true;
        wifiCfg.dhcp = wifi["dhcp"] | false;
        wifiCfg.ip  = wifi.["ip"] | "192.168.20.34";
        wifiCfg.mask = wifi["mask"] | "255.255.255.0";
        wifiCfg.gate = wifi["gate"] | "192.168.20.1";
        wifiCfg.pass = wifi["pass"] | "",
        wifiCfg.ssid = wifi["ssid"] | ""
    }
}

void ConfigManager::loadHttpConfig(JsonDocument& doc) {
    JsonObject http = doc["http_data"];
    if (!http.isNull()) {
        httpDataCfg.addr = http["addr"] | "";
        httpDataCfg.interval = http["interval"] | 10000;
        httpDataCfg.timeout = http["timeout"] | 2000;
        httpDataCfg.maxRetries = http["max_retries"] | 3;
        httpDataCfg.retryDelay = http["retry_delay"] | 1000;
    }
}

void ConfigManager::loadDataSourceConfig(JsonDocument& doc) {
    JsonObject ds = doc["data_source"];
    if (!ds.isNull()) {
        const char* src = ds["source"] | "modbus";
        activeDataSource = strcmp(src, "http") == 0 ? SOURCE_HTTP : SOURCE_MODBUS;
    }
}

void ConfigManager::loadNtpConfig(JsonDocument& doc) {
    JsonObject ntp = doc["ntp"];
    if (!ntp.isNull()) {
        ntpCfg.server = ntp["server"] | "pool.ntp.org";
        ntpCfg.gmt_offset = ntp["gmt_offset"] | 3600;
        ntpCfg.daylight_offset = ntp["daylight_offset"] | 3600;
    }
}

// ========== OBSŁUGA KONFIGURACJI AP ==========
bool ConfigManager::loadWlanConfig() {
  File file = LittleFS.open(wlanConfigPath, "r");
  if (!file) return false;
  
  //String content = file.readString();
  
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.print("Błąd parsowania wlan.json: ");
    Serial.println(error.c_str());
    return false;
  }
  
  JsonObject ap = doc["ap"];
  
  // Kopiuj do globalnej struktury ap_config
  strlcpy(ap_config.ssid, ap["ssid"] | "ESP_Config", sizeof(ap_config.ssid));
  strlcpy(ap_config.password, ap["password"] | "", sizeof(ap_config.password));
  ap_config.active = ap["active"] | true;
  
  Serial.println("📋 Odczytano konfigurację AP:");
  Serial.printf("   SSID: %s\n", ap_config.ssid);
  Serial.printf("   Hasło: %s\n", ap_config.password[0] ? "***" : "(brak)");
  
  return true;
}

bool ConfigManager::saveAPConfig(const char* ssid, const char* password, bool active) {
  Serial.println("🔴🔴🔴 UWAGA! saveAPConfig() ZAPISUJE wlan.json!");
  DynamicJsonDocument doc(4096);
  
  if (ssid != nullptr) {
    strlcpy(ap_config.ssid, ssid, sizeof(ap_config.ssid));
  }
  if (password != nullptr) {
    strlcpy(ap_config.password, password, sizeof(ap_config.password));
  }
  ap_config.active = active;
  
  doc["ap"]["ssid"] = ap_config.ssid;
  doc["ap"]["password"] = ap_config.password;
  doc["ap"]["active"] = ap_config.active;
  
  File file = LittleFS.open(wlanConfigPath, "w");
  if (!file) {
    Serial.println("Błąd otwarcia wlan.json do zapisu");
    return false;
  }
  
  //String output;
  size_t written = serializeJson(doc,file);
  
  Serial.println("✅ Zapisano konfigurację AP");
  file.close();
  return written > 0;
}

const char* ConfigManager::getAP_SSID() {
  return ap_config.ssid;
}

const char* ConfigManager::getAP_Password() {
  return ap_config.password;
}

bool ConfigManager::isAPActive() {
  return ap_config.active;
}

WifiConfig& ConfigManager::getWifiConfig() {
    return wifiConfig;
}

ModbusConfig& ConfigManager::getModbusConfig() {
    return modbusCfg;
}

Ustawienia& ConfigManager::getUstawienia() {
    return U;
}

HttpDataConfig& ConfigManager::getHttpDataConfig() {
    return httpDataCfg;
}

void ConfigManager::printConfig() {
  Serial.println("=== GŁÓWNA KONFIGURACJA ===");
  Serial.print("SSID: "); Serial.println(wifiConfig.ssid);
  Serial.print("IP: "); Serial.println(wifiConfig.ip);
  Serial.print("DHCP: "); Serial.println(wifiConfig.dhcp ? "true" : "false");
  
  Serial.println("=== KONFIGURACJA MODBUS ===");
  Serial.print("Enabled: "); Serial.println(modbusCfg.enabled ? "true" : "false");
  Serial.print("IP: "); Serial.println(modbusCfg.ip);
  Serial.print("Port: "); Serial.println(modbusCfg.port);
  Serial.print("Unit ID: "); Serial.println(modbusCfg.unitId);
  
  Serial.println("=== KONFIGURACJA AP ===");
  Serial.print("AP SSID: "); Serial.println(ap_config.ssid);
  Serial.print("AP Aktywny: "); Serial.println(ap_config.active ? "Tak" : "Nie");
}


bool ConfigManager::saveHeaterConfig() {
  return saveMainConfig();  // Najprościej - zapisz cały plik config.json
}

// ========== ZAPIS STRUKTURY USTAWIENIA ==========
bool ConfigManager::saveUstawienia() {
  Serial.println("saveUstawienia: Zapisuję ustawienia...");
  LOG_INFO("USTAWIENIA","Zapisuję ustawienia do config.json");
  
  return updateConfig([this](JsonDocument& doc)
  {   
      JsonObject s = doc["Ustawienia"];

      s["HeaterEnabled"] = U.HeaterEnabled;
      s["Ugrid_on"] = U.Ugrid_on;
      s["Ugrid_off"] = U.Ugrid_off;
      s["HeaterDelay_on_ms"] = U.HeaterDelay_on_ms;
      s["HeaterDelay_off_ms"] = U.HeaterDelay_off_ms;
      s["ContactorDelay_off_ms"] = U.ContactorDelay_off_ms;
      s["bojlerTmax"] = U.bojlerTmax;
      s["radiatorTmax"] = U.radiatorTmax;
      s["radiatorT_critical"] = U.radiatorT_critical;
      s["serwer_www_port"] = 80;
  });  
}

// ========== ODCZYT STRUKTURY USTAWIENIA ==========
void ConfigManager::loadSettingsConfig(JsonDocument& doc)
{
    JsonObject s = doc["Ustawienia"];
    if (!s.isNull()) {
        U.HeaterEnabled = s["HeaterEnabled"] | true;
        U.Ugrid_on = s["Ugrid_on"] | 252.0;
        U.Ugrid_off = s["Ugrid_off"] | 250.0;
        U.HeaterDelay_on_ms = s["HeaterDelay_on_ms"] | 1000;
        U.HeaterDelay_off_ms = s["HeaterDelay_off_ms"] | 5000;
        U.ContactorDelay_off_ms = s["ContactorDelay_off_ms"] | 5000;
        U.bojlerTmax = s["bojlerTmax"] | 80.0;
        U.radiatorTmax = s["radiatorTmax"] | 70.0;
        U.radiatorT_critical = s["radiatorT_critical"] | false;
    }
}

// ========== HTTP DATA CONFIG ==========
HttpDataConfig* ConfigManager::getHttpDataConfig() {
  return &http_data_cfg;
}

bool ConfigManager::saveDataSource(const char* source)
{
    activeDataSource =
        strcmp(source, "modbus") == 0 ?
        SOURCE_MODBUS :
        SOURCE_HTTP;

    return updateConfig([&](JsonDocument& doc)
    {
        doc["data_source"]["source"] = source;
    });
}