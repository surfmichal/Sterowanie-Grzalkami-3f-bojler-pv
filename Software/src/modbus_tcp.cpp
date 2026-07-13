#include "modbus_tcp.h"
#include "globals.h"
#include <WiFi.h>
#include "ModbusClientTCP.h"

// Adresy rejestrów dla falownika Sofar (zgodne z Python)
static const uint16_t START_REGISTER = 0x0000;
static const uint16_t NUM_REGISTERS = 46;  // 0x00 do 0x2D

// Dla falowników Sofar
static const uint8_t DEFAULT_UNIT_ID = 1;   // UWAGA: Python używa modbus_id=1
static const uint16_t DEFAULT_MODBUS_PORT = 502;

static WiFiClient modbusClient;

// ========== KONSTRUKTOR ==========
ModbusManager::ModbusManager() {
  mbClient = nullptr;
  modbusTaskHandle = nullptr;
  inverterData.connected = false;
}

// ========== DESTRUKTOR ==========
ModbusManager::~ModbusManager() {
  if (mbClient) {
    delete mbClient;
  }
}

uint16_t swap16(uint16_t val) {
  return (val >> 8) | ((val & 0xFF) << 8);
}

// ========== HANDLER DANYCH ==========
void handleModbusData(ModbusMessage response, uint32_t token) {
  if (response.getFunctionCode() == 0x03) {
    uint8_t byteCount = response[2];
    uint16_t* values = (uint16_t*)(response.data() + 3);
    int numValues = byteCount / 2;
    
    // Little-Endian swap
    uint16_t swapped[46];
    for (int i = 0; i < numValues; i++) {
      swapped[i] = (values[i] >> 8) | ((values[i] & 0xFF) << 8);
    }
    
    // Teraz używaj swapped zamiast values
    if (numValues >= 45) {
      inverterData.status = swapped[0];
      inverterData.alarm1 = swapped[1];
      inverterData.alarm2 = swapped[2];
      inverterData.alarm3 = swapped[3];
      inverterData.alarm4 = swapped[4];
      inverterData.alarm5 = swapped[5];
      
      inverterData.pv1_voltage = swapped[6] / 10.0f;
      inverterData.pv1_current = swapped[7] / 100.0f;
      inverterData.pv2_voltage = swapped[8] / 10.0f;
      inverterData.pv2_current = swapped[9] / 100.0f;
      
      inverterData.gridFrequency = swapped[14] / 100.0f;
      inverterData.gridVoltage1 = swapped[15] / 10.0f;
      inverterData.gridCurrent1 = swapped[16] / 100.0f;
      inverterData.gridVoltage2 = swapped[17] / 10.0f;
      inverterData.gridCurrent2 = swapped[18] / 100.0f;
      inverterData.gridVoltage3 = swapped[19] / 10.0f;
      inverterData.gridCurrent3 = swapped[20] / 100.0f;      
      
      inverterData.pv1_power = inverterData.pv1_voltage * inverterData.pv1_current;
      inverterData.pv2_power = inverterData.pv2_voltage * inverterData.pv2_current;
      inverterData.total_pv_power = inverterData.pv1_power + inverterData.pv2_power;
      inverterData.gridPower = inverterData.total_pv_power;
      
      inverterData.totalEnergy = (uint32_t)(swapped[21] * 65536 + swapped[22]);
      inverterData.totalHours = swapped[23] * 65536 + swapped[24];
      inverterData.dailyEnergy = swapped[25] / 100.0f;
      inverterData.todayTime = swapped[26];
      inverterData.moduleTemp = swapped[27];
      inverterData.innerTemp = swapped[28];
      inverterData.busVoltage = swapped[29] / 10.0f;
      
      inverterData.insulationPv1 = swapped[36];
      inverterData.insulationPv2 = swapped[37];
      inverterData.insulationToGnd = swapped[38];
      inverterData.country = swapped[39];
      inverterData.comPhA = swapped[43];
      inverterData.comPhB = swapped[44];
      inverterData.comPhC = swapped[45];
      
      inverterData.connected = true;
      
      // Debug
      static unsigned long lastPrint = 0;
      if (millis() - lastPrint > 30000) {
        Serial.println("\n========== SOFAR MODBUS DATA (Little-Endian) ==========");
        Serial.printf("Ua: %.1f V\n", inverterData.gridVoltage1);
        Serial.printf("Ia: %.2f A\n", inverterData.gridCurrent1);
        Serial.printf("Moc PV: %.0f W\n", inverterData.total_pv_power);
        Serial.printf("Temp: %d°C\n", inverterData.innerTemp);
        lastPrint = millis();
      }
    }
  }
}

// ========== HANDLER BŁĘDÓW ==========
void handleModbusError(Error error, uint32_t token) {
  ModbusError me(error);
  Serial.printf("❌ Modbus błąd: %02X - %s\n", (int)me, (const char*)me);
  inverterData.connected = false;
}

// ========== INICJALIZACJA ==========
bool ModbusManager::begin() {
  // Sprawdź czy Modbus jest aktywnym źródłem
  if (activeDataSource != SOURCE_MODBUS) {
    Serial.println("📡 Modbus: pomijam inicjalizację - inne źródło danych");
    inverterData.connected = false;
    return false;
  }
    
  if (!modbusCfg.enabled) {
    Serial.println("⚠️ Modbus wyłączony w konfiguracji");
    return false;
  }
  
  const char* ip = (strlen(modbusCfg.ip) > 0) ? modbusCfg.ip : "192.168.20.70";
  uint16_t port = (modbusCfg.port > 0) ? modbusCfg.port : DEFAULT_MODBUS_PORT;
  uint8_t unitId = (modbusCfg.unitId > 0) ? modbusCfg.unitId : DEFAULT_UNIT_ID;
  
  Serial.printf("📡 Modbus TCP: łączę z %s:%d (Unit ID: %d)\n", ip, port, unitId);
  
  // Inicjalizacja klienta Modbus TCP
  if (mbClient == nullptr) {
    mbClient = new ModbusClientTCP(modbusClient);
  }
  
  // Ustaw handler
  mbClient->onDataHandler(&handleModbusData);
  mbClient->onErrorHandler(&handleModbusError);
  mbClient->setTimeout(2000, 200);
  
  // Ustaw target
  IPAddress modbusIP;
  if (!modbusIP.fromString(ip)) {
    Serial.printf("❌ Błędny adres IP: %s\n", ip);
    return false;
  }
  mbClient->setTarget(modbusIP, port);
  
  // Uruchom
  mbClient->begin();
  
  inverterData.connected = true;
  Serial.println("✅ Modbus TCP: klient uruchomiony");
  
  return true;
}

// ========== ODCZYT WSZYSTKICH REJESTRÓW ==========
void ModbusManager::readAllRegisters() {
  if (!mbClient || !inverterData.connected) return;
  
  uint8_t unitId = (modbusCfg.unitId > 0) ? modbusCfg.unitId : DEFAULT_UNIT_ID;
  
  Error err = mbClient->addRequest((uint32_t)millis(), unitId, READ_HOLD_REGISTER, START_REGISTER, NUM_REGISTERS);
  
  if (err != SUCCESS) {
    ModbusError me(err);
    Serial.printf("❌ Modbus błąd żądania: %02X - %s\n", (int)me, (const char*)me);
    inverterData.connected = false;
  }
}

// ========== AKTUALIZACJA ==========
void ModbusManager::update() {
  // eModbus działa w tle
}

// ========== URUCHOMIENIE TASKA ==========
void ModbusManager::startPeriodicRead() {
  // Uruchom tylko jeśli Modbus jest aktywnym źródłem
  if (activeDataSource != SOURCE_MODBUS) {
    Serial.println("📡 Modbus: nieuruchamiam - inne źródło danych aktywne");
    return;
  }
  
  xTaskCreatePinnedToCore(
    taskModbus,
    "Modbus Task",
    8192,
    this,
    2,
    &modbusTaskHandle,
    0
  );
  
  Serial.println("📡 Modbus: task uruchomiony");
}


void ModbusManager::taskModbus(void* parameter) {
  ModbusManager* manager = (ModbusManager*)parameter;
  
   // SPRAWDŹ CZY MODBUS JEST AKTYWNYM ŹRÓDŁEM
  if (activeDataSource != SOURCE_MODBUS) {
    Serial.println("📡 Modbus: źródło nieaktywne - task kończy pracę");
    vTaskDelete(NULL);
    return;
  }

  uint16_t readInterval = (U.readDataInterval > 0) ? U.readDataInterval : 5000;

  while (true) {
    if (simulationMode) {
      // 🔥 TRYB SYMULACJI
      inverterData.connected  = simulationModbusConnected;  // 
      inverterData.gridVoltage1 = simVoltage1;
      inverterData.gridVoltage2 = simVoltage2;
      inverterData.gridVoltage3 = simVoltage3;
      inverterData.gridCurrent1 = 5.2;
      inverterData.gridCurrent2 = 4.8;
      inverterData.gridCurrent3 = 5.0;
      inverterData.total_pv_power = 3500;
      inverterData.innerTemp = 45;
      
      static unsigned long lastSimLog = 0;
      if (millis() - lastSimLog > 10000) {
        Serial.printf("🔧 SYMULACJA: %s | L1=%.1fV, L2=%.1fV, L3=%.1fV\n", 
                      inverterData.connected  ? "Online" : "Offline",
                      simVoltage1, simVoltage2, simVoltage3);
        lastSimLog = millis();
      }

    
    } else if (activeDataSource != SOURCE_MODBUS) {
      // Sprawdź czy nadal jesteśmy aktywnym źródłem
      Serial.println("📡 Modbus: przełączono na inne źródło - kończę pracę");
      manager->mbClient->setTimeout(0, 0);  // zakończ połączenie
      vTaskDelete(NULL);
      return;
    
    
    } else    {
      // Normalny odczyt
      if (modbusCfg.enabled && manager->mbClient) {
        manager->readAllRegisters();
      }
    }
    
    vTaskDelay(U.readDataInterval / portTICK_PERIOD_MS);
  }
}