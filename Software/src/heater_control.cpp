#include "heater_control.h"
#include "globals.h"
#include <esp_task_wdt.h>
#include "statistics.h"
#include "logger.h"

extern StycznikState stycznik;
extern Zmienne Z;

extern ModbusData modbusData;

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
  
  // Stan początkowy: WSZYSTKO WYŁĄCZONE (HIGH dla odwróconej logiki)
  digitalWrite(GRZALKA1_pin, GRZALKA_OFF);
  digitalWrite(GRZALKA2_pin, GRZALKA_OFF);
  digitalWrite(GRZALKA3_pin, GRZALKA_OFF);
  digitalWrite(LED_GRZALKA1_pin, OFF);
  digitalWrite(LED_GRZALKA2_pin, OFF);
  digitalWrite(LED_GRZALKA3_pin, OFF);
  digitalWrite(STYCZNIK_PIN, STYCZNIK_OFF);  // HIGH = stycznik wyłączony

  Z.heater1_flag = false;  // Aktualizacja flag dla strony WWW
  Z.heater2_flag = false; 
  Z.heater3_flag = false;
  
  // Inicjalizacja stanu stycznika
  stycznik.state = false;
  stycznik.requested = false;
  stycznik.waitingToTurnOn = false;
  stycznik.waitingToTurnOff = false;
  stycznik.lastChange = 0;
  Serial.println("HeaterControl zainicjalizowany");
  LOG_INFO("Main", "HeaterControl zainicjalizowany");
}

bool HeaterControl::isModbusDataValid() {
  if (!modbusData.connected) return false;
  
  if (modbusData.gridVoltage1 < 150 || modbusData.gridVoltage1 > 270) return false;
  if (modbusData.gridVoltage2 < 150 || modbusData.gridVoltage2 > 270) return false;
  if (modbusData.gridVoltage3 < 150 || modbusData.gridVoltage3 > 270) return false;
  
  return true;
}

bool HeaterControl::isTemperatureSafe() {
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

// Natychmiastowe załączenie gdy napięcie >= U_on
bool HeaterControl::shouldTurnOn(float voltage) {
  // 🔥 Główny wyłącznik systemu grzania
  if (!U.HeaterEnabled) return false;
  
  // Zabezpieczenia
  if (!isModbusDataValid()) return false;
  if (!isTemperatureSafe()) return false;
  
  // Regulacja
  return (voltage >= U.Ugrid_on);
}

// Rozpocznij odliczanie do wyłączenia gdy napięcie <= U_off
bool HeaterControl::shouldStartTurnOffTimer(float voltage) {
  //if (!heater_config.enabled) return false;
  if (!isModbusDataValid()) return false;
  if (!isTemperatureSafe()) return true;  // Jeśli temperatura niebezpieczna - chcemy wyłączyć
  
  return (voltage <= U.Ugrid_off);
}

// Sprawdź czy należy ANULOWAĆ odliczanie (napięcie wróciło powyżej U_off)
bool HeaterControl::shouldCancelTurnOffTimer(float voltage) {
  return (voltage > U.Ugrid_off);
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
        voltage = modbusData.gridVoltage1;
        break;
      case 1: 
        pin = GRZALKA2_pin; 
        ledPin = LED_GRZALKA2_pin; 
        phaseName = "L2";
        voltage = modbusData.gridVoltage2;
        break;
      case 2: 
        pin = GRZALKA3_pin; 
        ledPin = LED_GRZALKA3_pin; 
        phaseName = "L3";
        voltage = modbusData.gridVoltage3;
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

// Rozpocznij odliczanie do wyłączenia
void HeaterControl::startTurnOffTimer(int index) {
  HeaterState* state = heater_states[index];
  
  if (state->state && !state->waitingToTurnOff) {
    state->waitingToTurnOff = true;
    state->turnOffTime = millis() + U.HeaterDelay_off_ms;  // Użyj poprawnej nazwy pola
    
    const char* phaseName = "";
    float voltage = 0;
    
    switch(index) {
      case 0: phaseName = "L1"; voltage = modbusData.gridVoltage1; break;
      case 1: phaseName = "L2"; voltage = modbusData.gridVoltage2; break;
      case 2: phaseName = "L3"; voltage = modbusData.gridVoltage3; break;
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
      case 0: phaseName = "L1"; voltage = modbusData.gridVoltage1; break;
      case 1: phaseName = "L2"; voltage = modbusData.gridVoltage2; break;
      case 2: phaseName = "L3"; voltage = modbusData.gridVoltage3; break;
    }
    
    Serial.printf("[%s] 🔄 Anulowano odliczanie (napięcie wzrosło do %.1fV > %.1fV)\n", 
                  phaseName, voltage, U.Ugrid_off);
    LOG_INFO("HeaterControl", "[%s] Anulowano odliczanie (napięcie wzrosło do %.1fV > %.1fV)", 
             phaseName, voltage, U.Ugrid_off);
  }
}

// ========== FUNKCJE STEROWANIA STYCZNIKIEM ==========

// Sprawdza czy jakikolwiek triak ma być załączony
bool HeaterControl::isAnyHeaterRequested() {
  for (int i = 0; i < 3; i++) {
    float voltage = (i == 0 ? modbusData.gridVoltage1 : 
                     (i == 1 ? modbusData.gridVoltage2 : modbusData.gridVoltage3));
    if (shouldTurnOn(voltage)) {
      return true;
    }
  }
  return false;
}

// Aktualizacja flagi stanu grzałki w strukturze Z
void HeaterControl::updateHeaterFlag(int index, bool state) {
  switch(index) {
    case 0: Z.heater1_flag = state; break;
    case 1: Z.heater2_flag = state; break;
    case 2: Z.heater3_flag = state; break;    
  }
}

// Załącz stycznik
void HeaterControl::turnOnContactor() {
  if (stycznik.state) return;  // już załączony
  
  digitalWrite(STYCZNIK_PIN, STYCZNIK_ON);
  stycznik.state = true;
  stycznik.lastChange = millis();
  stycznik.waitingToTurnOn = false;
  Serial.println("🔌 STYCZNIK ZAŁĄCZONY");
  LOG_INFO("HeaterControl", "STYCZNIK ZAŁĄCZONY");
}

// Wyłącz stycznik
void HeaterControl::turnOffContactor() {
  if (!stycznik.state) return;  // już wyłączony
  
  digitalWrite(STYCZNIK_PIN, STYCZNIK_OFF);
  stycznik.state = false;
  stycznik.lastChange = millis();
  stycznik.waitingToTurnOff = false;
  Serial.println("🔌 STYCZNIK WYŁĄCZONY");
  LOG_INFO("HeaterControl", "STYCZNIK WYŁĄCZONY");
}

// Aktualizacja stanu stycznika (wywoływana co 1 sekundę)
void HeaterControl::updateContactor() {
  bool anyHeaterNeeded = isAnyHeaterRequested();
  unsigned long now = millis();
  
  if (anyHeaterNeeded) {
    // Potrzebujemy załączyć triaki
    stycznik.requested = true;
    
    if (!stycznik.state && !stycznik.waitingToTurnOn) {
      // Stycznik wyłączony - rozpocznij procedurę załączania
      stycznik.waitingToTurnOn = true;
      stycznik.lastChange = now;
      Serial.println("⏳ STYCZNIK: rozpoczynam odliczanie do załączenia");
      LOG_INFO("HeaterControl", "Stycznik: rozpoczynam odliczanie do załączenia");
    }
    
    // Jeśli czekamy na załączenie i minął czas
    if (stycznik.waitingToTurnOn && (now - stycznik.lastChange) >= STYCZNIK_DELAY_ON) {
      turnOnContactor();
    }
  } else {
    // Nie potrzeba żadnych triaków
    stycznik.requested = false;
    
    if (stycznik.state && !stycznik.waitingToTurnOff) {
      // Stycznik załączony, ale nie potrzeba triaków - rozpocznij wyłączanie
      stycznik.waitingToTurnOff = true;
      stycznik.lastChange = now;
      Serial.println("⏳ STYCZNIK: rozpoczynam odliczanie do wyłączenia");
      LOG_INFO("HeaterControl", "STYCZNIK: rozpoczynam odliczanie do wyłączenia");
    }
    
    // Jeśli czekamy na wyłączenie i minął czas
    if (stycznik.waitingToTurnOff && (now - stycznik.lastChange) >= STYCZNIK_DELAY_OFF) {
      turnOffContactor();

    }
  }
}

// ========== FUNKCJA updateHeaterState ==========
void HeaterControl::updateHeaterState(int index) {
  HeaterState* state = heater_states[index];
  bool newState = state->state;
  
  // Jeśli stan się nie zmienił, nic nie robimy
  // (to sprawdzenie jest już w update(), ale dla bezpieczeństwa)
  // if (state->state == newState) return;  // nie potrzebne
  
  // Sprawdź czy stycznik jest załączony (przy załączaniu)
  if (newState == true && !stycznik.state && !stycznik.waitingToTurnOn) {
    Serial.printf("[%s] ⚠️ Nie mogę załączyć triaka - stycznik wyłączony!\n", 
                  index == 0 ? "L1" : (index == 1 ? "L2" : "L3"));
    LOG_INFO("HeaterControl", "[%s] Nie mogę załączyć triaka - stycznik wyłączony!", 
             index == 0 ? "L1" : (index == 1 ? "L2" : "L3"));
    return;
  }
  
  // NATYCHMIASTOWA ZMIANA STANU (bez opóźnienia)
  // state->state już jest ustawione przez turnOnNow/startTurnOffTimer
  
  int pin = -1;
  int ledPin = -1;
  const char* phaseName = "";
  
  switch(index) {
    case 0: 
      pin = GRZALKA1_pin; 
      ledPin = LED_GRZALKA1_pin; 
      phaseName = "L1";
      updateHeaterFlag(index, newState);
      break;
    case 1: 
      pin = GRZALKA2_pin; 
      ledPin = LED_GRZALKA2_pin; 
      phaseName = "L2";
      updateHeaterFlag(index, newState);
      break;
    case 2: 
      pin = GRZALKA3_pin; 
      ledPin = LED_GRZALKA3_pin; 
      phaseName = "L3";
      updateHeaterFlag(index, newState);
      break;
  }
  
  if (pin != -1) {
    digitalWrite(pin, newState ? GRZALKA_ON : GRZALKA_OFF);
    digitalWrite(ledPin, newState ? HIGH : LOW);
    
    float voltage = (index == 0 ? modbusData.gridVoltage1 : 
                    (index == 1 ? modbusData.gridVoltage2 : modbusData.gridVoltage3));
    
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
// ========== ZMIENIONA FUNKCJA update() ==========
void HeaterControl::update() {
  // ===== 1. SPRAWDŹ DANE MODBUS =====
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
      Serial.println("⚠️ Brak danych Modbus - wyłączam wszystko!");
      digitalWrite(GRZALKA1_pin, GRZALKA_OFF);
      digitalWrite(GRZALKA2_pin, GRZALKA_OFF);
      digitalWrite(GRZALKA3_pin, GRZALKA_OFF);
      digitalWrite(LED_GRZALKA1_pin, LOW);
      digitalWrite(LED_GRZALKA2_pin, LOW);
      digitalWrite(LED_GRZALKA3_pin, LOW);
      
      // Aktualizuj flagi dla strony WWW
      Z.heater1_flag = false;
      Z.heater2_flag = false;
      Z.heater3_flag = false;
      LOG_INFO("HeaterControl:Update", "Brak danych Modbus - wyłączam wszystko!!!");
    }
    if (stycznik.state) turnOffContactor();
    LOG_INFO("HeaterControl:Update", "Brak danych Modbus - wyłączam styznik!!!");
    return;
  }
  
  // ===== 2. SPRAWDŹ TEMPERATURĘ =====
  if (!isTemperatureSafe()) {
    bool wasAnythingOn = false;
    for (int i = 0; i < 3; i++) {
      if (heater_states[i]->state || heater_states[i]->waitingToTurnOff) {
        wasAnythingOn = true;
        Serial.printf("⚠️ Temperatura %.1f°C niebezpieczna - wyłączam grzałkę %d!\n", 
                      Z.T_current, i+1);
        heater_states[i]->state = false;
        heater_states[i]->waitingToTurnOff = false;
        heater_states[i]->turnOffTime = 0;
      }
    }
    if (wasAnythingOn) {
      digitalWrite(GRZALKA1_pin, GRZALKA_OFF);
      digitalWrite(GRZALKA2_pin, GRZALKA_OFF);
      digitalWrite(GRZALKA3_pin, GRZALKA_OFF);
      digitalWrite(LED_GRZALKA1_pin, LOW);
      digitalWrite(LED_GRZALKA2_pin, LOW);
      digitalWrite(LED_GRZALKA3_pin, LOW);
      
      Z.heater1_flag = false;
      Z.heater2_flag = false;
      Z.heater3_flag = false;
      LOG_INFO("HeaterControl:Update", "Temperatura %.1f°C niebezpieczna - wyłączam grzałki!!!", Z.T_current);
    }
    if (stycznik.state) turnOffContactor();
    LOG_INFO("HeaterControl:Update", "Temperatura %.1f°C niebezpieczna - wyłączam styznik!!!", Z.T_current);
    return;
  }
  
  // ===== 3. AKTUALIZUJ STAN STYCZNIKA =====
  updateContactor();
  
  // ===== 4. LOGIKA TRIAKÓW =====
  if (stycznik.state || stycznik.waitingToTurnOn) {
    float voltages[3] = {modbusData.gridVoltage1, 
                         modbusData.gridVoltage2, 
                         modbusData.gridVoltage3};
    
    for (int i = 0; i < 3; i++) {
      WDT_RESET();  // 🔥 kopnij watchdoga przy każdej fazie (ważne!)
      
      float phaseVoltage = voltages[i];
      HeaterState* state = heater_states[i];
      
      if (!state->state) {
        // Grzałka wyłączona - sprawdź czy załączyć
        if (shouldTurnOn(phaseVoltage)) {
          turnOnNow(i);
          incrementHeaterCycles(i + 1);  // zlicz załączenie
        }
      } else {
        // Grzałka załączona - sprawdź czy wyłączyć
        if (state->waitingToTurnOff) {
          if (shouldCancelTurnOffTimer(phaseVoltage)) {
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
  } else {
    // Stycznik wyłączony - upewnij się że triaki są wyłączone
    bool wasAnythingOn = false;
    for (int i = 0; i < 3; i++) {
      if (heater_states[i]->state) {
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
      digitalWrite(LED_GRZALKA1_pin, LOW);
      digitalWrite(LED_GRZALKA2_pin, LOW);
      digitalWrite(LED_GRZALKA3_pin, LOW);
      
      Z.heater1_flag = false;
      Z.heater2_flag = false;
      Z.heater3_flag = false;
      LOG_INFO("HeaterControl:Update", "Stycznik wyłączony - wyłączam wszystkie triaki!");

    }
  }
}

// ========== PUBLICZNE FUNKCJE USTAWIENIA ==========
void HeaterControl::setThresholds(float U_on, float U_off) {
  U.Ugrid_on = U_on;
  U.Ugrid_off = U_off;
  Serial.printf("⚙️ Konfiguracja grzałek: U_on=%.1fV, U_off=%.1fV\n", U_on, U_off);
}

void HeaterControl::setTurnOffDelay(uint16_t Td_ms) {
  U.HeaterDelay_off_ms = Td_ms;  // Użyj poprawnej nazwy pola
  Serial.printf("⚙️ Opóźnienie wyłączenia: %dms\n", Td_ms);
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
  Z.T_current = temp;
  Z.T_sensor_ok = (temp > -55 && temp < 125);
}

void HeaterControl::setModbusStatus(bool connected) {
  modbusData.connected = connected;
}

void HeaterControl::printStatus() {
  Serial.println("=== STATUS GRZAŁEK ===");
  Serial.printf("L1: %s (%.1fV) | L2: %s (%.1fV) | L3: %s (%.1fV)\n",
                heater_states[0]->state ? "ON " : 
                  (heater_states[0]->waitingToTurnOff ? "Td " : "OFF"),
                modbusData.gridVoltage1,
                heater_states[1]->state ? "ON " : 
                  (heater_states[1]->waitingToTurnOff ? "Td " : "OFF"),
                modbusData.gridVoltage2,
                heater_states[2]->state ? "ON " : 
                  (heater_states[2]->waitingToTurnOff ? "Td " : "OFF"),
                modbusData.gridVoltage3);
  Serial.printf("🌡️ Temperatura: %.1f°C / %.1f°C | Modbus: %s | System: %s\n",
                Z.T_current, U.bojlerTmax,
                modbusData.connected ? "OK" : "BRAK");
                //heater_config.enabled ? "AKTYWNY" : "NIEAKTYWNY");
}