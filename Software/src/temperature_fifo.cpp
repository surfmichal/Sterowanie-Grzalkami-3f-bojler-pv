#include "temperature_fifo.h"
#include "globals.h"
#include <Arduino.h>

// ========== INICJALIZACJA BUFORA ==========
void initTemperatureFIFO() {
  tempFIFO.head = 0;
  tempFIFO.count = 0;
  tempFIFO.last_index = 0;
  memset(tempFIFO.values, 0, sizeof(tempFIFO.values));
  Serial.println("🌡️ Bufor temperatur FIFO zainicjalizowany (720 pomiarów)");
}

// ========== DODAJ NOWY POMIAR ==========
void addTemperatureReading(int8_t temp) {
  // Zapisz nową wartość
  tempFIFO.values[tempFIFO.head] = temp;
  
  // Przesuń head
  tempFIFO.head = (tempFIFO.head + 1) % MAX_TEMP_HISTORY;
  
  // Zwiększ licznik (maksymalnie MAX_TEMP_HISTORY)
  if (tempFIFO.count < MAX_TEMP_HISTORY) {
    tempFIFO.count++;
  }
  
  // DEBUG: co godzinę pokaż info
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 3600000) {  // co godzinę
    lastDebug = millis();
    Serial.printf("📊 FIFO: zapisano %d/%d pomiarów temperatury\n", 
                  tempFIFO.count, MAX_TEMP_HISTORY);
  }
}

// ========== POBIERZ TEMPERATURĘ Z HISTORII ==========
// index: 0 = najstarszy, count-1 = najnowszy
int8_t getTemperatureFromHistory(int index) {
  if (index >= tempFIFO.count) {
    return -127;  // błąd
  }
  
  // Oblicz rzeczywisty indeks w tablicy
  // Najstarszy pomiar jest na pozycji: (head - count + MAX_TEMP_HISTORY) % MAX_TEMP_HISTORY
  int realIndex = (tempFIFO.head - tempFIFO.count + index + MAX_TEMP_HISTORY) % MAX_TEMP_HISTORY;
  return tempFIFO.values[realIndex];
}

// ========== POBIERZ OSTATNI POMIAR ==========
int8_t getLastTemperature() {
  if (tempFIFO.count == 0) {
    return -127;
  }
  // Ostatni pomiar jest na pozycji head-1
  int lastIndex = (tempFIFO.head - 1 + MAX_TEMP_HISTORY) % MAX_TEMP_HISTORY;
  return tempFIFO.values[lastIndex];
}

// ========== POBIERZ CAŁĄ HISTORIĘ JAKO JSON ==========
String getTemperatureHistoryJSON() {
  String json = "{\"count\":" + String(tempFIFO.count) + ",\"values\":[";
  
  for (int i = 0; i < tempFIFO.count; i++) {
    if (i > 0) json += ",";
    json += String(getTemperatureFromHistory(i));
  }
  
  json += "]}";
  return json;
}

// ========== POBIERZ OSTATNIE N POMIARÓW ==========
String getLastNTemperaturesJSON(int n) {
  if (n > tempFIFO.count) n = tempFIFO.count;
  
  String json = "{\"count\":" + String(n) + ",\"values\":[";
  
  int start = tempFIFO.count - n;
  for (int i = start; i < tempFIFO.count; i++) {
    if (i > start) json += ",";
    json += String(getTemperatureFromHistory(i));
  }
  
  json += "]}";
  return json;
}

// ========== ODCZYT Z INDEKSU (dla strony WWW) ==========
// Zwraca JSON z zakresem [start, end] gdzie start i end to indeksy w historii
String getTemperatureRangeJSON(int startIndex, int endIndex) {
  if (startIndex < 0) startIndex = 0;
  if (endIndex >= tempFIFO.count) endIndex = tempFIFO.count - 1;
  if (startIndex > endIndex) return "{\"values\":[]}";
  
  String json = "{\"start\":" + String(startIndex) + 
                ",\"end\":" + String(endIndex) + 
                ",\"values\":[";
  
  for (int i = startIndex; i <= endIndex; i++) {
    if (i > startIndex) json += ",";
    json += String(getTemperatureFromHistory(i));
  }
  
  json += "]}";
  return json;
}

// ========== POBRZ PODSUMOWANIE (min, max, avg) ==========
String getTemperatureSummaryJSON() {
  if (tempFIFO.count == 0) {
    return "{\"min\":0,\"max\":0,\"avg\":0,\"count\":0}";
  }
  
  int8_t min = 127;
  int8_t max = -128;
  int32_t sum = 0;
  
  for (int i = 0; i < tempFIFO.count; i++) {
    int8_t temp = getTemperatureFromHistory(i);
    if (temp < min) min = temp;
    if (temp > max) max = temp;
    sum += temp;
  }
  
  float avg = (float)sum / tempFIFO.count;
  
  String json = "{\"min\":" + String(min) + 
                ",\"max\":" + String(max) + 
                ",\"avg\":" + String(avg, 1) + 
                ",\"count\":" + String(tempFIFO.count) + "}";
  return json;
}