#ifndef TEMPERATURE_FIFO_H
#define TEMPERATURE_FIFO_H

#include <Arduino.h>

enum TempSensor {
  SENSOR_BOJLER,
  SENSOR_RADIATOR
};

void initTemperatureFIFO();
void addTemperatureReading(int8_t tempBojler, int8_t tempRadiator);
int8_t getTemperatureFromHistory(int index, TempSensor sensor = SENSOR_BOJLER);
int8_t getLastTemperature(TempSensor sensor = SENSOR_BOJLER);
String getTemperatureHistoryJSON();
String getLastNTemperaturesJSON(int n);
String getTemperatureRangeJSON(int startIndex, int endIndex);
String getTemperatureSummaryJSON();

#endif