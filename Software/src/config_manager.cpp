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
  
  // Wczytaj główną konfigurację
  bool mainOk = loadMainConfig();
  if (!mainOk) {
    Serial.println("Brak config.json");
    // saveWifiConfig("", "", "", "", "", "", true, true);
  }
  
  // Wczytaj konfigurację AP
  bool apOk = loadWlanConfig();
  if (!apOk) {
    Serial.println("Brak wlan.json, tworzę domyślny...");
    saveAPConfig("ESP_Config", "", true);
  }
  
  return mainOk || apOk;
}

// ========== OBSŁUGA GŁÓWNEJ KONFIGURACJI ==========
bool ConfigManager::loadMainConfig() {
  File file = LittleFS.open(mainConfigPath, "r");
  if (!file) {
    Serial.println("Brak pliku config.json");
    return false;
  }
  
  String content = file.readString();
  file.close();
  
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, content);
  
  if (error) {
    Serial.print("Błąd parsowania JSON: ");
    Serial.println(error.c_str());
    return false;
  }
  
  JsonObject wifi = doc["wifi"];
  
  // Odczyt danych WiFi
  strlcpy(wifiConfig.ssid, wifi["ssid"] | "", sizeof(wifiConfig.ssid));
  strlcpy(wifiConfig.pass, wifi["pass"] | "", sizeof(wifiConfig.pass));
  strlcpy(wifiConfig.ip, wifi["ip"] | "", sizeof(wifiConfig.ip));
  strlcpy(wifiConfig.gate, wifi["gate"] | "", sizeof(wifiConfig.gate));
  strlcpy(wifiConfig.mask, wifi["mask"] | "", sizeof(wifiConfig.mask));
  strlcpy(wifiConfig.dns, wifi["dns"] | "", sizeof(wifiConfig.dns));
  
  // Odczyt DHCP
  if (wifi.containsKey("dhcp")) {
    if (wifi["dhcp"].is<bool>()) {
      wifiConfig.dhcp = wifi["dhcp"].as<bool>();
    } else if (wifi["dhcp"].is<const char*>()) {
      String dhcpStr = wifi["dhcp"].as<const char*>();
      wifiConfig.dhcp = (dhcpStr == "true" || dhcpStr == "1" || dhcpStr == "True");
    } else if (wifi["dhcp"].is<int>()) {
      wifiConfig.dhcp = wifi["dhcp"].as<int>() != 0;
    } else {
      wifiConfig.dhcp = wifi["dhcp"] | true;
    }
  } else {
    Serial.println("  dhcp NIE istnieje w JSON - używam domyślnego true");
    wifiConfig.dhcp = true;
  }
  
  // Odczyt active
  wifiConfig.active = wifi["active"] | true;
  
  // ========== ODCZYT KONFIGURACJI MODBUS ==========
  JsonObject modbus = doc["modbus"];
  if (modbus) {
    strlcpy(modbusCfg.ip, modbus["ip"] | "", sizeof(modbusCfg.ip));
    modbusCfg.port = modbus["port"] | 502;
    modbusCfg.unitId = modbus["unitId"] | 1;
    modbusCfg.readInterval = modbus["readInterval"] | 5000;
    modbusCfg.enabled = modbus["enabled"] | false;
 
    Serial.println("📋 Odczytano konfigurację Modbus z config.json:");
    Serial.printf("   enabled: %s\n", modbusCfg.enabled ? "true" : "false");
    Serial.printf("   ip: %s\n", modbusCfg.ip);
    Serial.printf("   port: %d\n", modbusCfg.port);
    Serial.printf("   unitId: %d\n", modbusCfg.unitId);
    Serial.printf("   readInterval: %d\n", modbusCfg.readInterval);
  } else {
    Serial.println("⚠️ Brak sekcji 'modbus' w config.json - używam domyślnych wartości");
    // Ustaw domyślne wartości
    strcpy(modbusCfg.ip, "192.168.20.70");
    modbusCfg.port = 502;
    modbusCfg.unitId = 1;
    modbusCfg.readInterval = 5000;
    modbusCfg.enabled = false;
  }

  // ===== SEKCJA WCZYTYWANIA USTAWIENIA =====
JsonObject Ustawienia = doc["Ustawienia"];
if (Ustawienia) {
  // Sekcja istnieje - odczytaj wartości
  U.HeaterEnabled = Ustawienia["HeaterEnabled"] | true;
  U.Ugrid_on = Ustawienia["Ugrid_on"] | 252.0;
  U.Ugrid_off = Ustawienia["Ugrid_off"] | 250.0;
  U.HeaterDelay_on_ms = Ustawienia["HeaterDelay_on_ms"] | 1000;
  U.HeaterDelay_off_ms = Ustawienia["HeaterDelay_off_ms"] | 5000;
  U.ContactorDelay_off_ms = Ustawienia["ContactorDelay_off_ms"] | 5000;
  U.bojlerTmax = Ustawienia["bojlerTmax"] | 80.0;
  U.radiatorTmax = Ustawienia["radiatorTmax"] | 60.0;
  U.radiatorT_critical = Ustawienia["radiatorT_critical"] | true;
  
  Serial.println("✅ Odczytano ustawienia z sekcji 'Ustawienia':");
  Serial.printf("   HeaterEnabled: %s\n", U.HeaterEnabled ? "true" : "false");
  Serial.printf("   Ugrid_on: %.1f V\n", U.Ugrid_on);
  Serial.printf("   Ugrid_off: %.1f V\n", U.Ugrid_off);
  Serial.printf("   HeaterDelay_on_ms: %d ms\n", U.HeaterDelay_on_ms);
  Serial.printf("   HeaterDelay_off_ms: %d ms\n", U.HeaterDelay_off_ms);
  Serial.printf("   ContactorDelay_off_ms: %d ms\n", U.ContactorDelay_off_ms);
  Serial.printf("   bojlerTmax: %.1f °C\n", U.bojlerTmax);
  Serial.printf("   radiatorTmax: %.1f °C\n", U.radiatorTmax);
  Serial.printf("   radiatorT_critical: %s\n", U.radiatorT_critical ? "true" : "false");
} else {
  // Sekcja nie istnieje - użyj domyślnych
  Serial.println("⚠️ Brak sekcji 'Ustawienia' w config.json - używam domyślnych wartości");
  U.HeaterEnabled = true;
  U.Ugrid_on = 253.0;
  U.Ugrid_off = 250.0;
  U.HeaterDelay_on_ms = 1000;
  U.HeaterDelay_off_ms = 5000;
  U.ContactorDelay_off_ms = 5000;
  U.bojlerTmax = 80.0;
  U.radiatorT_critical = true;
  U.radiatorTmax = 60.0;
}

  
  return true;
}

bool ConfigManager::saveMainConfig() {
  Serial.println("🔴🔴🔴 UWAGA! saveMainConfig() ZAPISUJE config.json!");
  File file = LittleFS.open(mainConfigPath, "w");
  if (!file) {
    Serial.println("Błąd otwarcia config.json do zapisu");
    return false;
  }
  
  DynamicJsonDocument doc(1024);
  doc["wifi"]["ssid"] = wifiConfig.ssid;
  doc["wifi"]["pass"] = wifiConfig.pass;
  doc["wifi"]["ip"] = wifiConfig.ip;
  doc["wifi"]["gate"] = wifiConfig.gate;
  doc["wifi"]["mask"] = wifiConfig.mask;
  doc["wifi"]["dns"] = wifiConfig.dns;
  doc["wifi"]["dhcp"] = wifiConfig.dhcp;
  doc["wifi"]["active"] = wifiConfig.active;

  doc["modbus"]["ip"] = modbusCfg.ip;
  doc["modbus"]["port"] = modbusCfg.port;
  doc["modbus"]["unitId"] = modbusCfg.unitId;
  doc["modbus"]["readInterval"] = modbusCfg.readInterval;
  doc["modbus"]["enabled"] = modbusCfg.enabled;
  
  String output;
  serializeJson(doc, output);
  file.print(output);
  file.close();
  
  Serial.println("✅ Zapisano config.json");
  return true;
}

// ========== OBSŁUGA KONFIGURACJI AP ==========
bool ConfigManager::loadWlanConfig() {
  File file = LittleFS.open(wlanConfigPath, "r");
  if (!file) return false;
  
  String content = file.readString();
  file.close();
  
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, content);
  
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
  DynamicJsonDocument doc(512);
  
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
  
  String output;
  serializeJson(doc, output);
  file.print(output);
  file.close();
  
  Serial.println("✅ Zapisano konfigurację AP");
  return true;
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

// ========== POZOSTAŁE FUNKCJE ==========
WifiConfig* ConfigManager::getWifiConfig() {
  return &wifiConfig;
}

bool ConfigManager::saveWifiConfig(const char* ssid, const char* pass, 
                                   const char* ip, const char* gate, 
                                   const char* mask, const char* dns,
                                   bool dhcp, bool active) {
  Serial.println("🔴🔴🔴 UWAGA! saveWifiConfig() ZAPISUJE config.json!");
  strlcpy(wifiConfig.ssid, ssid, sizeof(wifiConfig.ssid));
  strlcpy(wifiConfig.pass, pass, sizeof(wifiConfig.pass));
  strlcpy(wifiConfig.ip, ip, sizeof(wifiConfig.ip));
  strlcpy(wifiConfig.gate, gate, sizeof(wifiConfig.gate));
  strlcpy(wifiConfig.mask, mask, sizeof(wifiConfig.mask));
  strlcpy(wifiConfig.dns, dns, sizeof(wifiConfig.dns));
  wifiConfig.dhcp = dhcp;
  wifiConfig.active = active;
  
  return saveMainConfig();
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

bool ConfigManager::saveModbusConfig() {
  return saveMainConfig();  // Najprościej - zapisz cały plik config.json
}

bool ConfigManager::saveHeaterConfig() {
  return saveMainConfig();  // Najprościej - zapisz cały plik config.json
}

// ========== ZAPIS STRUKTURY USTAWIENIA ==========
bool ConfigManager::saveUstawienia() {
  Serial.println("saveUstawienia: Zapisuję ustawienia...");
  LOG_INFO("USTAWIENIA","Zapisuję ustawienia do config.json");
  
  File file = LittleFS.open(mainConfigPath, "r");
  DynamicJsonDocument doc(2048);
  
  if (file) {
    String content = file.readString();
    file.close();
    deserializeJson(doc, content);
  }
  
  // Zapisz do sekcji "Ustawienia"
  doc["Ustawienia"]["HeaterEnabled"] = U.HeaterEnabled;
  doc["Ustawienia"]["Ugrid_on"] = U.Ugrid_on;
  doc["Ustawienia"]["Ugrid_off"] = U.Ugrid_off;
  doc["Ustawienia"]["HeaterDelay_on_ms"] = U.HeaterDelay_on_ms;
  doc["Ustawienia"]["HeaterDelay_off_ms"] = U.HeaterDelay_off_ms;
  doc["Ustawienia"]["ContactorDelay_off_ms"] = U.ContactorDelay_off_ms;
  doc["Ustawienia"]["bojlerTmax"] = U.bojlerTmax;
  doc["Ustawienia"]["radiatorTmax"] = U.radiatorTmax;
  doc["Ustawienia"]["radiatorT_critical"] = U.radiatorT_critical;
  doc["Ustawienia"]["serwer_www_port"] = 80;
  
  file = LittleFS.open(mainConfigPath, "w");
  Serial.println("Otwieram plik config.json!");
  if (!file) {
    Serial.println("Błąd otwarcia config.json do zapisu!");
    LOG_ERROR("USTAWIENIA","Błąd otwarcia config.json do zapisu!");
    return false;
  }
  
  String output;
  serializeJson(doc, output);
  file.print(output);
  file.close();
  
  Serial.println("✅ Zapisano ustawienia:");
  Serial.println(output);
  LOG_INFO("USTAWIENIA","Zapisano ustawienia");
  
  return true;
}

// ========== ODCZYT STRUKTURY USTAWIENIA ==========
bool ConfigManager::loadUstawienia() {
  Serial.println("Odczytuje config.json - szukam sekcji 'Ustawienia'...");
  File file = LittleFS.open(mainConfigPath, "r");
  if (!file) {
    Serial.println("Brak config.json - używam domyślnych");
    LOG_INFO("URUCHAMIANIE","Brak config.json - używam domyślnych wartości");
    return false;
  }
  
  String content = file.readString();
  file.close();
  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, content);
  
  if (error) {
    Serial.println("Błąd parsowania JSON");
    LOG_ERROR("URUCHAMIANIE","Błąd parsowania JSON");
    return false;
  }
  
  JsonObject Ustawienia = doc["Ustawienia"];
  if (Ustawienia) {
    U.HeaterEnabled = Ustawienia["HeaterEnabled"] | true;
    U.Ugrid_on = Ustawienia["Ugrid_on"] | 252.0;
    U.Ugrid_off = Ustawienia["Ugrid_off"] | 250.0;
    U.HeaterDelay_on_ms = Ustawienia["HeaterDelay_on_ms"] | 1000;
    U.HeaterDelay_off_ms = Ustawienia["HeaterDelay_off_ms"] | 5000;
    U.ContactorDelay_off_ms = Ustawienia["ContactorDelay_off_ms"] | 5000;
    U.bojlerTmax = Ustawienia["bojlerTmax"] | 80.0;
    U.radiatorTmax = Ustawienia["radiatorTmax"] | 70.0;
    U.radiatorT_critical = Ustawienia["radiatorT_critical"] | false;
            
    Serial.println("✅ Wczytano ustawienia z config.json");
    LOG_INFO("URUCHAMIANIE","Wczytano ustawienia z config.json");
    return true;
  }

  JsonObject ntpCfg = doc["ntp"];
  if (ntpCfg) {
    String ntpServer = ntpCfg["server"] | "pool.ntp.org";
    long gmtOffset = ntpCfg["gmt_offset"] | 3600;
    int daylightOffset = ntpCfg["daylight_offset"] | 3600;
  }
  
  Serial.println("Brak sekcji 'Ustawienia' - używam domyślnych");
  LOG_INFO("URUCHAMIANIE","Brak sekcji 'Ustawienia' w config.json - używam domyślnych wartości");
  
  return false;
}