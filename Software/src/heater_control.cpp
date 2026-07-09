#include "heater_control.h"
#include "globals.h"
#include <esp_task_wdt.h>
#include "statistics.h"
#include "logger.h"

extern StycznikState stycznik;
extern Zmienne Z;
extern InverterData inverterData;
extern DataSource activeDataSource; 

HeaterControl heaterControl;  // Globalna instancja obiektu HeaterControl

HeaterControl::HeaterControl() {
  heater_states[0] = &heater1_state;
  heater_states[1] = &heater2_state;
  heater_states[2] = &heater3_state;
}

void HeaterControl::begin() {
  pinMode(GRZALKA1_pin, OUTPUT);
  pinMode(GRZALKA2_pin, OUTPUT);
  pinMode(GRZALKA3_pin, OUTPUT);
  pinMode(LED_GRZALKA1_pin, OUTPUT);
  pinMode(LED_GRZALKA2_pin, OUTPUT);
  pinMode(LED_GRZALKA3_pin, OUTPUT);
  pinMode(STYCZNIK_PIN, OUTPUT);
  
  digitalWrite(GRZALKA1_pin, GRZALKA_OFF);
  digitalWrite(GRZALKA2_pin, GRZALKA_OFF);
  digitalWrite(GRZALKA3_pin, GRZALKA_OFF);
  digitalWrite(LED_GRZALKA1_pin, LED_OFF);
  digitalWrite(LED_GRZALKA2_pin, LED_OFF);
  digitalWrite(LED_GRZALKA3_pin, LED_OFF);
  digitalWrite(STYCZNIK_PIN, STYCZNIK_OFF);

  Z.heater1_flag = false;
  Z.heater2_flag = false; 
  Z.heater3_flag = false;
  
  stycznik.state = false;
  stycznik.requested = false;
  stycznik.waitingToTurnOn = false;
  stycznik.waitingToTurnOff = false;
  stycznik.lastChange = 0;
  
  Serial.println("HeaterControl zainicjalizowany");
  LOG_INFO("Main", "HeaterControl zainicjalizowany");
}

bool HeaterControl::isDataValid() {
  // Sprawdź czy dane są dostępne w zależności od aktywnego źródła
  switch (activeDataSource) {
    case SOURCE_MODBUS:
      return inverterData.connected;
    case SOURCE_HTTP:
      return inverterData.connected;
    case SOURCE_NONE:
    default:
      return false;
  }
}

bool HeaterControl::isModbusDataValid() {
  // Użyj nowej funkcji isDataValid()
  if (!isDataValid()) return false;
  
  if (inverterData.gridVoltage1 < 150 || inverterData.gridVoltage1 > 270) return false;
  if (inverterData.gridVoltage2 < 150 || inverterData.gridVoltage2 > 270) return false;
  if (inverterData.gridVoltage3 < 150 || inverterData.gridVoltage3 > 270) return false;
  
  return true;
}

bool HeaterControl::isTemperatureSafe() {
  // === BOJLER ===
  if (!T.bojler.ok) {
    LOG_ERROR_DEDUP("HeaterControl", "Czujnik bojlera nie działa - BLOKADA grzałek!");
    return false;
  }
  
  if (T.bojler.temperatura >= (float)U.bojlerTmax) {
    LOG_ERROR_DEDUP("HeaterControl", "Temperatura bojlera %.1f°C >= %.1f°C - BLOKADA!", 
                    T.bojler.temperatura, (float)U.bojlerTmax);
    return false;
  }
  
  // === RADIATOR ===
  if (U.radiatorT_critical) {
    if (T.radiator.ok) {
      if (T.radiator.temperatura >= (float)U.radiatorTmax) {
        LOG_ERROR_DEDUP("HeaterControl", "Temperatura radiatora %.1f°C >= %.1f°C - BLOKADA!", 
                        T.radiator.temperatura, (float)U.radiatorTmax);
        return false;
      }
      
      if (T.radiator.ok && T.radiator.temperatura >= (float)U.radiatorTmax - 5.0) {
        LOG_WARN_DEDUP("HeaterControl", "Ostrzeżenie: radiator %.1f°C (blisko limitu %.1f°C)", 
                      T.radiator.temperatura, (float)U.radiatorTmax);
      }
    } else {
      LOG_ERROR_DEDUP("HeaterControl", "Czujnik radiatora nie działa i jest KRYTYCZNY - BLOKADA!");
      return false;
    }
  }
  return true;
}

// Natychmiastowe załączenie gdy napięcie >= U_on
bool HeaterControl::shouldTurnOn(float voltage) {
  if (!isHeaterAllowed()) {
        return false;
    }

  if (!U.HeaterEnabled) return false;  
  if (!isModbusDataValid()) return false;
  if (!isTemperatureSafe()) return false;
  
  return (voltage >= U.Ugrid_on);
}

// Rozpocznij odliczanie do wyłączenia gdy napięcie <= U_off
bool HeaterControl::shouldStartTurnOffTimer(float voltage) {
  if (!isModbusDataValid()) return false;
  if (!isTemperatureSafe()) return true;  // Jeśli temperatura niebezpieczna - natychmiast wyłączamy
  
  return (voltage <= U.Ugrid_off);
}

// Sprawdź czy należy ANULOWAĆ odliczanie (napięcie wróciło powyżej U_off)
bool HeaterControl::shouldCancelTurnOffTimer(float voltage) {
  return (voltage > U.Ugrid_off);
}

void HeaterControl::startTurnOnTimer(int index) {
  HeaterState* state = heater_states[index];
  
  if (!state->state && !state->waitingToTurnOn) {
    state->waitingToTurnOn = true;
    state->turnOnTime = millis() + U.HeaterDelay_on_ms;
    
    const char* phaseName = "";
    float voltage = 0;
    
    switch(index) {
      case 0: phaseName = "L1"; voltage = inverterData.gridVoltage1; break;
      case 1: phaseName = "L2"; voltage = inverterData.gridVoltage2; break;
      case 2: phaseName = "L3"; voltage = inverterData.gridVoltage3; break;
    }
    
    Serial.printf("[%s] ⏳ Rozpoczęto odliczanie do ZAŁĄCZENIA (%dms, napięcie: %.1fV >= %.1fV)\n", 
                  phaseName, U.HeaterDelay_on_ms, voltage, U.Ugrid_on);
    LOG_INFO("HeaterControl", "[%s] Rozpoczęto odliczanie do ZAŁĄCZENIA (%dms)", 
             phaseName, U.HeaterDelay_on_ms);
  }
}

void HeaterControl::cancelTurnOnTimer(int index) {
  HeaterState* state = heater_states[index];
  
  if (state->waitingToTurnOn) {
    state->waitingToTurnOn = false;
    state->turnOnTime = 0;
    
    const char* phaseName = "";
    switch(index) {
      case 0: phaseName = "L1"; break;
      case 1: phaseName = "L2"; break;
      case 2: phaseName = "L3"; break;
    }
    
    Serial.printf("[%s] 🔄 Anulowano odliczanie do ZAŁĄCZENIA (napięcie spadło poniżej %.1fV)\n", 
                  phaseName, U.Ugrid_on);
  }
}

void HeaterControl::executeTurnOn(int index) {
  HeaterState* state = heater_states[index];
  
  if (state->waitingToTurnOn && !state->state) {
    if (millis() >= state->turnOnTime) {
      state->waitingToTurnOn = false;
      state->state = true;
      state->turnOnTime = 0;
      
      // Wywołaj turnOnNow dla faktycznego załączenia
      turnOnNow(index);
    }
  }
}

// Natychmiastowe załączenie grzałki
void HeaterControl::turnOnNow(int index) {
  HeaterState* state = heater_states[index];
  
  if (!state->state) {
    state->state = true;
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
        voltage = inverterData.gridVoltage1;
        break;
      case 1: 
        pin = GRZALKA2_pin; 
        ledPin = LED_GRZALKA2_pin; 
        phaseName = "L2";
        voltage = inverterData.gridVoltage2;
        break;
      case 2: 
        pin = GRZALKA3_pin; 
        ledPin = LED_GRZALKA3_pin; 
        phaseName = "L3";
        voltage = inverterData.gridVoltage3;
        break;
    }
    
    if (pin != -1) {
      digitalWrite(pin, GRZALKA_ON);
      digitalWrite(ledPin, LED_ON);      
      
      LOG_INFO_DEDUP("HeaterControl", "[%s] 🔥 GRZAŁKA ZAŁĄCZONA (napięcie: %.1fV, T: %.1f°C)", 
                     phaseName, voltage, T.bojler.temperatura);
      
      incrementHeaterCycles(index + 1);
    }
  }
}

// Rozpocznij odliczanie do wyłączenia
void HeaterControl::startTurnOffTimer(int index) {
  HeaterState* state = heater_states[index];
  
  if (state->state && !state->waitingToTurnOff) {
    state->waitingToTurnOff = true;
    state->turnOffTime = millis() + U.HeaterDelay_off_ms;
    
    const char* phaseName = "";
    float voltage = 0;
    
    switch(index) {
      case 0: phaseName = "L1"; voltage = inverterData.gridVoltage1; break;
      case 1: phaseName = "L2"; voltage = inverterData.gridVoltage2; break;
      case 2: phaseName = "L3"; voltage = inverterData.gridVoltage3; break;
    }
    
    Serial.printf("[%s] ⏱️ Rozpoczęto odliczanie do wyłączenia (%dms, napięcie: %.1fV <= %.1fV)\n", 
                  phaseName, U.HeaterDelay_off_ms, voltage, U.Ugrid_off);
    LOG_INFO("HeaterControl", "[%s] Rozpoczęto odliczanie do wyłączenia (%dms, napięcie: %.1fV <= %.1fV)", 
             phaseName, U.HeaterDelay_off_ms, voltage, U.Ugrid_off);
  }
}

// Anuluj odliczanie (napięcie wzrosło)
void HeaterControl::cancelTurnOffTimer(int index) {
  HeaterState* state = heater_states[index];
  
  if (state->waitingToTurnOff) {
    state->waitingToTurnOff = false;
    state->turnOffTime = 0;
    
    const char* phaseName = "";
    float voltage = 0;
    
    switch(index) {
      case 0: phaseName = "L1"; voltage = inverterData.gridVoltage1; break;
      case 1: phaseName = "L2"; voltage = inverterData.gridVoltage2; break;
      case 2: phaseName = "L3"; voltage = inverterData.gridVoltage3; break;
    }
    
    Serial.printf("[%s] 🔄 Anulowano odliczanie (napięcie wzrosło do %.1fV > %.1fV)\n", 
                  phaseName, voltage, U.Ugrid_off);
    LOG_INFO("HeaterControl", "[%s] Anulowano odliczanie (napięcie wzrosło do %.1fV > %.1fV)", 
             phaseName, voltage, U.Ugrid_off);
  }
}

// ========== FUNKCJE STEROWANIA STYCZNIKIEM ==========

bool HeaterControl::isAnyHeaterRequested() {
  for (int i = 0; i < 3; i++) {
    float voltage = (i == 0 ? inverterData.gridVoltage1 : 
                    (i == 1 ? inverterData.gridVoltage2 : inverterData.gridVoltage3));
    
    if (shouldTurnOn(voltage) || heater_states[i]->waitingToTurnOn) {
      return true;
    }
  }
  return false;
}

void HeaterControl::updateHeaterFlag(int index, bool state) {
  switch(index) {
    case 0: Z.heater1_flag = state; break;
    case 1: Z.heater2_flag = state; break;
    case 2: Z.heater3_flag = state; break;    
  }
}

void HeaterControl::turnOnContactor() {
  if (stycznik.state) return;
  
  digitalWrite(STYCZNIK_PIN, STYCZNIK_ON);
  stycznik.state = true;
  stycznik.lastChange = millis();
  stycznik.waitingToTurnOn = false;
  Serial.println("🔌 STYCZNIK ZAŁĄCZONY");
  LOG_INFO("HeaterControl", "STYCZNIK ZAŁĄCZONY");
}

void HeaterControl::turnOffContactor() {
  if (!stycznik.state) return;
  
  digitalWrite(STYCZNIK_PIN, STYCZNIK_OFF);
  stycznik.state = false;
  stycznik.lastChange = millis();
  stycznik.waitingToTurnOff = false;
  Serial.println("🔌 STYCZNIK WYŁĄCZONY");
  LOG_INFO("HeaterControl", "STYCZNIK WYŁĄCZONY");
}

bool HeaterControl::isAnyHeaterPhysicallyOn() {
  for (int i = 0; i < 3; i++) {
    if (heater_states[i]->state) return true;
  }
  return false;
}

void HeaterControl::updateContactor() {
  bool anyHeaterNeeded = isAnyHeaterRequested();
  bool anyHeaterOn = isAnyHeaterPhysicallyOn();
  unsigned long now = millis();
  
  if (anyHeaterNeeded) {
    stycznik.requested = true;
    stycznik.waitingToTurnOff = false;
    
    if (!stycznik.state && !stycznik.waitingToTurnOn) {
      stycznik.waitingToTurnOn = true;
      stycznik.lastChange = now;
      Serial.println("⏳ STYCZNIK: rozpoczynam odliczanie do załączenia");
    }
    
    if (stycznik.waitingToTurnOn && (now - stycznik.lastChange) >= STYCZNIK_DELAY_ON) {
      turnOnContactor();
    }
  } 
  else {
    stycznik.requested = false;
    stycznik.waitingToTurnOn = false;
    
    if (anyHeaterOn) {
      Serial.println("⚠️ STYCZNIK: Nie mogę wyłączyć - triaki są załączone!");
      stycznik.waitingToTurnOff = false;
      return;
    }
    
    if (stycznik.state && !stycznik.waitingToTurnOff) {
      stycznik.waitingToTurnOff = true;
      stycznik.lastChange = now;
      Serial.printf("⏳ STYCZNIK: rozpoczynam odliczanie do wyłączenia (%dms)\n", 
                    U.ContactorDelay_off_ms);
    }
    
    if (stycznik.waitingToTurnOff && (now - stycznik.lastChange) >= U.ContactorDelay_off_ms) {
      turnOffContactor();
    }
  }
}

// ========== FUNKCJA updateHeaterState ==========
void HeaterControl::updateHeaterState(int index) {
  HeaterState* state = heater_states[index];
  bool newState = state->state;
  
  if (newState == true && !stycznik.state && !stycznik.waitingToTurnOn) {
    Serial.printf("[%s] ⚠️ Nie mogę załączyć triaka - stycznik wyłączony!\n", 
                  index == 0 ? "L1" : (index == 1 ? "L2" : "L3"));
    return;
  }
  
  int pin = -1;
  int ledPin = -1;
  const char* phaseName = "";
  
  switch(index) {
    case 0: 
      pin = GRZALKA1_pin; 
      ledPin = LED_GRZALKA1_pin; 
      phaseName = "L1";
      break;
    case 1: 
      pin = GRZALKA2_pin; 
      ledPin = LED_GRZALKA2_pin; 
      phaseName = "L2";
      break;
    case 2: 
      pin = GRZALKA3_pin; 
      ledPin = LED_GRZALKA3_pin; 
      phaseName = "L3";
      break;
  }
  
  if (pin != -1) {
    byte currentPinState = digitalRead(pin);
    byte targetPinState = newState ? GRZALKA_ON : GRZALKA_OFF;
    
    if (currentPinState != targetPinState) {
      digitalWrite(pin, targetPinState);
      digitalWrite(ledPin, newState ? LED_ON : LED_OFF);
      updateHeaterFlag(index, newState);
      
      float voltage = (index == 0 ? inverterData.gridVoltage1 : 
                      (index == 1 ? inverterData.gridVoltage2 : inverterData.gridVoltage3));
      
      Serial.printf("[%s] %s (napięcie: %.1fV, stycznik: %s)\n", 
                    phaseName,
                    newState ? "ZAŁĄCZONA 🔥" : "WYŁĄCZONA ❄️",
                    voltage,
                    stycznik.state ? "ON" : "OFF");
      LOG_INFO("HeaterControl", "[%s] %s (napięcie: %.1fV, stycznik: %s)", 
               phaseName,
               newState ? "ZAŁĄCZONA 🔥" : "WYŁĄCZONA ❄️",
               voltage,
               stycznik.state ? "ON" : "OFF");
    }
  }
}

// ========== GŁÓWNA FUNKCJA update() ==========
void HeaterControl::update() {
 
  // ===== 1. SPRAWDŹ DANE =====
  if (!isModbusDataValid()) {
    bool wasAnythingOn = false;
    for (int i = 0; i < 3; i++) {
      if (heater_states[i]->state || heater_states[i]->waitingToTurnOff) {
        wasAnythingOn = true;
        heater_states[i]->state = false;
        heater_states[i]->waitingToTurnOff = false;
        heater_states[i]->turnOffTime = 0;
      }
    }
    if (wasAnythingOn) {
      Serial.println("⚠️ Brak danych - wyłączam wszystko!");
      digitalWrite(GRZALKA1_pin, GRZALKA_OFF);
      digitalWrite(GRZALKA2_pin, GRZALKA_OFF);
      digitalWrite(GRZALKA3_pin, GRZALKA_OFF);
      digitalWrite(LED_GRZALKA1_pin, LED_OFF);
      digitalWrite(LED_GRZALKA2_pin, LED_OFF);
      digitalWrite(LED_GRZALKA3_pin, LED_OFF);
      
      Z.heater1_flag = false;
      Z.heater2_flag = false;
      Z.heater3_flag = false;
      LOG_INFO("HeaterControl:Update", "Brak danych - wyłączam wszystko!!!");
    }
    if (stycznik.state) turnOffContactor();
    return;
  }
  
  // ===== 2. SPRAWDŹ TEMPERATURĘ =====
  if (!isTemperatureSafe()) {
    bool wasAnythingOn = false;
    for (int i = 0; i < 3; i++) {
      if (heater_states[i]->state || heater_states[i]->waitingToTurnOff) {
        wasAnythingOn = true;
        heater_states[i]->state = false;
        heater_states[i]->waitingToTurnOff = false;
        heater_states[i]->turnOffTime = 0;
      }
    }
    if (wasAnythingOn) {
      digitalWrite(GRZALKA1_pin, GRZALKA_OFF);
      digitalWrite(GRZALKA2_pin, GRZALKA_OFF);
      digitalWrite(GRZALKA3_pin, GRZALKA_OFF);
      digitalWrite(LED_GRZALKA1_pin, LED_OFF);
      digitalWrite(LED_GRZALKA2_pin, LED_OFF);
      digitalWrite(LED_GRZALKA3_pin, LED_OFF);
      
      Z.heater1_flag = false;
      Z.heater2_flag = false;
      Z.heater3_flag = false;
      LOG_INFO("HeaterControl:Update", "Temperatura niebezpieczna - wyłączam grzałki!!!");
    }
    if (stycznik.state) turnOffContactor();
    return;
  }
  
  // ===== 3. AKTUALIZUJ STAN STYCZNIKA =====
  updateContactor();
  
  // ===== 4. LOGIKA TRIAKÓW =====
  if (stycznik.state || stycznik.waitingToTurnOn) {
    float voltages[3] = {inverterData.gridVoltage1, 
                         inverterData.gridVoltage2, 
                         inverterData.gridVoltage3};
    
    for (int i = 0; i < 3; i++) {
      WDT_RESET();
      
      float phaseVoltage = voltages[i];
      HeaterState* state = heater_states[i];
      
      if (!state->state) {
        // Grzałka WYŁĄCZONA
        
        if (shouldTurnOn(phaseVoltage)) {
          if (state->waitingToTurnOn) {
            // Już czeka na załączenie - sprawdź czy nie anulować
            if (!shouldTurnOn(phaseVoltage)) {
              cancelTurnOnTimer(i);
            }
          } else {
            // Rozpocznij odliczanie do załączenia
            startTurnOnTimer(i);
          }
        } else {
          // Napięcie spadło poniżej progu - anuluj oczekiwanie
          if (state->waitingToTurnOn) {
            cancelTurnOnTimer(i);
          }
        }
        
        // Wykonaj załączenie jeśli czas minął
        executeTurnOn(i);
        
      } else {
        // Grzałka ZAŁĄCZONA - logika wyłączania
        if (state->waitingToTurnOff) {
          if (millis() >= state->turnOffTime) {
            state->state = false;
            state->waitingToTurnOff = false;
            state->turnOffTime = 0;
          } else if (shouldCancelTurnOffTimer(phaseVoltage)) {
            cancelTurnOffTimer(i);
          }
        } else {
          if (shouldStartTurnOffTimer(phaseVoltage)) {
            startTurnOffTimer(i);
          }
        }
      }
      
      updateHeaterState(i);
    }
  }
}

// ========== PUBLICZNE FUNKCJE USTAWIENIA ==========
void HeaterControl::setThresholds(float U_on, float U_off) {
  U.Ugrid_on = U_on;
  U.Ugrid_off = U_off;
  Serial.printf("⚙️ Konfiguracja grzałek: U_on=%.1fV, U_off=%.1fV\n", U_on, U_off);
}

void HeaterControl::setDelays(uint16_t Td_on_ms, uint16_t Td_off_ms) {
  U.HeaterDelay_on_ms = Td_on_ms;
  U.HeaterDelay_off_ms = Td_off_ms;
  Serial.printf("⚙️ Konfiguracja czasu opóźnień: delay_on=%dms, delay_off=%dms\n", Td_on_ms, Td_off_ms);
}

void HeaterControl::enableSystem(bool heaterEn){
  U.HeaterEnabled = heaterEn;
  Serial.printf("⚙️ Załączanie systemu grzałek: HeaterEnabled=%s\n", heaterEn ? "WŁĄCZONY" : "WYŁĄCZONY");
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

void HeaterControl::setDataStatus(bool connected) {
  // Ustaw odpowiednią flagę w zależności od aktywnego źródła
  switch (activeDataSource) {
    case SOURCE_MODBUS:
      inverterData.connected = connected;
      break;
    case SOURCE_HTTP:
      inverterData.connected = connected;
      break;
    default:
      break;
  }
}

void HeaterControl::printStatus() {
  Serial.println("=== STATUS GRZAŁEK ===");
  Serial.printf("L1: %s (%.1fV) | L2: %s (%.1fV) | L3: %s (%.1fV)\n",
                heater_states[0]->state ? "ON " : 
                  (heater_states[0]->waitingToTurnOff ? "Td " : "OFF"),
                inverterData.gridVoltage1,
                heater_states[1]->state ? "ON " : 
                  (heater_states[1]->waitingToTurnOff ? "Td " : "OFF"),
                inverterData.gridVoltage2,
                heater_states[2]->state ? "ON " : 
                  (heater_states[2]->waitingToTurnOff ? "Td " : "OFF"),
                inverterData.gridVoltage3);
  Serial.printf("🌡️ Temperatura: %.1f°C / %.1f°C | Źródło: %s | Modbus: %s | HTTP: %s\n",
                T.bojler.temperatura, U.bojlerTmax,
                activeDataSource == SOURCE_MODBUS ? "Modbus" : 
                  (activeDataSource == SOURCE_HTTP ? "HTTP" : "BRAK"),
                inverterData.connected ? "OK" : "BRAK");
}

// ========== AKTUALIZACJA SYSTEMU BLOKAD ==========
void HeaterControl::updateBlockFlags() {
    // Resetuj wszystkie flagi (oprócz manual_disable i heater_system_disabled)
    bool manualDisable = heaterBlocks.manual_disable;
    bool systemDisabled = heaterBlocks.heater_system_disabled;
    
    memset(&heaterBlocks, 0, sizeof(heaterBlocks));
    
    // Przywróć flagi ręczne
    heaterBlocks.manual_disable = manualDisable;
    heaterBlocks.heater_system_disabled = systemDisabled;
    
    // === BLOKADY KRYTYCZNE ===
    
    // 1. Brak danych z inwertera (Modbus lub HTTP)
    if (!inverterData.connected) {
        heaterBlocks.inverter_offline = true;
        Serial.println("🔴 Blokada: BRAK DANYCH Z INWERTERA");
    }
    
    // 2. Temperatura bojlera
    if (T.bojler.ok) {
        if (T.bojler.temperatura >= U.bojlerTmax) {
            heaterBlocks.temp_bojler_exceeded = true;
            Serial.printf("🔴 Blokada: TEMPERATURA BOJLERA %.1f°C >= MAX %d°C\n", 
                          T.bojler.temperatura, U.bojlerTmax);
        } else if (T.bojler.temperatura >= U.bojlerTmax - 5.0) {
            heaterBlocks.temp_bojler_warning = true;
            Serial.printf("🟡 Ostrzeżenie: Temperatura bojlera %.1f°C blisko max %d°C\n", 
                          T.bojler.temperatura, U.bojlerTmax);
        }
    } else {
        // Czujnik bojlera nie działa - blokada        
        heaterBlocks.temp_bojler_sensor_error = true;
        Serial.println("🔴 Blokada: CZUJNIK BOJLERA NIE DZIAŁA (krytyczny)");
    }
    
    // 3. Temperatura radiatora
    if (T.radiator.ok) {
        if (T.radiator.temperatura >= U.radiatorTmax) {
            heaterBlocks.temp_radiator_exceeded = true;
            Serial.printf("🔴 Blokada: TEMPERATURA RADIATORA %.1f°C >= MAX %d°C\n", 
                          T.radiator.temperatura, U.radiatorTmax);
        } else if (T.radiator.temperatura >= U.radiatorTmax - 5.0) {
            heaterBlocks.temp_radiator_warning = true;
            Serial.printf("🟡 Ostrzeżenie: Temperatura radiatora %.1f°C blisko max %d°C\n", 
                          T.radiator.temperatura, U.radiatorTmax);
        }
    } else if (U.radiatorT_critical) {
        // Czujnik radiatora nie działa i jest krytyczny
        heaterBlocks.radiator_sensor_error = true;
        heaterBlocks.temp_radiator_exceeded = true;
        Serial.println("🔴 Blokada: CZUJNIK RADIATORA NIE DZIAŁA (krytyczny)");
    }
    
    // 4. System grzania wyłączony w config
    if (!U.HeaterEnabled) {
        heaterBlocks.heater_system_disabled = true;
        Serial.println("🔴 Blokada: SYSTEM GRZANIA WYŁĄCZONY W CONFIG");
    }
    
    // 5. Moc falownika wynosi zero - blokujemy grzałki (jeśli flaga załaczona)
    if (U.ZeroPowerLock) {
        heaterBlocks.powerInverterIsZero = true;
        Serial.println("🔴 Blokada: MOC FALOWNIKA = 0");
    }


    // 6. Sprawdź czy jakakolwiek blokada jest aktywna
    heaterBlocks.any_blocked = false;
    heaterBlocks.active_blocks_count = 0;
    
    // Sprawdź wszystkie flagi blokad
    if (heaterBlocks.inverter_offline) { heaterBlocks.any_blocked = true; heaterBlocks.active_blocks_count++; }
    if (heaterBlocks.temp_bojler_exceeded) { heaterBlocks.any_blocked = true; heaterBlocks.active_blocks_count++; }
    if (heaterBlocks.temp_bojler_sensor_error) { heaterBlocks.any_blocked = true; heaterBlocks.active_blocks_count++; }
    if (heaterBlocks.temp_radiator_exceeded) { heaterBlocks.any_blocked = true; heaterBlocks.active_blocks_count++; }
    if (heaterBlocks.radiator_sensor_error) { heaterBlocks.any_blocked = true; heaterBlocks.active_blocks_count++; }
    if (heaterBlocks.powerInverterIsZero) { heaterBlocks.any_blocked = true; heaterBlocks.active_blocks_count++; }
    if (heaterBlocks.manual_disable) { heaterBlocks.any_blocked = true; heaterBlocks.active_blocks_count++; }
    if (heaterBlocks.heater_system_disabled) { heaterBlocks.any_blocked = true; heaterBlocks.active_blocks_count++; }
    
    heaterBlocks.last_update = millis();
    
    // Podsumowanie
    if (heaterBlocks.any_blocked) {
        Serial.printf("🔒 System BLOKOWANY (%d blokad)\n", heaterBlocks.active_blocks_count);
    } else {
        Serial.println("✅ System ODBLOKOWANY - grzałki mogą działać");
    }
}

// ========== SPRAWDŹ CZY GRZAŁKI MOGĄ SIĘ ZAŁĄCZYĆ ==========
bool HeaterControl::isHeaterAllowed() {
    // Aktualizuj blokady
    updateBlockFlags();
    
    // Jeśli jakakolwiek blokada jest aktywna, grzałki nie mogą się załączyć
    return !heaterBlocks.any_blocked;
}

// ========== POBIERZ TEKSTOWY OPIS BLOKAD ==========
String HeaterControl::getBlockReason() {
    String reason = "";
    
    if (heaterBlocks.inverter_offline) reason += "❌ Brak danych z inwertera; ";
    if (heaterBlocks.temp_bojler_exceeded) reason += "❌ Temp. bojlera za wysoka; ";
    if (heaterBlocks.temp_radiator_exceeded) reason += "❌ Temp. radiatora za wysoka; ";
    if (heaterBlocks.radiator_sensor_error) reason += "❌ Czujnik radiatora nie działa; ";
    if (heaterBlocks.powerInverterIsZero) reason += "❌ Moc falownika = 0; ";
    if (heaterBlocks.manual_disable) reason += "❌ Ręczne wyłączenie; ";
    if (heaterBlocks.heater_system_disabled) reason += "❌ System wyłączony w config; ";
    
    if (reason.length() == 0) reason = "✅ Brak blokad";
    
    return reason;
}

 
