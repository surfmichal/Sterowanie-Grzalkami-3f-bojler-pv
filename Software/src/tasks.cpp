#include "tasks.h"
#include "globals.h"
#include <Arduino.h>
#include "wifi_manager.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "heater_control.h"
#include "logger.h"
#include "ntp_manager.h"
#include "statistics.h"
#include "temperature_fifo.h"
#include <esp_task_wdt.h>
#include "onewire_manager.h"


// Handles zadań
TaskHandle_t taskWiFiHandle = NULL;
TaskHandle_t taskLEDHandle = NULL;
TaskHandle_t taskAlarmHandle = NULL;
TaskHandle_t taskTemperatureHandle = NULL;

// ========== DEFINICJE CZUJNIKÓW ==========
OneWireManager sensorBojler(ONE_WIRE_A_pin);
OneWireManager sensorRadiator(ONE_WIRE_B_pin);

HeaterControl heaterControl;

// Zmienne współdzielone między zadaniami (volatile dla bezpieczeństwa)
volatile bool alarmTriggered = false;
volatile float lastCurrentValue = 0.0;

unsigned long lastModbusRead = 0;

// Struktura do przekazywania parametrów do zadań
struct TaskParams {
  WiFiManager* wifi;
  ConfigManager* config;
};

// ========== ZADANIE 1: LED (miganie w normalnej pracy) ==========
void taskLED(void* parameter) {
  pinMode(LED_GRZALKA1_pin, OUTPUT);
  pinMode(LED_GRZALKA2_pin, OUTPUT);
  pinMode(LED_GRZALKA3_pin, OUTPUT);
  
  bool state = false;
  
  while (true) {
    WDT_RESET();  // 🔥 KOPNIJ WATCHDOGA
    // Normalne mruganie (heartbeat) tylko jeśli nie ma alarmu
    if (!alarmTriggered) {
      state = !state;
      digitalWrite(LED_GRZALKA1_pin, state);
      digitalWrite(LED_GRZALKA2_pin, !state);  // Przeciwna faza
      digitalWrite(LED_GRZALKA3_pin, state);
    }
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// ========== ZADANIE 2: ALARM (dioda alarmowa z wzorami) ==========
void taskAlarm(void* parameter) {
  while (true) {
    WDT_RESET();  // 🔥 KOPNIJ WATCHDOGA
    if (alarmTriggered) {
      // Szybkie miganie przy alarmie
      //digitalWrite(LED_AL1_pin, HIGH);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      //digitalWrite(LED_AL1_pin, LOW);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    } else {
      //digitalWrite(LED_AL1_pin, LOW);
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
  }
}
 
// ========== ZADANIE 3: MONITOR WiFi (z próbą ponownego połączenia) ==========
void taskWiFiMonitor(void* parameter) {
  TaskParams* params = (TaskParams*)parameter;
  WiFiManager* wifi = params->wifi;
  ConfigManager* config = params->config;
  
  while (true) {
    WDT_RESET();  // Kopnięcie watchdoga na początku każdego obiegu (co 10s)
    
    if (!wifi->isAPMode()) {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n⚠️ WiFi rozłączone!");
        LOG_INFO("WiFi", "WiFi rozłączone!");
        
        WifiConfig* wifiCfg = config->getWifiConfig();
        
        if (strlen(wifiCfg->ssid) > 0) {
          Serial.println("Wydano komendę ponownego połączenia (asynchronicznie)...");
          LOG_INFO("WiFi", "Próbuję ponownie połączyć się z siecią WiFi");
          WiFi.reconnect(); 
          // NIE robimy tu pętli while! 
          // Pozwalamy zadaniu zasnąć. Za 10 sekund sprawdzimy, czy się udało.
        }
      } else {
        // Opcjonalnie: jeśli połączone, możesz wypisać status, ale rzadko, żeby nie śmiecić w Serialu
      }
    }
    
    // Zadanie zasypia na 10 sekund. W tym czasie watchdog wie, że to zadanie "żyje"
    vTaskDelay(10000 / portTICK_PERIOD_MS);  
  }
}

// ========== ZADANIE 4: ODCZYT TEMPERATUR Z WSZYSTKICH CZUJNIKÓW ==========
void taskTemperature(void* parameter) {
  sensorBojler.begin();
  sensorRadiator.begin();
  
  int countBojler = sensorBojler.getDeviceCount();
  int countRadiator = sensorRadiator.getDeviceCount();
  
  if (countBojler > 0) {
    T.bojler.ok = true;
    Serial.println("✅ Czujnik bojlera: podłączony");
  } else {
    T.bojler.ok = false;
    Serial.println("⚠️ Czujnik bojlera: NIE podłączony");
  }
  
  if (countRadiator > 0) {
    T.radiator.ok = true;
    Serial.printf("✅ Czujnik radiatora: podłączony (krytyczny: %s)\n", 
                  U.radiatorT_critical ? "TAK" : "NIE");
  } else {
    T.radiator.ok = false;
    Serial.printf("⚠️ Czujnik radiatora: NIE podłączony (krytyczny: %s)\n", 
                  U.radiatorT_critical ? "TAK" : "NIE");
  }
  
  while (true) {
    WDT_RESET();
    
    // === CZUJNIK BOJLERA ===
    if (countBojler > 0) {
      sensorBojler.requestTemperatures();
      float temp = sensorBojler.readTemperature(0);
      if (temp != DEVICE_DISCONNECTED_C) {
        T.bojler.temperatura = temp;        
        T.bojler.ok = true;
      } else {
        T.bojler.ok = false;
      }      
    }
    
    // === CZUJNIK RADIATORA ===
    if (countRadiator > 0) {
      sensorRadiator.requestTemperatures();
      float temp = sensorRadiator.readTemperature(0);
      if (temp != DEVICE_DISCONNECTED_C) {
        T.radiator.temperatura = temp;
        T.radiator.ok = true;
      } else {
        T.radiator.ok = false;
      }
    } else {
      // Brak czujnika – ustaw domyślną temperaturę (bezpieczną)
      T.radiator.ok = false;      
    }
    Serial.printf("📡 Odczyt temperatury: bojler=%.2f°C, radiator=%.2f°C\n", 
                    T.bojler.temperatura, T.radiator.temperatura);
    LOG_INFO("Temperature", "Odczyt temperatury: bojler=%.2f°C, radiator=%.2f°C", 
                    T.bojler.temperatura, T.radiator.temperatura);
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

// ========== ZADANIE 5: ZAŁĄCZANIE GRZAŁEK ==========
void taskHeaterControl(void* parameter) {
  static int updateCount = 0;
  heaterControl.begin();
  
  // Wczytaj konfigurację z pliku
  heaterControl.setThresholds(U.Ugrid_on, U.Ugrid_off);
  heaterControl.setDelays(U.HeaterDelay_on_ms, U.HeaterDelay_off_ms);  // ← UŻYJ setDelays()
  heaterControl.enableSystem(U.HeaterEnabled);
  
  Serial.printf("⚙️ HeaterControl: U_on=%.1fV, U_off=%.1fV, delay_on=%dms, delay_off=%dms\n",
                U.Ugrid_on, U.Ugrid_off, U.HeaterDelay_on_ms, U.HeaterDelay_off_ms);
  
  while (true) {
    WDT_RESET();
    unsigned long currentTime = millis();
        
    if (currentTime - lastModbusRead >= modbusCfg.readInterval) {
      lastModbusRead = currentTime;
      updateCount++;      
      
      float v1 = modbusData.gridVoltage1;
      float v2 = modbusData.gridVoltage2;
      float v3 = modbusData.gridVoltage3;
      
      float temp = T.bojler.temperatura;
      
      heaterControl.setModbusStatus(modbusData.connected);
      
      heaterControl.update();
    }
    
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}
    
// ========== ZADANIE 6: SYNCHRONIZACJA NTP ==========
void taskNTPSync(void* parameter) {
  // Czekaj na połączenie WiFi (z watchdogiem)
  while (WiFi.status() != WL_CONNECTED) {
    WDT_RESET();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  
  // Nieblokująca inicjalizacja NTP
  ntp.begin("pool.ntp.org", 3600, 3600);
  Serial.println("NTP: rozpoczęto asynchroniczną synchronizację czasu");
  LOG_INFO("NTP", "Rozpoczynam asynchroniczną synchronizację czasu");
  
  // Główna pętla zadania
  while (true) {
    WDT_RESET();  // 🔥 kopnij watchdoga
    
    ntp.update();  // to już nie blokuje!
    
    // Sprawdź czy to nowy dzień (reset liczników)
    if (ntp.isSynced() && ntp.isNewDay()) {
      Serial.println("NTP: wykryto nowy dzień! Resetuję liczniki...");
      LOG_INFO("NTP", "Wykryto nowy dzień! Resetuję liczniki...");
      resetDailyCounters();
    }
    
    vTaskDelay(60000 / portTICK_PERIOD_MS);  // co minutę
  }
}

// ========== ZADANIE 7: MONITOR STATYSTYK ==========

void taskStatisticsMonitor(void* parameter) {
  // Odczekaj chwilę na synchronizację NTP (max 30 sekund)
  int waitCount = 0;
  while (!ntp.isSynced() && waitCount < 30) {
    WDT_RESET();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    waitCount++;
  }
  
  if (ntp.isSynced()) {
    Serial.println("📊 Statistics Monitor: NTP zsynchronizowany, startuję monitoring");
    LOG_INFO("StatisticsMonitor", "NTP zsynchronizowany, startuję monitoring");
  } else {
    Serial.println("⚠️ Statistics Monitor: NTP niezsynchronizowany, ale startuję (będę czekać na synchronizację)");
    LOG_INFO("StatisticsMonitor", "NTP niezsynchronizowany, ale startuję (będę czekać na synchronizację)");
  }
  
  unsigned long lastCheck = 0;
  const unsigned long CHECK_INTERVAL = 60000;  // 1 minuta
  
  while (true) {
    WDT_RESET();  // 🔥 kopnij watchdoga
    
    unsigned long now = millis();
    
    if (now - lastCheck >= CHECK_INTERVAL) {
      lastCheck = now;
      
      // Sprawdź czy zapisać statystyki (o 22:00)
      checkAndSaveDaily();
      
      // Opcjonalnie: pokaż status co godzinę
      static int lastDebugHour = -1;
      if (ntp.isSynced()) {
        int currentHour = ntp.getHour();
        if (currentHour != lastDebugHour) {
          lastDebugHour = currentHour;
          Serial.printf("📊 Statystyki: L1=%lu, L2=%lu, L3=%lu\n", 
                        liczniki.dzis_grzalka1, 
                        liczniki.dzis_grzalka2, 
                        liczniki.dzis_grzalka3);
          LOG_INFO("StatisticsMonitor", "Statystyki: L1=%lu, L2=%lu, L3=%lu", 
                        liczniki.dzis_grzalka1, 
                        liczniki.dzis_grzalka2, 
                        liczniki.dzis_grzalka3);
        }
      }
    }
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // sprawdzaj co sekundę, ale zapisuj co minutę
  }
}
// ========== ZADANIE 8: ZAPIS HISTORII TEMPERATURY CO 2 MINUTY ==========
void taskTemperatureLogger(void* parameter) {
  // Inicjalizacja bufora FIFO
  initTemperatureFIFO();
  
  unsigned long lastLogTime = 0;
  const unsigned long LOG_INTERVAL = 120000;  // 2 minuty = 120000 ms
  
  while (true) {
    WDT_RESET();  // 🔥 kopnij watchdoga
    
    unsigned long now = millis();
    
    if (now - lastLogTime >= LOG_INTERVAL) {
      lastLogTime = now;
      
      // Pobierz aktualną temperaturę (z zadania temperature)
      int8_t temp = (int8_t)T.bojler.temperatura;
      
      // Dodaj do bufora FIFO
      addTemperatureReading(temp);
      
      // Debug co 30 minut (ale nie za często)
      static unsigned long lastPrint = 0;
      if (millis() - lastPrint > 1800000) {  // 30 minut
        lastPrint = millis();
        Serial.printf("🌡️ Zapamiętano temperaturę: %d°C (łącznie %d pomiarów)\n", 
                      temp, tempFIFO.count);
        LOG_INFO("TemperatureLogger", "Zapamiętano temperaturę: %d°C (łącznie %d pomiarów)", 
                      temp, tempFIFO.count);
      }
    }
    
    vTaskDelay(10000 / portTICK_PERIOD_MS);  // sprawdzaj co 10 sekund
  }
}

// ========== URUCHOMIENIE WSZYSTKICH ZADAŃ ==========
void setupTasks(WiFiManager* wifi, ConfigManager* config) {
  
  // Utwórz parametry dla zadań (zwolnij pamięć po uruchomieniu?)
  TaskParams* params = new TaskParams;
  params->wifi = wifi;
  params->config = config;
  //loadStatistics();
  //loadTemperatureHistory();
  
  // Core 1: WiFi monitor (przekaż parametry)
  xTaskCreatePinnedToCore(
    taskWiFiMonitor,
    "WiFi Monitor",
    4096,
    (void*)params,        // ← PRZEKAZUJEMY params, NIE wifi
    1,
    &taskWiFiHandle,
    1
  );
  esp_task_wdt_add(taskWiFiHandle);  // DODAJ DO WATCHDOG
  //Serial.println("  - WiFi Monitor dodany do watchdoga");
  
  // Core 0: Odczyty temperatury
  xTaskCreatePinnedToCore(
    taskTemperature,
    "Temperature Task",
    8192,
    NULL,
    1,
    &taskTemperatureHandle,
    0
  );
  esp_task_wdt_add(taskTemperatureHandle);  // DODAJ DO WATCHDOG
  //Serial.println("  - Temperature Task dodany do watchdoga");
  


  // Core 0: synchronizacja NTP 
  xTaskCreatePinnedToCore(
    taskNTPSync,
    "NTP Sync",
    4096,
    NULL,
    1,     // wyższy priorytet
    NULL,
    0
  );
  

  // Zadanie monitora statystyk czasu pracy grzałek (Core 0)
  xTaskCreatePinnedToCore(
    taskStatisticsMonitor,
    "Statistics Monitor",
    4096,
    NULL,
    1,
    NULL,
    0
  );
  
  
  // Zadanie logowania temperatury co 2 minuty (Core 1)
  xTaskCreatePinnedToCore(
    taskTemperatureLogger,
    "Temp Logger",
    8192,
    NULL,
    1,
    NULL,
    1
  );
  
  // Core 1: LED (priorytet niższy)
  xTaskCreatePinnedToCore(
    taskLED,
    "LED Task",
    4096,
    NULL,
    0,
    &taskLEDHandle,
    1
  );
  esp_task_wdt_add(taskLEDHandle);  // DODAJ DO WATCHDOG
  
  // Core 1: Alarm (wyższy priorytet)
  xTaskCreatePinnedToCore(
    taskAlarm,
    "Alarm Task",
    2048,
    NULL,
    2,
    &taskAlarmHandle,
    1
  );
  esp_task_wdt_add(taskAlarmHandle);  // DODAJ DO WATCHDOG
  
  // Core 1: Sterowanie grzałkami (DODANE)
  xTaskCreatePinnedToCore(
    taskHeaterControl,
    "Heater Control",
    4096,
    NULL,
    2,
    NULL,
    1
  );// Nie dodajemy do watchdoga, bo to główne zadanie sterujące  
  
  Serial.println("Zadania FreeRTOS uruchomione:");
  Serial.println("  - WiFi Monitor (Core 0, prio 1)");
  Serial.println("  - Temperature Task (Core 0, prio 1)");
  Serial.println("  - LED Task (Core 1, prio 0)");
  Serial.println("  - Heater Task (Core 1, prio 2)");
  Serial.println("  - Alarm Task (Core 1, prio 2)");
}