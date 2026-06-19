#ifndef ONEWIRE_H
#define ONEWIRE_H

#include <OneWire.h>
#include <DallasTemperature.h>

class OneWireManager {
private:
  OneWire oneWire;
  DallasTemperature sensors;
  float lastTemperature;
  
public:
  OneWireManager(uint8_t pin);
  bool begin();
  float getTemperature();
  void requestTemperature();
};

#endif