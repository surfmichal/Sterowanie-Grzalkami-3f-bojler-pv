#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <functional>
#include "globals.h"

class ConfigManager 
{
  private:
    static const char* mainConfigPath;    // "config.json"
    
    // Nie potrzebujemy już wifiConfig, bo używamy globalnej wifiCfg
    
    bool loadConfig(JsonDocument& doc);
    bool saveConfig(const JsonDocument& doc);
    bool updateConfig(std::function<void(JsonDocument&)> updater);

    void loadWifiConfig(JsonDocument& doc);
    void loadAPConfig(JsonDocument& doc);
    void loadModbusConfig(JsonDocument& doc);
    void loadHttpConfig(JsonDocument& doc);
    void loadSettingsConfig(JsonDocument& doc);
    void loadNtpConfig(JsonDocument& doc);
    void loadDataSourceConfig(JsonDocument& doc);
    
  public:
    ConfigManager();
    bool begin();
    
    // ===== METODY ZAPISU =====
    bool saveWifiConfig();
    bool saveAPConfig();
    bool saveModbusConfig();
    bool saveHttpDataConfig();
    bool saveUstawienia();
    bool saveDataSource();
    bool saveNtpConfig();

    bool saveAPConfig(const char* ssid, const char* pass, 
                      const char* ip = "192.168.4.1", bool active = true);

    bool saveWifiConfig(const char* ssid, const char* pass, 
                      const char* ip = "", const char* gate = "", 
                      const char* mask = "", const char* dns = "",
                      bool dhcp = true, bool active = true);  
    bool saveDataSource(const char* source);

    // ===== METODY ODCZYTU =====
    bool loadMainConfig();
    
    // ===== INNE =====
    void printConfig();

    // ===== GATTERY INLINE =====
    HttpDataConfig* getHttpDataConfig() { return &httpDataCfg; }
    WifiConfig* getWifiConfig() { return &wifiCfg; }
    APConfig* getAPConfig() { return &apCfg; }
    ModbusConfig* getModbusConfig() { return &modbusCfg; }
    NtpConfig* getNtpConfig() { return &ntpCfg; }

};

#endif