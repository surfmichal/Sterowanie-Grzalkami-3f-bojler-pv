#include "heater_control.h"
#include "globals.h"
#include "logger.h"
#include "statistics.h"

extern HeaterState heater1_state;
extern HeaterState heater2_state;
extern HeaterState heater3_state;
extern Ustawienia U;
extern ModbusData modbusData;
extern Temperatury T;

HeaterControl::HeaterControl() {
  heater_states[0] = &heater1_state;
  heater_states[1] = &heater2_state;
  heater_states[2] = &heater3_state;
}

void HeaterControl::begin() {
  Serial.println("HeaterControl zainicjalizowany");
  
  pinMode(GRZALKA1_pin, OUTPUT);
  pinMode(GRZALKA2_pin, OUTPUT);
  pinMode(GRZALKA3_pin, OUTPUT);
  pinMode(LED_GRZALKA1_pin, OUTPUT);
  pinMode(LED_GRZALKA2_pin, OUTPUT);
  pinMode(LED_GRZALKA3_pin, OUTPUT);
  
  digitalWrite(GRZALKA1_pin, GRZALKA_OFF);
  digitalWrite(GRZALKA2_pin, GRZALKA_OFF);
  digitalWrite(GRZALKA3_pin, GRZALKA_OFF);
  digitalWrite(LED_GRZALKA1_pin, LED_OFF);
  digitalWrite(LED_GRZALKA2_pin, LED_OFF);
  digitalWrite(LED_GRZALKA3_pin, LED_OFF);

  Z.heater1_flag = false;
  Z.heater2_flag = false;
  Z.heater3_flag = false;
}

bool HeaterControl::isModbusDataValid() {
  if (!modbusData.connected) return false;
  
  if (modbusData.gridVoltage1 < 150 || modbusData.gridVoltage1 > 270) return false;
  if (modbusData.gridVoltage2 < 150 || modbusData.gridVoltage2 > 270) return false;
  if (modbusData.gridVoltage3 < 150 || modbusData.gridVoltage3 > 270) return false;
  
  return true;
}

bool HeaterControl::isTemperatureSafe() {
  unsigned long now = millis();
  
  // === BOJLER ===
  if (!T.bojler.ok) {
    LOG_ERROR_DEDUP("HeaterControl", "Czujnik bojlera nie działa - BLOKADA grzałek!");
    return false;
  }
  
  if (T.bojler.temperatura >= U.bojlerTmax) {
    LOG_ERROR_DEDUP("HeaterControl", "Temperatura bojlera %.1f°C >= %.1f°C - BLOKADA!", 
                    T.bojler.temperatura, U.bojlerTmax);
    return false;
  }
  
  // === RADIATOR ===
  if (T.radiator.ok) {
    if (T.radiator.temperatura >= U.radiatorTmax) {
      LOG_ERROR_DEDUP("HeaterControl", "Temperatura radiatora %.1f°C >= %.1f°C - BLOKADA!", 
                      T.radiator.temperatura, U.radiatorTmax);
      return false;
    }
    
    if (T.radiator.temperatura >= U.radiatorTmax - 5.0) {
      LOG_WARN_DEDUP("HeaterControl", "Ostrzeżenie: radiator %.1f°C (blisko limitu %.1f°C)", 
                     T.radiator.temperatura, U.radiatorTmax);
    }
  } else {
    if (U.radiatorT_critical) {
      LOG_ERROR_DEDUP("HeaterControl", "Czujnik radiatora nie działa i jest KRYTYCZNY - BLOKADA!");
      return false;
    } else {
      LOG_WARN_DEDUP("HeaterControl", "Czujnik radiatora nie działa (NIEKRYTYCZNY)");
    }
  }
  
  return true;
}

// ========== ZAŁĄCZANIE (z opóźnieniem) ==========
bool HeaterControl::shouldTurnOn(float voltage) {
  if (!U.HeaterEnabled) return false;
  if (!isModbusDataValid()) return false;
  if (!isTemperatureSafe()) return false;
  
  return (voltage >= U.Ugrid_on);
}

void HeaterControl::startTurnOnTimer(int index) {
  HeaterState* state = heater_states[index];
  
  if (!state->state && !state->waitingToTurnOn) {
    state->waitingToTurnOn = true;
    state->turnOnTime = millis() + U.HeaterDelay_on_ms;
    
    const char* phaseName = "";
    float voltage = 0;
    
    switch(index) {
      case 0: phaseName = "L1"; voltage = modbusData.gridVoltage1; break;
      case 1: phaseName = "L2"; voltage = modbusData.gridVoltage2; break;
      case 2: phaseName = "L3"; voltage = modbusData.gridVoltage3; break;
    }
    
    LOG_INFO("HeaterControl", "[%s] ⏱️ Rozpoczęto odliczanie do ZAŁĄCZENIA (%dms, napięcie: %.1fV)", 
             phaseName, U.HeaterDelay_on_ms, voltage);
  }
}

void HeaterControl::cancelTurnOnTimer(int index) {
  HeaterState* state = heater_states[index];
  
  if (state->waitingToTurnOn) {
    state->waitingToTurnOn = false;
    state->turnOnTime = 0;
    
    const char* phaseName = "";
    float voltage = 0;
    
    switch(index) {
      case 0: phaseName = "L1"; voltage = modbusData.gridVoltage1; break;
      case 1: phaseName = "L2"; voltage = modbusData.gridVoltage2; break;
      case 2: phaseName = "L3"; voltage = modbusData.gridVoltage3; break;
    }
    
    LOG_INFO("HeaterControl", "[%s] 🔄 Anulowano odliczanie do ZAŁĄCZENIA (napięcie spadło do %.1fV)", 
             phaseName, voltage);
  }
}

void HeaterControl::turnOnNow(int index) {
  HeaterState* state = heater_states[index];
  
  if (!state->state) {
    state->state = true;
    state->waitingToTurnOn = false;
    state->turnOnTime = 0;
    state->waitingToTurnOff = false;
    state->turnOffTime = 0;
    
    int pin = -1;
    int ledPin = -1;
    const char* phaseName = "";
    float voltage = 0;
    
    switch(index) {
      case 0: 
        pin = GRZALKA1_pin; 
        ledPin = LED_GRZALKA1_pin; 
        phaseName = "L1";
        voltage = modbusData.gridVoltage1;
        Z.heater1_flag = true;
        break;
      case 1: 
        pin = GRZALKA2_pin; 
        ledPin = LED_GRZALKA2_pin; 
        phaseName = "L2";
        voltage = modbusData.gridVoltage2;
        Z.heater2_flag = true;
        break;
      case 2: 
        pin = GRZALKA3_pin; 
        ledPin = LED_GRZALKA3_pin; 
        phaseName = "L3";
        voltage = modbusData.gridVoltage3;
        Z.heater3_flag = true;
        break;
    }
    
    if (pin != -1) {
      digitalWrite(pin, GRZALKA_ON);
      digitalWrite(ledPin, LED_ON);      
      
      LOG_INFO_DEDUP("HeaterControl", "[%s] 🔥 GRZAŁKA ZAŁĄCZONA (napięcie: %.1fV, T: %.1f°C)", 
                     phaseName, voltage, T.bojler.temperatura);
      
      // Zliczanie załączeń
      incrementHeaterCycles(index + 1);
    }
  }
}

// ========== WYŁĄCZANIE ==========
bool HeaterControl::shouldStartTurnOffTimer(float voltage) {
  if (!U.HeaterEnabled) return false;
  if (!isModbusDataValid()) return false;
  if (!isTemperatureSafe()) return true;
  
  return (voltage <= U.Ugrid_off);
}

bool HeaterControl::shouldCancelTurnOffTimer(float voltage) {
  return (voltage > U.Ugrid_off);
}

void HeaterControl::startTurnOffTimer(int index) {
  HeaterState* state = heater_states[index];
  
  if (state->state && !state->waitingToTurnOff) {
    state->waitingToTurnOff = true;
    state->turnOffTime = millis() + U.HeaterDelay_off_ms;
    
    const char* phaseName = "";
    float voltage = 0;
    
    switch(index) {
      case 0: phaseName = "L1"; voltage = modbusData.gridVoltage1; break;
      case 1: phaseName = "L2"; voltage = modbusData.gridVoltage2; break;
      case 2: phaseName = "L3"; voltage = modbusData.gridVoltage3; break;
    }
    
    LOG_INFO("HeaterControl", "[%s] ⏱️ Rozpoczęto odliczanie do WYŁĄCZENIA (%dms, napięcie: %.1fV)", 
             phaseName, U.HeaterDelay_off_ms, voltage);
  }
}

void HeaterControl::cancelTurnOffTimer(int index) {
  HeaterState* state = heater_states[index];
  
  if (state->waitingToTurnOff) {
    state->waitingToTurnOff = false;
    state->turnOffTime = 0;
    
    const char* phaseName = "";
    float voltage = 0;
    
    switch(index) {
      case 0: phaseName = "L1"; voltage = modbusData.gridVoltage1; break;
      case 1: phaseName = "L2"; voltage = modbusData.gridVoltage2; break;
      case 2: phaseName = "L3"; voltage = modbusData.gridVoltage3; break;
    }
    
    LOG_INFO("HeaterControl", "[%s] 🔄 Anulowano odliczanie do WYŁĄCZENIA (napięcie wzrosło do %.1fV)", 
             phaseName, voltage);
  }
}

void HeaterControl::updateHeaterState(int index) {
  HeaterState* state = heater_states[index];
  unsigned long now = millis();
  
  // Sprawdź czy czas załączenia minął
  if (state->waitingToTurnOn && !state->state) {
    if (now >= state->turnOnTime) {
      // Czas minął - załącz grzałkę
      turnOnNow(index);
    }
  }
  
  // Sprawdź czy czas wyłączenia minął
  if (state->waitingToTurnOff && state->state) {
    if (now >= state->turnOffTime) {
      // Czas minął - wyłącz grzałkę
      state->state = false;
      state->waitingToTurnOff = false;
      state->turnOffTime = 0;
      
      int pin = -1;
      int ledPin = -1;
      const char* phaseName = "";
      float voltage = 0;
      
      switch(index) {
        case 0: 
          pin = GRZALKA1_pin; 
          ledPin = LED_GRZALKA1_pin; 
          phaseName = "L1";
          voltage = modbusData.gridVoltage1;
          Z.heater1_flag = false;
          break;
        case 1: 
          pin = GRZALKA2_pin; 
          ledPin = LED_GRZALKA2_pin; 
          phaseName = "L2";
          voltage = modbusData.gridVoltage2;
          Z.heater2_flag = false;
          break;
        case 2: 
          pin = GRZALKA3_pin; 
          ledPin = LED_GRZALKA3_pin; 
          phaseName = "L3";
          voltage = modbusData.gridVoltage3;
          Z.heater3_flag = false;
          break;
      }
      
      if (pin != -1) {
        digitalWrite(pin, GRZALKA_OFF);
        digitalWrite(ledPin, LED_OFF);
        
        LOG_INFO_DEDUP("HeaterControl", "[%s] ❌ GRZAŁKA WYŁĄCZONA (napięcie: %.1fV)", 
                       phaseName, voltage);
      }
    }
  }
}

// ========== GŁÓWNA FUNKCJA STEROWANIA ==========
void HeaterControl::update() {
  unsigned long now = millis();
  
  // Sprawdź czy mamy ważne dane
  if (!isModbusDataValid()) {
    bool anyHeaterOn = false;
    for (int i = 0; i < 3; i++) {
      if (heater_states[i]->state || heater_states[i]->waitingToTurnOff) {
        anyHeaterOn = true;
        break;
      }
    }
    
    if (anyHeaterOn) {
      LOG_ERROR_DEDUP("HeaterControl", "Brak danych Modbus - wyłączam grzałki!");
    }
    
    // Wyłącz wszystkie grzałki
    for (int i = 0; i < 3; i++) {
      if (heater_states[i]->state || heater_states[i]->waitingToTurnOff) {
        heater_states[i]->state = false;
        heater_states[i]->waitingToTurnOff = false;
        heater_states[i]->turnOffTime = 0;
        heater_states[i]->waitingToTurnOn = false;
        heater_states[i]->turnOnTime = 0;
        
        int pin = (i == 0 ? GRZALKA1_pin : (i == 1 ? GRZALKA2_pin : GRZALKA3_pin));
        int ledPin = (i == 0 ? LED_GRZALKA1_pin : (i == 1 ? LED_GRZALKA2_pin : LED_GRZALKA3_pin));
        digitalWrite(pin, GRZALKA_OFF);
        digitalWrite(ledPin, LOW);
        switch(i) {
          case 0: Z.heater1_flag = false; break;
          case 1: Z.heater2_flag = false; break;
          case 2: Z.heater3_flag = false; break;
        }
      }
    }
    return;
  }
  
  // Sprawdź czy temperatura jest bezpieczna
  if (!isTemperatureSafe()) {
    bool anyHeaterOn = false;
    for (int i = 0; i < 3; i++) {
      if (heater_states[i]->state || heater_states[i]->waitingToTurnOff || heater_states[i]->waitingToTurnOn) {
        anyHeaterOn = true;
        break;
      }
    }
    
    if (anyHeaterOn) {
      LOG_ERROR_DEDUP("HeaterControl", "Temperatura %.1f°C niebezpieczna - wyłączam grzałki!", 
                      T.bojler.temperatura);
    }
    
    // Wyłącz wszystkie grzałki
    for (int i = 0; i < 3; i++) {
      if (heater_states[i]->state || heater_states[i]->waitingToTurnOff || heater_states[i]->waitingToTurnOn) {
        heater_states[i]->state = false;
        heater_states[i]->waitingToTurnOff = false;
        heater_states[i]->turnOffTime = 0;
        heater_states[i]->waitingToTurnOn = false;
        heater_states[i]->turnOnTime = 0;
        
        int pin = (i == 0 ? GRZALKA1_pin : (i == 1 ? GRZALKA2_pin : GRZALKA3_pin));
        int ledPin = (i == 0 ? LED_GRZALKA1_pin : (i == 1 ? LED_GRZALKA2_pin : LED_GRZALKA3_pin));
        digitalWrite(pin, GRZALKA_OFF);
        digitalWrite(ledPin, LOW);
        switch(i) {
          case 0: Z.heater1_flag = false; break;
          case 1: Z.heater2_flag = false; break;
          case 2: Z.heater3_flag = false; break;
        }
      }
    }
    return;
  }
  
  // NIEZALEŻNA LOGIKA DLA KAŻDEJ FAZY
  float voltages[3] = {modbusData.gridVoltage1, 
                       modbusData.gridVoltage2, 
                       modbusData.gridVoltage3};
  
  for (int i = 0; i < 3; i++) {
    float phaseVoltage = voltages[i];
    HeaterState* state = heater_states[i];
    
    // ===== SPRAWDŹ STAN GRZAŁKI =====
    
    if (!state->state) {
      // === GRZAŁKA WYŁĄCZONA ===
      
      if (state->waitingToTurnOn) {
        // Już czeka na załączenie - sprawdź czy anulować (napięcie spadło)
        if (!shouldTurnOn(phaseVoltage)) {
          cancelTurnOnTimer(i);
        }
        // Jeśli napięcie nadal wysokie - czekamy dalej
      } else {
        // Nie czeka - sprawdź czy rozpocząć odliczanie do załączenia
        if (shouldTurnOn(phaseVoltage)) {
          startTurnOnTimer(i);
        }
      }
      
    } else {
      // === GRZAŁKA ZAŁĄCZONA ===
      
      if (state->waitingToTurnOff) {
        // Już czeka na wyłączenie - sprawdź czy anulować (napięcie wzrosło)
        if (shouldCancelTurnOffTimer(phaseVoltage)) {
          cancelTurnOffTimer(i);
        }
      } else {
        // Nie czeka - sprawdź czy rozpocząć odliczanie do wyłączenia
        if (shouldStartTurnOffTimer(phaseVoltage)) {
          startTurnOffTimer(i);
        }
      }
    }
    
    // Aktualizuj stan (sprawdź czy minął czas timera)
    updateHeaterState(i);
  }
}

// ========== PUBLICZNE FUNKCJE USTAWIENIA ==========
void HeaterControl::setThresholds(float U_on, float U_off) {
  U.Ugrid_on = U_on;
  U.Ugrid_off = U_off;
  LOG_INFO("HeaterControl", "Konfiguracja: U_on=%.1fV, U_off=%.1fV", U_on, U_off);
}

void HeaterControl::setDelays(uint16_t delay_on_ms, uint16_t delay_off_ms) {
  U.HeaterDelay_on_ms = delay_on_ms;
  U.HeaterDelay_off_ms = delay_off_ms;
  LOG_INFO("HeaterControl", "Opóźnienia: ON=%dms, OFF=%dms", delay_on_ms, delay_off_ms);
}

void HeaterControl::enableSystem(bool enable) {
  U.HeaterEnabled = enable;
  LOG_INFO("HeaterControl", "System grzałek: %s", enable ? "AKTYWNY" : "NIEAKTYWNY");
}

bool HeaterControl::getHeaterState(int heaterIndex) {
  if (heaterIndex < 0 || heaterIndex > 2) return false;
  return heater_states[heaterIndex]->state;
}

int HeaterControl::getActiveHeatersCount() {
  int count = 0;
  for (int i = 0; i < 3; i++) {
    if (heater_states[i]->state) count++;
  }
  return count;
}

void HeaterControl::setBojlerTemperature(float temp) {
  T.bojler.temperatura = temp;
  T.temperatura_bojlera = (int8_t)temp;
  T.bojler.ok = (temp > -55 && temp < 125);
}

void HeaterControl::setModbusStatus(bool connected) {
  modbusData.connected = connected;
}

void HeaterControl::printStatus() {
  Serial.println("=== STATUS GRZAŁEK ===");
  Serial.printf("L1: %s (%.1fV) | L2: %s (%.1fV) | L3: %s (%.1fV)\n",
                heater_states[0]->state ? "ON " : 
                  (heater_states[0]->waitingToTurnOn ? "ON→" :
                   (heater_states[0]->waitingToTurnOff ? "OFF→" : "OFF")),
                modbusData.gridVoltage1,
                heater_states[1]->state ? "ON " : 
                  (heater_states[1]->waitingToTurnOn ? "ON→" :
                   (heater_states[1]->waitingToTurnOff ? "OFF→" : "OFF")),
                modbusData.gridVoltage2,
                heater_states[2]->state ? "ON " : 
                  (heater_states[2]->waitingToTurnOn ? "ON→" :
                   (heater_states[2]->waitingToTurnOff ? "OFF→" : "OFF")),
                modbusData.gridVoltage3);
  Serial.printf("🌡️ Temperatury: Bojler=%.1f°C, Radiator=%.1f°C | Modbus: %s | System: %s\n",
                T.bojler.temperatura, T.radiator.temperatura,
                modbusData.connected ? "OK" : "BRAK",
                U.HeaterEnabled ? "AKTYWNY" : "NIEAKTYWNY");
}