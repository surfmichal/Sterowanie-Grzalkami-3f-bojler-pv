#include "temperature_fifo.h"
#include "globals.h"
#include <Arduino.h>

extern Ustawienia U;   // dostęp do U.temperatureLogInterval

// ========== INICJALIZACJA BUFORA ==========
void initTemperatureFIFO() {
  tempFIFO.head = 0;
  tempFIFO.count = 0;
  tempFIFO.last_index = 0;
  memset(tempFIFO.values_boj, 0, sizeof(tempFIFO.values_boj));
  memset(tempFIFO.values_rad, 0, sizeof(tempFIFO.values_rad));
  Serial.println("🌡️ Bufor temperatur FIFO zainicjalizowany (720 pomiarów)");
}

// ========== DODAJ NOWY POMIAR (bojler + radiator) ==========
void addTemperatureReading(int8_t tempBojler, int8_t tempRadiator) {
  tempFIFO.values_boj[tempFIFO.head] = tempBojler;
  tempFIFO.values_rad[tempFIFO.head] = tempRadiator;
  
  tempFIFO.head = (tempFIFO.head + 1) % MAX_TEMP_HISTORY;
  
  if (tempFIFO.count < MAX_TEMP_HISTORY) {
    tempFIFO.count++;
  }
  
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 3600000) {
    lastDebug = millis();
    Serial.printf("📊 FIFO: zapisano %d/%d pomiarów temperatury\n", 
                  tempFIFO.count, MAX_TEMP_HISTORY);
  }
}

// ========== POBIERZ TEMPERATURĘ Z HISTORII (bojler lub radiator) ==========
int8_t getTemperatureFromHistory(int index, TempSensor sensor) {
  if (index >= tempFIFO.count) {
    return -127;
  }
  int realIndex = (tempFIFO.head - tempFIFO.count + index + MAX_TEMP_HISTORY) % MAX_TEMP_HISTORY;
  return (sensor == SENSOR_RADIATOR) ? tempFIFO.values_rad[realIndex] : tempFIFO.values_boj[realIndex];
}

// ========== POBIERZ OSTATNI POMIAR (bojler lub radiator) ==========
int8_t getLastTemperature(TempSensor sensor) {
  if (tempFIFO.count == 0) {
    return -127;
  }
  int lastIndex = (tempFIFO.head - 1 + MAX_TEMP_HISTORY) % MAX_TEMP_HISTORY;
  return (sensor == SENSOR_RADIATOR) ? tempFIFO.values_rad[lastIndex] : tempFIFO.values_boj[lastIndex];
}

// ========== POBIERZ CAŁĄ HISTORIĘ JAKO JSON (bojler + radiator + interwał) ==========
String getTemperatureHistoryJSON() {
  uint16_t intervalSec = U.temperatureLogInterval;   // ⬅️ konwersja ms -> s

  String json = "{\"count\":" + String(tempFIFO.count) + 
                ",\"interval_sec\":" + String(intervalSec) + ",";
  
  json += "\"values_bojler\":[";
  for (int i = 0; i < tempFIFO.count; i++) {
    if (i > 0) json += ",";
    json += String(getTemperatureFromHistory(i));
  }
  json += "],\"values_radiator\":[";
  for (int i = 0; i < tempFIFO.count; i++) {
    if (i > 0) json += ",";
    json += String(getTemperatureFromHistory(i, SENSOR_RADIATOR));
  }
  json += "]}";
  
  return json;
}

// ========== POBIERZ OSTATNIE N POMIARÓW (bojler + radiator) ==========
String getLastNTemperaturesJSON(int n) {
  if (n > tempFIFO.count) n = tempFIFO.count;
  
  uint16_t intervalSec = U.temperatureLogInterval;

  String json = "{\"count\":" + String(n) + 
                ",\"interval_sec\":" + String(intervalSec) + ",";
  
  int start = tempFIFO.count - n;
  
  json += "\"values_bojler\":[";
  for (int i = start; i < tempFIFO.count; i++) {
    if (i > start) json += ",";
    json += String(getTemperatureFromHistory(i));
  }
  json += "],\"values_radiator\":[";
  for (int i = start; i < tempFIFO.count; i++) {
    if (i > start) json += ",";
    json += String(getTemperatureFromHistory(i, SENSOR_RADIATOR));
  }
  json += "]}";
  
  return json;
}

// ========== ODCZYT Z ZAKRESU (bojler + radiator) ==========
String getTemperatureRangeJSON(int startIndex, int endIndex) {
  if (startIndex < 0) startIndex = 0;
  if (endIndex >= tempFIFO.count) endIndex = tempFIFO.count - 1;
  if (startIndex > endIndex) return "{\"values_bojler\":[],\"values_radiator\":[]}";
  
  uint16_t intervalSec = U.temperatureLogInterval;

  String json = "{\"start\":" + String(startIndex) + 
                ",\"end\":" + String(endIndex) + 
                ",\"interval_sec\":" + String(intervalSec) + ",";
  
  json += "\"values_bojler\":[";
  for (int i = startIndex; i <= endIndex; i++) {
    if (i > startIndex) json += ",";
    json += String(getTemperatureFromHistory(i));
  }
  json += "],\"values_radiator\":[";
  for (int i = startIndex; i <= endIndex; i++) {
    if (i > startIndex) json += ",";
    json += String(getTemperatureFromHistory(i, SENSOR_RADIATOR));
  }
  json += "]}";
  
  return json;
}

// ========== PODSUMOWANIE (min, max, avg) DLA OBU CZUJNIKÓW ==========
String getTemperatureSummaryJSON() {
  if (tempFIFO.count == 0) {
    return "{\"bojler\":{\"min\":0,\"max\":0,\"avg\":0},\"radiator\":{\"min\":0,\"max\":0,\"avg\":0},\"count\":0}";
  }
  
  int8_t minB = 127, maxB = -128;
  int8_t minR = 127, maxR = -128;
  int32_t sumB = 0, sumR = 0;
  
  for (int i = 0; i < tempFIFO.count; i++) {
    int8_t tB = getTemperatureFromHistory(i);
    int8_t tR = getTemperatureFromHistory(i, SENSOR_RADIATOR);
    
    if (tB < minB) minB = tB;
    if (tB > maxB) maxB = tB;
    sumB += tB;
    
    if (tR < minR) minR = tR;
    if (tR > maxR) maxR = tR;
    sumR += tR;
  }
  
  float avgB = (float)sumB / tempFIFO.count;
  float avgR = (float)sumR / tempFIFO.count;
  
  String json = "{\"bojler\":{\"min\":" + String(minB) + 
                ",\"max\":" + String(maxB) + 
                ",\"avg\":" + String(avgB, 1) + "},";
  json += "\"radiator\":{\"min\":" + String(minR) + 
                ",\"max\":" + String(maxR) + 
                ",\"avg\":" + String(avgR, 1) + "},";
  json += "\"count\":" + String(tempFIFO.count) + "}";
  
  return json;
}