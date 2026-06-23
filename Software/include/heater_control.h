#ifndef HEATER_CONTROL_H
#define HEATER_CONTROL_H

#include <Arduino.h>
#include "globals.h"

class HeaterControl {
private:
  HeaterState* heater_states[3];
  
  bool isTemperatureSafe();
  bool isModbusDataValid();
  bool shouldTurnOn(float voltage);                    // Sprawdź czy załączyć
  bool shouldStartTurnOffTimer(float voltage);         // Sprawdź czy wyłączyć
  bool shouldCancelTurnOffTimer(float voltage);        // Sprawdź czy anulować wyłączenie
  
  void startTurnOnTimer(int index);                    // Rozpocznij odliczanie do załączenia
  void cancelTurnOnTimer(int index);                   // Anuluj odliczanie do załączenia
  void turnOnNow(int index);                           // Natychmiastowe załączenie
  void startTurnOffTimer(int index);                   // Rozpocznij odliczanie do wyłączenia
  void cancelTurnOffTimer(int index);                  // Anuluj odliczanie do wyłączenia
  void updateHeaterState(int index);                   // Aktualizacja stanu (sprawdza timery)
  
public:
  HeaterControl();
  void begin();
  void update();  // Główna funkcja sterowania (wywoływana co 1 sekundę)
  
  void setThresholds(float U_on, float U_off);
  void setDelays(uint16_t delay_on_ms, uint16_t delay_off_ms);
  void enableSystem(bool enable);
  
  bool getHeaterState(int heaterIndex);
  int getActiveHeatersCount();
  void setBojlerTemperature(float temp);
  void setModbusStatus(bool connected);
  void printStatus();
};

#endif