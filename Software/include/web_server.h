#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>
#include "config_manager.h"
#include "wifi_manager.h"
#include "globals.h"

class WebServerManager {
private:
  WebServer server;
  ConfigManager* config;
  WiFiManager* wifi;
  
  // Strony
  void handleRoot();           // Strona główna (status)
  void handleSettings();       // NOWA: strona ustawień
  
  // API
  void handleApiStatus();
  void handleApiConfig();
  void handleApiData();
  void handleApiSaveConfig();  // NOWA: zapis całej konfiguracji
  void handleApiSaveWiFi();    // NOWA: zapis WiFi
  void handleApiSaveModbus();  // NOWA: zapis Modbus
  void handleApiSaveHeater();  // NOWA: zapis ustawień grzałek
  void handleApiHeaterConfig(); 
  void handleApiRestart();
  void handleApiResetWiFi();
  void handleNotFound();
  void handleLogsPage();
  void handleApiLogs();
  void handleApiLogsDownload();
  void handleApiLogsClear();
  void handleApiStatistics();
  void handleApiTemperatureLast();
  void handleApiTemperatureHistory();
  void handleApiTemperatureSummary();
  void handleApiTime();
  void handleApiLogsConfig();
  void handleApiSaveLogsConfig();
  void handleApiVersion();
  void handleApiSimulationGet();
  void handleApiSimulationPost();
  bool saveUstawienia();
  void handleApiHeaterEnable();
  void handleApiHeaterStatus();
  
public:
  WebServerManager(ConfigManager* cfg, WiFiManager* wifiMgr);
  void begin();
  void handle();
};

#endif