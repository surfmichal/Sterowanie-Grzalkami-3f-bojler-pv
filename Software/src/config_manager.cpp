#include "config_manager.h"
#include "globals.h"  
#include <ArduinoJson.h>
#include "logger.h"

const char* ConfigManager::mainConfigPath = "/config.json";

// Deklaracje zewnętrzne (definicje są w globals.cpp)
extern WifiConfig wifiCfg;
extern APConfig apCfg;
extern ModbusConfig modbusCfg;
extern HttpDataConfig httpDataCfg;
extern NtpConfig ntpCfg;
extern DataSource activeDataSource;

ConfigManager::ConfigManager() {}

// ========== INICJALIZACJA ==========
bool ConfigManager::begin() {
    Serial.println("Inicjalizacja LittleFS...");
    
    // ===== KROK 1: Montowanie =====
    if (!LittleFS.begin(true)) {
        Serial.println("❌ Błąd montowania LittleFS!");
        return false;
    }
    Serial.println("✅ LittleFS zamontowany");
    
    // ===== KROK 2: Test zapisu =====
    bool writeOk = false;
    File test = LittleFS.open("/test.txt", "w");
    if (test) {
        test.println("test");
        test.close();
        LittleFS.remove("/test.txt");
        writeOk = true;
        Serial.println("✅ LittleFS działa w trybie zapisu");
    } else {
        Serial.println("⚠️ LittleFS jest w trybie read-only!");
        LittleFS.end();
        if (!LittleFS.begin(true)) {
            Serial.println("❌ Nie udało się naprawić LittleFS!");
            return false;
        }
        Serial.println("✅ LittleFS naprawiony (sformatowany)");
    }
    
    // ===== KROK 3: Wczytaj config =====
    bool mainOk = loadMainConfig();
    if (!mainOk) {
        Serial.println("⚠️ Brak config.json lub błąd odczytu");
        
        // Sprawdź czy plik istnieje
        if (!LittleFS.exists("/config.json")) {
            Serial.println("   config.json nie istnieje - tworzę domyślny");
            if (!createDefaultConfig()) {
                Serial.println("❌ Błąd tworzenia domyślnego config.json!");
                return false;
            }
        } else {
            Serial.println("   config.json istnieje ale nie można go odczytać");
            // Spróbuj ponownie odczytać
            if (!loadMainConfig()) {
                Serial.println("❌ Błąd odczytu config.json");
                return false;
            }
        }
    }
    
    Serial.println("✅ Konfiguracja wczytana pomyślnie");
    return true;
}

// ========== TWORZENIE DOMYŚLNEGO PLIKU KONFIGURACJI ==========
bool ConfigManager::createDefaultConfig() {
    DynamicJsonDocument doc(1024);
    doc["wifi"]["ssid"] = "";
    doc["wifi"]["pass"] = "";
    doc["data_source"]["source"] = "http";
    doc["http_data"]["addr"] = "http://192.168.0.251:8080/api/data";
    
    File file = LittleFS.open("/config.json", "w");
    if (!file) {
        Serial.println("❌ Nie można utworzyć config.json");
        return false;
    }
    
    if (serializeJsonPretty(doc, file) == 0) {
        file.close();
        Serial.println("❌ Błąd zapisu JSON");
        return false;
    }
    file.close();
    Serial.println("✅ config.json utworzony");
    return true;
}

// ========== OBSŁUGA GŁÓWNEJ KONFIGURACJI ==========
bool ConfigManager::loadMainConfig() {
    DynamicJsonDocument doc(4096);

    if (!loadConfig(doc)) {
        Serial.println("❌ loadConfig() zwrócił false");
        return false;
    }

    loadWifiConfig(doc);
    loadAPConfig(doc);
    loadModbusConfig(doc);
    loadSettingsConfig(doc);
    loadHttpConfig(doc);
    loadDataSourceConfig(doc);
    loadNtpConfig(doc);

    Serial.println("✅ loadMainConfig() zakończony");
    return true;
}

// ========== ODCZYT PLIKU KONFIGURACJI ==========
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

// ========== ZAPIS PLIKU KONFIGURACJI ==========
bool ConfigManager::saveConfig(const JsonDocument& doc)
{
    // Zapisz do pliku tymczasowego
    File file = LittleFS.open("/config.tmp", "w");
    if (!file) {
        Serial.println("❌ Nie można utworzyć config.tmp");
        return false;
    }

    size_t written = serializeJsonPretty(doc, file);
    file.flush();
    file.close();
    
    if (written == 0) {
        Serial.println("❌ Błąd zapisu JSON");
        LittleFS.remove("/config.tmp");
        return false;
    }

    // Usuń stary plik i przemianuj nowy
    LittleFS.remove("/config.json");
    
    if (!LittleFS.rename("/config.tmp", "/config.json")) {
        Serial.println("❌ Błąd rename config.tmp → config.json");
        return false;
    }
    
    Serial.println("✅ config.json zapisany");
    return true;
}

// ========== AKTUALIZACJA KONFIGURACJI ==========
bool ConfigManager::updateConfig(std::function<void(JsonDocument&)> updater)
{
    DynamicJsonDocument doc(4096);

    if (!loadConfig(doc))
        return false;

    updater(doc);

    return saveConfig(doc);
}

// ========== ODCZYT KONFIGURACJI WIFI ==========
void ConfigManager::loadWifiConfig(JsonDocument& doc) {
    JsonObject wifi = doc["wifi"];
    if (!wifi.isNull()) {
        wifiCfg.active = wifi["active"] | true;
        wifiCfg.dhcp = wifi["dhcp"] | false;
        strlcpy(wifiCfg.ip, wifi["ip"] | "192.168.20.34", sizeof(wifiCfg.ip));
        strlcpy(wifiCfg.mask, wifi["mask"] | "255.255.255.0", sizeof(wifiCfg.mask));
        strlcpy(wifiCfg.gate, wifi["gate"] | "192.168.20.1", sizeof(wifiCfg.gate));
        strlcpy(wifiCfg.dns, wifi["dns"] | "8.8.8.8", sizeof(wifiCfg.dns));
        strlcpy(wifiCfg.ssid, wifi["ssid"] | "", sizeof(wifiCfg.ssid));
        strlcpy(wifiCfg.pass, wifi["pass"] | "", sizeof(wifiCfg.pass));
    }
}

// ========== ZAPIS KONFIGURACJI WIFI ==========
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

bool ConfigManager::saveWifiConfig(const char* ssid, const char* pass, 
                                   const char* ip, const char* gate, 
                                   const char* mask, const char* dns,
                                   bool dhcp, bool active) {
    // Kopiuj do globalnej wifiCfg
    strlcpy(wifiCfg.ssid, ssid, sizeof(wifiCfg.ssid));
    strlcpy(wifiCfg.pass, pass, sizeof(wifiCfg.pass));
    strlcpy(wifiCfg.ip, ip, sizeof(wifiCfg.ip));
    strlcpy(wifiCfg.gate, gate, sizeof(wifiCfg.gate));
    strlcpy(wifiCfg.mask, mask, sizeof(wifiCfg.mask));
    strlcpy(wifiCfg.dns, dns, sizeof(wifiCfg.dns));
    wifiCfg.dhcp = dhcp;
    wifiCfg.active = active;
    
    // Zapisz do pliku
    return saveWifiConfig();
}

// ========== ODCZYT KONFIGURACJI AP ==========
void ConfigManager::loadAPConfig(JsonDocument& doc) {
    JsonObject ap = doc["ap"];   // ← POPRAWNIE: "ap"
    if (!ap.isNull()) {
        strlcpy(apCfg.ip, ap["ip"] | "192.168.4.1", sizeof(apCfg.ip));
        strlcpy(apCfg.password, ap["password"] | "", sizeof(apCfg.password));
        strlcpy(apCfg.ssid, ap["ssid"] | "ESP32_Config", sizeof(apCfg.ssid));
        apCfg.active = ap["active"] | true;
    }
}

// ========== ZAPIS KONFIGURACJI AP ==========
bool ConfigManager::saveAPConfig()
{
    return updateConfig([this](JsonDocument& doc)
    {
        JsonObject ap = doc["ap"];

        ap["ip"] = apCfg.ip;
        ap["password"] = apCfg.password;
        ap["ssid"] = apCfg.ssid;
        ap["active"] = apCfg.active;
    });
}

bool ConfigManager::saveAPConfig(const char* ssid, const char* pass,
                                 const char* ip, bool active) {
    // Kopiuj do globalnej apCfg
    strlcpy(apCfg.ssid, ssid, sizeof(apCfg.ssid));
    strlcpy(apCfg.password, pass, sizeof(apCfg.password));
    strlcpy(apCfg.ip, ip, sizeof(apCfg.ip));
    apCfg.active = active;
    
    // Zapisz do pliku
    return saveAPConfig();
}

// ========== ODCZYT KONFIGURACJI ŹRÓDŁA DANYCH ==========
void ConfigManager::loadDataSourceConfig(JsonDocument& doc) {
    JsonObject ds = doc["data_source"];
    if (!ds.isNull()) {
        const char* src = ds["source"] | "modbus";
        activeDataSource = strcmp(src, "http") == 0 ? SOURCE_HTTP : SOURCE_MODBUS;
        U.readDataInterval = ds["readDataInterval"] | 5000;
    }
}

// ========== ZAPIS ŹRÓDŁA DANYCH ==========
bool ConfigManager::saveDataSource()
{
    const char* source = (activeDataSource == SOURCE_HTTP) ? "http" : "modbus";

    return updateConfig([&](JsonDocument& doc)
    {
        doc["data_source"]["source"] = source;
        doc["data_source"]["readDataInterval"] = (int)U.readDataInterval;             
    });
}

// ========== ZAPIS ŹRÓDŁA DANYCH (z parametrem) ==========
bool ConfigManager::saveDataSource(const char* source) {
    if (strcmp(source, "http") == 0) {
        activeDataSource = SOURCE_HTTP;
    } else {
        activeDataSource = SOURCE_MODBUS;
    }
    return saveDataSource();  // wywołaj wersję bez parametrów
}

// ========== ODCZYT KONFIGURACJI HTTP ==========
void ConfigManager::loadHttpConfig(JsonDocument& doc) {
    JsonObject http = doc["http_data"];
    if (!http.isNull()) {
        strlcpy(httpDataCfg.addr, http["addr"] | "", sizeof(httpDataCfg.addr));
        //httpDataCfg.interval = http["interval"] | 10000;
        httpDataCfg.timeout = http["timeout"] | 2000;
        httpDataCfg.maxRetries = http["max_retries"] | 3;
        httpDataCfg.retryDelay = http["retry_delay"] | 1000;
    }
}

// ========== ZAPIS KONFIGURACJI HTTP DATA ==========
bool ConfigManager::saveHttpDataConfig()
{
    return updateConfig([this](JsonDocument& doc)
    {
        JsonObject http = doc["http_data"];

        http["addr"] = httpDataCfg.addr;
        //http["interval"] = httpDataCfg.interval;
        http["timeout"] = httpDataCfg.timeout;
        http["max_retries"] = httpDataCfg.maxRetries;
        http["retry_delay"] = httpDataCfg.retryDelay;
    });
}

// ========== ODCZYT KONFIGURACJI MODBUS ==========
void ConfigManager::loadModbusConfig(JsonDocument& doc) {
    JsonObject modbus = doc["modbus"];
    if (!modbus.isNull()) {
        strlcpy(modbusCfg.ip, modbus["ip"] | "192.168.20.70", sizeof(modbusCfg.ip));
        modbusCfg.port = modbus["port"] | 502;
        modbusCfg.unitId = modbus["unitId"] | 1;
        //modbusCfg.readInterval = modbus["readInterval"] | 1000;
        modbusCfg.timeout = modbus["timeout"] | 1000;
        modbusCfg.maxRetries = modbus["max_retries"] | 3;
        modbusCfg.retryDelay = modbus["retry_delay"] | 1000;
    }
}       

// ========== ZAPIS KONFIGURACJI MODBUS ==========
bool ConfigManager::saveModbusConfig()
{
    return updateConfig([this](JsonDocument& doc)
    {
        JsonObject modbus = doc["modbus"];

        modbus["ip"] = modbusCfg.ip;
        modbus["port"] = modbusCfg.port;
        modbus["unitId"] = modbusCfg.unitId;
        //modbus["readInterval"] = modbusCfg.readInterval;
        modbus["timeout"] = modbusCfg.timeout;
        modbus["max_retries"] = modbusCfg.maxRetries;
        modbus["retry_delay"] = modbusCfg.retryDelay;
    });
}

// ========== ODCZYT KONFIGURACJI NTP ==========
void ConfigManager::loadNtpConfig(JsonDocument& doc) {
    JsonObject ntp = doc["ntp"];
    if (!ntp.isNull()) {
        strlcpy(ntpCfg.server, ntp["server"] | "pool.ntp.org", sizeof(ntpCfg.server));
        ntpCfg.gmt_offset = ntp["gmt_offset"] | 3600;
        ntpCfg.daylight_offset = ntp["daylight_offset"] | 3600;
        strlcpy(ntpCfg.timezone, ntp["timezone"] | "CET-1CEST-2,M3.5.0/2,M10.5.0/3", sizeof(ntpCfg.timezone));
        ntpCfg.enabled = ntp["enabled"] | true;
    }
}

// ========== ZAPIS KONFIGURACJI NTP ==========
bool ConfigManager::saveNtpConfig()
{
    return updateConfig([this](JsonDocument& doc)
    {
        JsonObject ntp = doc["ntp"];

        ntp["server"] = ntpCfg.server;
        ntp["gmt_offset"] = ntpCfg.gmt_offset;
        ntp["daylight_offset"] = ntpCfg.daylight_offset;
        ntp["timezone"] = ntpCfg.timezone;
        ntp["enabled"] = ntpCfg.enabled;
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
        U.MinPowerLock = s["MinPowerLock"] | false;
        U.MinPower = s["MinPower"] | 3000;
        U.HeaterDelay_on_ms = s["HeaterDelay_on_ms"] | 1000;
        U.HeaterDelay_off_ms = s["HeaterDelay_off_ms"] | 5000;
        U.ContactorDelay_off_sec = s["ContactorDelay_off_sec"] | 600;
        U.bojlerTmax = s["bojlerTmax"] | 80;
        U.radiatorTmax = s["radiatorTmax"] | 70;
        U.radiatorT_critical = s["radiatorT_critical"] | false;
        U.serwer_www_port = s["serwer_www_port"] | 80;
        U.temperatureLogInterval = s["temperatureLogInterval"] | 120000;
    }
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
      s["MinPowerLock"] = U.MinPowerLock;
      s["MinPower"] = U.MinPower;
      s["HeaterDelay_on_ms"] = U.HeaterDelay_on_ms;
      s["HeaterDelay_off_ms"] = U.HeaterDelay_off_ms;
      s["ContactorDelay_off_sec"] = U.ContactorDelay_off_sec;
      s["bojlerTmax"] = (int)U.bojlerTmax;
      s["radiatorTmax"] = (int)U.radiatorTmax;
      s["radiatorT_critical"] = U.radiatorT_critical;
      s["serwer_www_port"] = (int)U.serwer_www_port;  
      s["temperatureLogInterval"] = (int)U.temperatureLogInterval;  
  });  
}

// ========== DRUKOWANIE KONFIGURACJI ==========
void ConfigManager::printConfig() {
  if (wifiCfg.active) {
    Serial.println("=== KONFIGURACJA WIFI ===");
    Serial.print("SSID: "); Serial.println(wifiCfg.ssid);
    Serial.print("IP: "); Serial.println(wifiCfg.ip);
    Serial.print("DHCP: "); Serial.println(wifiCfg.dhcp ? "true" : "false");
  } else {
    Serial.println("=== KONFIGURACJA WIFI ===");
    Serial.println("WiFi nieaktywne - urucham trybu AP");
    Serial.print("AP SSID: "); Serial.println(apCfg.ssid);
    Serial.print("AP IP: "); Serial.println(apCfg.ip);    
  }

  Serial.println("=== GŁÓWNA KONFIGURACJA ===");
  if(activeDataSource == SOURCE_HTTP) {
    Serial.println("Źródło danych: HTTP");
    Serial.print("HTTP URL: "); Serial.println(httpDataCfg.addr);
  } else if(activeDataSource == SOURCE_MODBUS) {
    Serial.println("Źródło danych: Modbus");
    Serial.print("Modbus IP: "); Serial.println(modbusCfg.ip);
  } else {
    Serial.println("Źródło danych: Brak");
  }
  Serial.println("=== USTAWIENIA ===");
  Serial.print("Heater Enabled: "); Serial.println(U.HeaterEnabled ? "true" : "false");
  Serial.print("Ugrid_on: "); Serial.println(U.Ugrid_on);
  Serial.print("Ugrid_off: "); Serial.println(U.Ugrid_off);
  Serial.print("HeaterDelay_on_ms: "); Serial.println(U.HeaterDelay_on_ms);
  Serial.print("HeaterDelay_off_ms: "); Serial.println(U.HeaterDelay_off_ms);
  Serial.print("ContactorDelay_off_sec: "); Serial.println(U.ContactorDelay_off_sec);
  Serial.print("bojlerTmax: "); Serial.println(U.bojlerTmax);
  Serial.print("radiatorTmax: "); Serial.println(U.radiatorTmax);
  Serial.print("radiatorT_critical: "); Serial.println(U.radiatorT_critical ? "true" : "false");
  Serial.print("readDataInterva: "); Serial.println(U.readDataInterval);
  Serial.print("temperatureLogInterval: "); Serial.println(U.temperatureLogInterval);
}
