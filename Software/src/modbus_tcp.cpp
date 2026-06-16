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
  modbusData.connected = false;
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
      modbusData.status = swapped[0];
      modbusData.alarm1 = swapped[1];
      modbusData.alarm2 = swapped[2];
      modbusData.alarm3 = swapped[3];
      modbusData.alarm4 = swapped[4];
      modbusData.alarm5 = swapped[5];
      
      modbusData.pv1_voltage = swapped[6] / 10.0f;
      modbusData.pv1_current = swapped[7] / 100.0f;
      modbusData.pv2_voltage = swapped[8] / 10.0f;
      modbusData.pv2_current = swapped[9] / 100.0f;
      
      modbusData.gridFrequency = swapped[14] / 100.0f;
      modbusData.gridVoltage1 = swapped[15] / 10.0f;
      modbusData.gridCurrent1 = swapped[16] / 100.0f;
      modbusData.gridVoltage2 = swapped[17] / 10.0f;
      modbusData.gridCurrent2 = swapped[18] / 100.0f;
      modbusData.gridVoltage3 = swapped[19] / 10.0f;
      modbusData.gridCurrent3 = swapped[20] / 100.0f;
      
      modbusData.pv1_power = modbusData.pv1_voltage * modbusData.pv1_current;
      modbusData.pv2_power = modbusData.pv2_voltage * modbusData.pv2_current;
      modbusData.total_pv_power = modbusData.pv1_power + modbusData.pv2_power;
      modbusData.power = modbusData.total_pv_power;
      
      modbusData.totalEnergy = (uint32_t)(swapped[21] * 65536 + swapped[22]);
      modbusData.totalHours = swapped[23] * 65536 + swapped[24];
      modbusData.dailyEnergy = swapped[25] / 100.0f;
      modbusData.todayTime = swapped[26];
      modbusData.moduleTemp = swapped[27];
      modbusData.innerTemp = swapped[28];
      modbusData.busVoltage = swapped[29] / 10.0f;
      
      modbusData.insulationPv1 = swapped[36];
      modbusData.insulationPv2 = swapped[37];
      modbusData.insulationToGnd = swapped[38];
      modbusData.country = swapped[39];
      modbusData.comPhA = swapped[43];
      modbusData.comPhB = swapped[44];
      modbusData.comPhC = swapped[45];
      
      modbusData.connected = true;
      
      // Debug
      static unsigned long lastPrint = 0;
      if (millis() - lastPrint > 30000) {
        Serial.println("\n========== SOFAR MODBUS DATA (Little-Endian) ==========");
        Serial.printf("Ua: %.1f V\n", modbusData.gridVoltage1);
        Serial.printf("Ia: %.2f A\n", modbusData.gridCurrent1);
        Serial.printf("Moc PV: %.0f W\n", modbusData.total_pv_power);
        Serial.printf("Temp: %d°C\n", modbusData.innerTemp);
        lastPrint = millis();
      }
    }
  }
}

// ========== HANDLER BŁĘDÓW ==========
void handleModbusError(Error error, uint32_t token) {
  ModbusError me(error);
  Serial.printf("❌ Modbus błąd: %02X - %s\n", (int)me, (const char*)me);
  modbusData.connected = false;
}

// ========== INICJALIZACJA ==========
bool ModbusManager::begin() {
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
  
  modbusData.connected = true;
  Serial.println("✅ Modbus TCP: klient uruchomiony");
  
  return true;
}

// ========== ODCZYT WSZYSTKICH REJESTRÓW ==========
void ModbusManager::readAllRegisters() {
  if (!mbClient || !modbusData.connected) return;
  
  uint8_t unitId = (modbusCfg.unitId > 0) ? modbusCfg.unitId : DEFAULT_UNIT_ID;
  
  Error err = mbClient->addRequest((uint32_t)millis(), unitId, READ_HOLD_REGISTER, START_REGISTER, NUM_REGISTERS);
  
  if (err != SUCCESS) {
    ModbusError me(err);
    Serial.printf("❌ Modbus błąd żądania: %02X - %s\n", (int)me, (const char*)me);
    modbusData.connected = false;
  }
}

// ========== AKTUALIZACJA ==========
void ModbusManager::update() {
  // eModbus działa w tle
}

// ========== URUCHOMIENIE TASKA ==========
void ModbusManager::startPeriodicRead() {
  xTaskCreatePinnedToCore(
    taskModbus,
    "Modbus Task",
    8192,
    this,
    2,
    &modbusTaskHandle,
    0
  );
}

// ========== TASK FreeRTOS ==========
/*
void ModbusManager::taskModbus(void* parameter) {
  ModbusManager* manager = (ModbusManager*)parameter;
  uint16_t readInterval = (modbusCfg.readInterval > 0) ? modbusCfg.readInterval : 5000;
  
  while (true) {
    if (modbusCfg.enabled && manager->mbClient) {
      manager->readAllRegisters();
    }
    vTaskDelay(readInterval / portTICK_PERIOD_MS);
  }
}
*/

void ModbusManager::taskModbus(void* parameter) {
  ModbusManager* manager = (ModbusManager*)parameter;
  
  while (true) {
    if (simulationMode) {
      // 🔥 TRYB SYMULACJI
      modbusData.connected = simulationModbusConnected;  // 
      modbusData.gridVoltage1 = simVoltage1;
      modbusData.gridVoltage2 = simVoltage2;
      modbusData.gridVoltage3 = simVoltage3;
      modbusData.gridCurrent1 = 5.2;
      modbusData.gridCurrent2 = 4.8;
      modbusData.gridCurrent3 = 5.0;
      modbusData.total_pv_power = 3500;
      modbusData.innerTemp = 45;
      
      static unsigned long lastSimLog = 0;
      if (millis() - lastSimLog > 10000) {
        Serial.printf("🔧 SYMULACJA: %s | L1=%.1fV, L2=%.1fV, L3=%.1fV\n", 
                      modbusData.connected ? "Online" : "Offline",
                      simVoltage1, simVoltage2, simVoltage3);
        lastSimLog = millis();
      }
    } else {
      // Normalny odczyt
      if (modbusCfg.enabled && manager->mbClient) {
        manager->readAllRegisters();
      }
    }
    
    vTaskDelay(modbusCfg.readInterval / portTICK_PERIOD_MS);
  }
}