#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "globals.h"

class ConfigManager {
private:
  static const char* mainConfigPath;    // "config.json"
  static const char* wlanConfigPath;    // "wlan.json"
  
  WifiConfig wifiConfig;
  
  bool loadMainConfig();
  bool saveMainConfig();
  
  bool loadWlanConfig();    // NOWE
  bool saveWlanConfig();    // NOWE
  
public:
  ConfigManager();
  bool begin();
  
  // Dla głównej konfiguracji
  WifiConfig* getWifiConfig();
  bool saveWifiConfig(const char* ssid, const char* pass, 
                                   const char* ip, const char* gate, 
                                   const char* mask, const char* dns,
                                   bool dhcp = true, bool active = true); 

  void printConfig();
  
  // NOWE: dla konfiguracji AP
  bool loadAPConfig();
  bool saveAPConfig(const char* ssid = nullptr, const char* password = nullptr, bool active = true);
  const char* getAP_SSID();
  const char* getAP_Password();
  bool isAPActive();

  bool saveModbusConfig();
  bool saveHeaterConfig();
  bool loadUstawienia();
  bool saveUstawienia();
  
};

#endif