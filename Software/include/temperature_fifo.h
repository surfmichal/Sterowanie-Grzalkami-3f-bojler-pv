#ifndef TEMPERATURE_FIFO_H
#define TEMPERATURE_FIFO_H

#include <Arduino.h>

void initTemperatureFIFO();
void addTemperatureReading(int8_t temp);
int8_t getTemperatureFromHistory(int index);
int8_t getLastTemperature();
String getTemperatureHistoryJSON();
String getLastNTemperaturesJSON(int n);
String getTemperatureRangeJSON(int startIndex, int endIndex);
String getTemperatureSummaryJSON();

#endif