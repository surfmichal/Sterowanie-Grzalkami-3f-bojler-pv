#ifndef HEATER_CONTROL_H
#define HEATER_CONTROL_H

#include <Arduino.h>
#include "globals.h"

class HeaterControl {
private:
  HeaterState* heater_states[3];
  
  bool isTemperatureSafe();       // Sprawdź czy temperatura bojlera i radiatora jest bezpieczna
  bool isDataValid();             // Sprawdź czy dane są dostępne w zależności od aktywnego źródła
  bool isModbusDataValid();       // Sprawdź czy dane Modbus są poprawne (napięcia w zakresie)
  
  
  bool shouldTurnOn(float voltage);                   // Sprawdź czy załączyć
  bool shouldStartTurnOffTimer(float voltage);        // Sprawdź czy wyłączyć
  bool shouldCancelTurnOffTimer(float voltage);       // Sprawdź czy anulować wyłączenie
   
  void executeTurnOn(int index);                      // Wykonaj załączenie jeśli czas minął
  
  bool isAnyHeaterPhysicallyOn();                     // Sprawdź czy którykolwiek triak jest fizycznie załączony
  bool isAnyHeaterRequested();                        // Sprawdź czy którykolwiek triak powinien być załączony (napięcie >= U_on)
  
  void turnOnNow(int index);                          // Natychmiastowe załączenie
  void turnOffNow(int index);                         // Natychmiastowe wyłączenie
  
  void updateHeaterState(int index);                  // Aktualizacja stanu (sprawdza timery)
  void updateHeaterFlag(int index, bool state);       // Aktualizacja flagi w strukturze Z
  
  void turnOnContactor();                             // Załącz stycznik
  void turnOffContactor();                            // Wyłącz stycznik
  void updateContactor();                             // Aktualizacja stanu stycznika (odpowiednio do żądań grzałek)
    
  void startTurnOffTimer(int index);                  // Rozpocznij odliczanie do wyłączenia
  void cancelTurnOffTimer(int index);                 // Anuluj odliczanie do wyłączenia
  
  void startTurnOnTimer(int index);                   // Rozpocznij odliczanie do załączenia
  void cancelTurnOnTimer(int index);                  // Anuluj odliczanie do załączenia
  
  

public:
  HeaterControl();
  void begin();                                       // Inicjalizacja (przypisanie wskaźników do struktur)
  void update();                                      // Główna funkcja sterowania (wywoływana co 1 sekundę)
    
  void setThresholds(float U_on, float U_off);        // Ustaw progi napięć włączania i wyłączania
  void setDelays(uint16_t delay_on_ms, uint16_t delay_off_ms);  // Ustaw opóźnienia włączania i wyłączania grzałek
  void enableSystem(bool enable);                      // Włącz lub wyłącz system grzałek (true=aktywne, false=nieaktywne)
  
  bool getHeaterState(int heaterIndex);               // Pobierz stan grzałki (true=załączona, false=wyłączona)
  int getActiveHeatersCount();                        // Pobierz liczbę aktywnych grzałek (0-3)
  
  void setDataStatus(bool connected);                 // Ustaw status połączenia danych (true=połączony, false=rozłączony)
  void printStatus();                                 // Wypisz aktualny status grzałek i stycznika na Serial
  bool isHeaterAllowed();                             // Sprawdź czy grzałki mogą być włączone (czy nie ma blokad)
  void updateBlockFlags();                            // Aktualizacja flag blokad w strukturze heaterBlocks

  String getBlockReason();                            // Pobierz powód blokady grzałek w formie tekstowej
  HeaterBlockFlags& getBlockFlags() { return heaterBlocks; }                                  
};

// Globalna instancja (deklaracja)
extern HeaterControl heaterControl;

// Funkcja do uzyskania wskaźnika (opcjonalnie)
HeaterControl* getHeaterControl();

#endif