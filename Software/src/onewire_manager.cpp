#include "onewire_manager.h"
#include <Arduino.h>

OneWireManager::OneWireManager(uint8_t pin) : pin(pin) {
  oneWire = nullptr;
  sensors = nullptr;
  deviceCount = 0;
  initialized = false;
  lastTemperature = -127.0f;
  lastReadTime = 0;
}

OneWireManager::~OneWireManager() {
  if (sensors) {
    delete sensors;
    sensors = nullptr;
  }
  if (oneWire) {
    delete oneWire;
    oneWire = nullptr;
  }
}

bool OneWireManager::begin() {
  if (initialized) return true;
  
  oneWire = new OneWire(pin);
  sensors = new DallasTemperature(oneWire);
  sensors->begin();
  
  deviceCount = sensors->getDeviceCount();
  initialized = true;
  
  if (deviceCount > 0) {
    Serial.printf("✅ Znaleziono %d czujnik(i) DS18B20 na GPIO%d\n", deviceCount, pin);
    printAddress(0);
    return true;
  } else {
    Serial.printf("⚠️ Brak czujnika DS18B20 na GPIO%d\n", pin);
    return false;
  }
}

bool OneWireManager::isInitialized() {
  return initialized;
}

int OneWireManager::getDeviceCount() {
  return deviceCount;
}

void OneWireManager::requestTemperatures() {
  if (sensors) {
    sensors->requestTemperatures();
  }
}

float OneWireManager::readTemperature(int index) {
  if (!sensors || deviceCount == 0 || index >= deviceCount) {
    return DEVICE_DISCONNECTED_C;
  }
  
  float temp = sensors->getTempCByIndex(index);
  
  if (temp != DEVICE_DISCONNECTED_C) {
    lastTemperature = temp;
    lastReadTime = millis();
  }
  
  return temp;
}

float OneWireManager::getLastTemperature() {
  return lastTemperature;
}

bool OneWireManager::isDevicePresent(int index) {
  if (!sensors || deviceCount == 0) return false;
  float temp = sensors->getTempCByIndex(index);
  return (temp != DEVICE_DISCONNECTED_C);
}

void OneWireManager::printAddress(int index) {
  if (!sensors || deviceCount == 0 || index >= deviceCount) {
    Serial.println("  Brak czujnika");
    return;
  }
  
  DeviceAddress address;
  sensors->getAddress(address, index);
  Serial.print("  Adres czujnika: ");
  for (uint8_t i = 0; i < 8; i++) {
    Serial.printf("%02X", address[i]);
  }
  Serial.println();
}