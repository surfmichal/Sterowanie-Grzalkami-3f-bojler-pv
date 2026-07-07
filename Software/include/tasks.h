#ifndef TASKS_H
#define TASKS_H

#include <Arduino.h>
#include "config_manager.h"
#include "wifi_manager.h"

// Deklaracje zadań FreeRTOS
void taskLED(void* parameter);
void taskAlarm(void* parameter);
void taskWiFiMonitor(void* parameter);
void taskTemperature(void* parameter); 

// Deklaracja funkcji uruchamiającej zadania
void setupTasks(WiFiManager* wifi, ConfigManager* config);  // 

// Deklaracje extern handle (definicje w tasks.cpp)
extern TaskHandle_t taskWiFiHandle;
extern TaskHandle_t taskLEDHandle;
extern TaskHandle_t taskAlarmHandle;
extern TaskHandle_t taskTemperatureHandle; 

#endif