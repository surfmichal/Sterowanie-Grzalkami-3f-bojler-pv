#include "data_manager.h"
#include "modbus_tcp.h"
#include "http_data_client.h"
#include "globals.h"

extern ModbusManager modbus;
extern HttpDataClient httpClient;
extern DataSource activeDataSource;
extern InverterData inverterData;

DataManager::DataManager() {
  currentSource = SOURCE_NONE;
  dataValid = false;
  lastUpdate = 0;
}

void DataManager::begin() {
  currentSource = activeDataSource;
  
  // Zatrzymaj poprzednie źródło
  stopCurrentSource();
  
  // Uruchom nowe źródło
  switch (currentSource) {
    case SOURCE_MODBUS:
      // Modbus sam uruchomi task w startPeriodicRead()
      modbus.begin();
      modbus.startPeriodicRead();
      Serial.println("📡 DataManager: Modbus TCP aktywny");
      break;
      
    case SOURCE_HTTP:
      httpClient.begin();
      // HTTP działa w taskDataFetch
      Serial.println("📡 DataManager: HTTP Data aktywny");
      break;
      
    default:
      Serial.println("⚠️ DataManager: BRAK źródła danych");
      break;
  }
}

void DataManager::stopCurrentSource() {
  switch (currentSource) {
    case SOURCE_MODBUS:
      // Task Modbus sam się zakończy gdy sprawdzi activeDataSource
      break;
    case SOURCE_HTTP:
      // Nic nie trzeba robić
      break;
    default:
      break;
  }
}

bool DataManager::fetchData() {
  bool success = false;
  
  switch (currentSource) {
    case SOURCE_MODBUS:
      success = inverterData.mbConnected;  // Modbus ustawia tę flagę
      break;
      
    case SOURCE_HTTP:
      success = httpClient.fetchDataAsync();
      // fetchDataAsync() ustawia inverterData.httpConnected
      break;
      
    default:
      success = false;
      break;
  }
  
  if (success) {
    lastUpdate = millis();
    dataValid = true;
  } else {
    dataValid = false;
  }
  
  return success;
}

bool DataManager::isDataValid() {
  return dataValid && (millis() - lastUpdate < 30000);  // max 30s stare
}

bool DataManager::setSource(DataSource newSource) {
  if (newSource == currentSource) return true;
  
  // Wyłącz stare źródło
  switch (currentSource) {
    case SOURCE_MODBUS:
      // Nic nie trzeba robić - Modbus sam się wyłączy
      break;
    case SOURCE_HTTP:
      // Nic nie trzeba robić
      break;
    default:
      break;
  }
  
  // Włącz nowe źródło
  currentSource = newSource;
  activeDataSource = newSource;
  
  switch (newSource) {
    case SOURCE_MODBUS:
      modbus.begin();
      modbus.startPeriodicRead();
      Serial.println("📡 DataManager: przełączono na Modbus TCP");
      break;
      
    case SOURCE_HTTP:
      httpClient.begin();
      Serial.println("📡 DataManager: przełączono na HTTP Data");
      break;
      
    default:
      Serial.println("⚠️ DataManager: wyłączono źródło danych");
      break;
  }
  
  dataValid = false;
  return true;
}

String DataManager::getSourceName() {
  switch (currentSource) {
    case SOURCE_MODBUS: return "Modbus TCP";
    case SOURCE_HTTP:   return "HTTP Server";
    case SOURCE_NONE:   return "Brak";
    default:            return "Nieznane";
  }
}