#ifndef MODBUS_TCP_H
#define MODBUS_TCP_H

#include <Arduino.h>
#include "ModbusClientTCP.h"
#include "globals.h"

class ModbusManager {
private:
  ModbusClientTCP* mbClient;
  TaskHandle_t modbusTaskHandle;
  
  void readAllRegisters();
  
public:
  ModbusManager();
  ~ModbusManager();
  bool begin();
  void update();
  void startPeriodicRead();
  
  static void taskModbus(void* parameter);
};

#endif