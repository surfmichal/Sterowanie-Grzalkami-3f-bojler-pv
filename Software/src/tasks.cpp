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


// Handles zadań
TaskHandle_t taskWiFiHandle = NULL;
TaskHandle_t taskLEDHandle = NULL;
TaskHandle_t taskAlarmHandle = NULL;
TaskHandle_t taskTemperatureHandle = NULL;

// ========== KONFIGURACJA DS18B20 ==========
OneWire oneWire(ONE_WIRE_pin);
DallasTemperature ds18b20(&oneWire);
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
  pinMode(LED_AL1_pin, OUTPUT);
  digitalWrite(LED_AL1_pin, LOW);
  
  while (true) {
    WDT_RESET();  // 🔥 KOPNIJ WATCHDOGA
    if (alarmTriggered) {
      // Szybkie miganie przy alarmie
      digitalWrite(LED_AL1_pin, HIGH);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      digitalWrite(LED_AL1_pin, LOW);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    } else {
      digitalWrite(LED_AL1_pin, LOW);
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

// ========== ZADANIE 4: ODCZYT TEMPERATURY ==========
void taskTemperature(void* parameter) {
  // Inicjalizacja DS18B20
  ds18b20.begin();
  
  // Sprawdź czy sensor jest podłączony
  int deviceCount = ds18b20.getDeviceCount();
  if (deviceCount == 0) {
    Serial.println("⚠️ Błąd: Nie znaleziono czujnika DS18B20 na pinie ONE_WIRE_pin!");
    LOG_INFO("DS18B20", "Nie znaleziono czujnika DS18B20 na pinie ONE_WIRE_pin!");
  } else {
    Serial.printf("✅ Znaleziono %d czujnik(i) DS18B20\n", deviceCount);
    LOG_INFO("DS18B20", "Znaleziono %d czujnik(i) DS18B20", deviceCount);
    
    // Wyświetl adresy czujników
    for (int i = 0; i < deviceCount; i++) {
      DeviceAddress address;
      ds18b20.getAddress(address, i);
      Serial.printf("  Czujnik %d: ", i);
      for (uint8_t j = 0; j < 8; j++) {
        Serial.printf("%02X", address[j]);
      }
      Serial.println();
    }
  }
  
  while (true) {
    WDT_RESET();  // 🔥 KOPNIJ WATCHDOGA
    // Rozpocznij konwersję temperatury na wszystkich czujnikach
    ds18b20.requestTemperatures();
    
    // Odczytaj temperaturę z pierwszego czujnika (indeks 0)
    float tempC = ds18b20.getTempCByIndex(0);
    
    // Sprawdź czy odczyt jest poprawny (DEVICE_DISCONNECTED_C = -127)
    if (tempC != DEVICE_DISCONNECTED_C) {
      // Zapisz do globalnej struktury Temperatury
      Z.T_current = (int8_t)tempC;
      LOG_INFO("DS18B20", "Zmierzono temperaturę bojlera %.1f°C", Z.T_current);
      
      // Debug co 30 sekund
      static unsigned long lastPrint = 0;
      if (millis() - lastPrint > 30000) {
        Serial.printf("🌡️ Temperatura bojlera: %.1f°C\n", tempC);
        lastPrint = millis();
      }
    } else {
      Serial.println("⚠️ Błąd odczytu DS18B20 - czujnik nie odpowiada!");
      LOG_INFO("DS18B20", "Problem z pomiarem temperatury");
      Z.T_current = -127;
    }
    
    // Czekaj 5 sekund między odczytami
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

// ========== ZADANIE 5: ZAŁĄCZANIE GRZAŁEK ==========
void taskHeaterControl(void* parameter) {
  static int updateCount = 0;
  heaterControl.begin();
  
  // Wczytaj konfigurację z pliku
  heaterControl.setThresholds(U.Ugrid_on, U.Ugrid_off);
  heaterControl.setTurnOffDelay(U.HeaterDelay_off_ms);  // Zmiana nazwy
  //heaterControl.enableSystem(heater_config.enabled);
  U.bojlerTmax = 80.0; // Z config.json

    
  while (true) {
    WDT_RESET();  // 🔥 KOPNIJ WATCHDOGA
    unsigned long currentTime = millis();
        
    if (currentTime - lastModbusRead >= modbusCfg.readInterval) {
      lastModbusRead = currentTime;
      //updateCount++;      
      
      // Pobierz napięcia z globalnej struktury modbusData
      float v1 = modbusData.gridVoltage1;
      float v2 = modbusData.gridVoltage2;
      float v3 = modbusData.gridVoltage3;
      
      // Pobierz temperaturę z globalnej struktury Z
      float temp = Z.T_current;
      heaterControl.setBojlerTemperature(temp);
      
      // Ustaw status Modbus
      heaterControl.setModbusStatus(modbusData.connected);
      
      // Steruj grzałkami (UWAGA: update nie przyjmuje parametrów!)
      heaterControl.update();

      //if (updateCount % 10 == 0) {
        //Serial.printf("📊 Heater update #%d: L1=%.1fV, L2=%.1fV, L3=%.1fV, T=%.1f°C\n", 
        //          updateCount, v1, v2, v3, temp);
        //LOG_INFO("HeaterControl", "sprawdzamy warunki grzania");
      //}
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
      int8_t temp = Z.T_current;  // lub T.bojler.temperatura
      
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
  loadStatistics();
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
    4096,
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
    4096,
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