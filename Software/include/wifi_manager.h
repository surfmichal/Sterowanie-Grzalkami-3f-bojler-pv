#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "config_manager.h"
#include "globals.h"


class WiFiManager {
private:
  WebServer server;
  DNSServer dnsServer;
  ConfigManager* config;
  bool apMode;
  const char* ap_ssid = "ESP_Config";
  const char* ap_password = "";
  
  void startAPMode();
  void handleRoot();
  void handleConnect();
  void handleReset();
  void handleScan();
  void handleStatus();
  void handleSetAP();
  void handleSetAPConfig();  
  
public:
  WiFiManager(ConfigManager* cfg);
  
  void handle();
  bool isAPMode();
  String getLocalIP();
  void reconnect();
  bool begin();
  bool begin(bool forceAP);
  
};

#endif