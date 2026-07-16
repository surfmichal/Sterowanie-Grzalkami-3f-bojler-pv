#ifndef STATISTICS_H
#define STATISTICS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "globals.h"

extern LicznikiCzasu liczniki;

// Inicjalizacja (wczytaj total z LittleFS)
bool loadStatistics();

// Zapis dziennych danych do total (wywoływane raz dziennie)
bool saveDailyStatistics();

// Dodaj czas pracy (co sekundę, tylko w RAM)
void updateHeaterRuntime();

// Dodaj załączenie (tylko w RAM)
void incrementHeaterCycles(int heaterNum);

// Reset dziennych liczników (po zapisie)
void resetDailyCounters();

// Sprawdź czy trzeba zapisać (wywoływane w pętli)
void checkAndSaveDaily();

// Pobierz statystyki w formacie JSON (dla strony WWW)
String getStatisticsJSON();

void addHeaterRuntimeSeconds(int heaterIndex, uint32_t seconds);

#endif