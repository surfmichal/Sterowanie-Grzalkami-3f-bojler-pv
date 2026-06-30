#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <Arduino.h>
#include "globals.h"

class DataManager {
private:
  DataSource currentSource;
  bool dataValid;
  unsigned long lastUpdate;
  
public:
  DataManager();
  
  // Inicjalizacja na podstawie activeDataSource
  void begin();
  void stopCurrentSource();
  
  // Pobierz dane z aktywnego źródła
  bool fetchData();
  
  // Sprawdź czy dane są aktualne
  bool isDataValid();
  
  // Pobierz aktualne źródło
  DataSource getSource() { return currentSource; }
  
  // Zmień źródło (np. z poziomu strony WWW)
  bool setSource(DataSource newSource);
  
  // Pobierz opis źródła
  String getSourceName();
};

#endif