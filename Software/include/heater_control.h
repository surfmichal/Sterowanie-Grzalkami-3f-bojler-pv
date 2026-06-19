#ifndef HEATER_CONTROL_H
#define HEATER_CONTROL_H

#include <Arduino.h>
#include "globals.h"

class HeaterControl {
private:
  HeaterState* heater_states[3];
  
  bool isTemperatureSafe();
  bool isModbusDataValid();
  bool shouldTurnOn(float voltage);                    // Natychmiastowe załączenie
  bool shouldStartTurnOffTimer(float voltage);         // Rozpocznij odliczanie
  bool shouldCancelTurnOffTimer(float voltage);        // Anuluj odliczanie (napięcie wzrosło)

  void updateHeaterFlag(int index, bool state);
  
  void turnOnNow(int index);                           // Natychmiastowe załączenie
  void startTurnOffTimer(int index);                   // Rozpocznij odliczanie
  void cancelTurnOffTimer(int index);                  // Anuluj odliczanie
  void updateHeaterState(int index);                   // Aktualizacja stanu (sprawdza timer)
  

  bool isAnyHeaterRequested();
  void updateContactor();
  void turnOnContactor();
  void turnOffContactor();
  
public:
  HeaterControl();
  void begin();
  
  void update();  // Główna funkcja sterowania (wywoływana co 1 sekundę)
  
  void setThresholds(float U_on, float U_off);
  void setTurnOffDelay(uint16_t Td_ms);
  void enableSystem(bool enable);
  
  bool getHeaterState(int heaterIndex);
  int getActiveHeatersCount();
  void setBojlerTemperature(float temp);
  void setModbusStatus(bool connected);
  void printStatus();
};

#endif