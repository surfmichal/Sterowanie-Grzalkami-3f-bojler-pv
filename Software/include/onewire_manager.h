#ifndef ONEWIRE_MANAGER_H
#define ONEWIRE_MANAGER_H

#include <Arduino.h>
#include <DallasTemperature.h>

class OneWireManager {
private:
  OneWire* oneWire;
  DallasTemperature* sensors;
  uint8_t pin;
  int deviceCount;
  bool initialized;
  float lastTemperature;
  unsigned long lastReadTime;
  
public:
  OneWireManager(uint8_t pin);
  ~OneWireManager();
  
  bool begin();
  bool isInitialized();
  int getDeviceCount();
  float readTemperature(int index = 0);
  float getLastTemperature();
  bool isDevicePresent(int index = 0);
  void requestTemperatures();
  void printAddress(int index = 0);
};

#endif