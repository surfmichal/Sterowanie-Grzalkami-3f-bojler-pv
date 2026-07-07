#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "globals.h"

class ConfigManager 
{
  private:
    static const char* mainConfigPath;    // "config.json"
    static const char* wlanConfigPath;    // "wlan.json"
    
    WifiConfig wifiConfig;
    
    bool loadConfig(JsonDocument& doc);
    bool saveConfig(const JsonDocument& doc);
    bool updateConfig(std::function<void(JsonDocument&)> updater);

    void loadWifiConfig(JsonDocument& doc);
    void loadModbusConfig(JsonDocument& doc);
    void loadHttpConfig(JsonDocument& doc);
    void loadSettingsConfig(JsonDocument& doc);
    void loadNtpConfig(JsonDocument& doc);
    void loadDataSourceConfig(JsonDocument& doc);
    
  public:
    ConfigManager();
    bool begin();
    
    // Dla głównej konfiguracji
    WifiConfig* getWifiConfig();
    
    bool loadMainConfig();
    bool loadWlanConfig();
    bool saveAPConfig(const char* ssid, const char* password, bool active);

    bool saveWifiConfig();
    bool saveModbusConfig();
    bool saveHttpDataConfig();
    bool saveUstawienia();
    bool saveDataSource(const char* source);

    WifiConfig& wifi(); 
    ModbusConfig& modbus(); 
    HttpDataConfig& http(); 
    Ustawienia& ustawienia(); 

  
};

#endif