#include <Arduino.h>
//#include <ArduinoOTA.h>
#include "globals.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "tasks.h"           
#include "web_server.h" 
#include "modbus_tcp.h" 
#include "logger.h"
#include <esp_task_wdt.h>

// Globalne obiekty
ConfigManager config;
WiFiManager wifi(&config);
WebServerManager webServer(&config, &wifi); 
ModbusManager modbus;
//NTPManager ntp;  // Obiekt NTP (zdefiniowany w ntp_manager.h)
//Logger logger;   // Obiekt Logger (zdefiniowany w logger.h)


// Handles dla zadań FreeRTOS (zdefiniowane w tasks.cpp, ale extern w tasks.h)
// Nie potrzebujesz ich tutaj deklarować - są w tasks.h

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP32 START ===");
  Serial.printf("Flash size:    %d MB\n", ESP.getFlashChipSize() / 1024 / 1024);
  Serial.printf("Free heap:     %d KB\n", ESP.getFreeHeap() / 1024);
  Serial.printf("PSRAM size:    %d MB\n", ESP.getPsramSize() / 1024 / 1024);
  Serial.printf("Free PSRAM:    %d KB\n", ESP.getFreePsram() / 1024);
  Serial.printf("Chip rev:      %d\n", ESP.getChipRevision());
  Serial.printf("CPU freq:      %d MHz\n", ESP.getCpuFreqMHz());
  
  // ===== INICJALIZACJA PINÓW (z globals.h) =====
  pinMode(LED_BUILTIN_pin, OUTPUT);
    pinMode(RESET_WIFI_pin, INPUT_PULLUP);  // Używamy poprawnej nazwy z globals.h
  
  // Ustawienia początkowe
  digitalWrite(LED_BUILTIN_pin, HIGH);
  GRZALKA1_grzanie(GRZALKA_OFF);
  GRZALKA2_grzanie(GRZALKA_OFF);
  GRZALKA3_grzanie(GRZALKA_OFF);

  delay(500);
  digitalWrite(LED_BUILTIN_pin, LOW);
  delay(500);
  digitalWrite(LED_BUILTIN_pin, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN_pin, LOW);
  

  // ===== KONFIGURACJA WATCHDOG =====
  esp_task_wdt_deinit();                       // wyłącz domyślny watchdog
  esp_task_wdt_init(15, true);  // 15 sekund, true = restart przy timeout
  esp_task_wdt_add(NULL);        // dodaj bieżące zadanie (główną pętlę)
  
  Serial.println("✅ Watchdog zainicjalizowany (10s timeout)");
  
  
  // ===== INICJALIZACJA LittleFS I KONFIGURACJI =====
  if (!config.begin()) {    
    Serial.println("Błąd inicjalizacji konfiguracji");
    E_teraz.LittleFS_init = true;  // Ustawienie błędu w strukturze z globals.h
  }


  // Diagnostyka LittleFS - wyświetlenie zawartości katalogu i config.json
  Serial.println("\n=== DIAGNOSTYKA LITTLEFS ===");
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf("Plik: %s (%d bytes)\n", file.name(), file.size());        
    file = root.openNextFile();
  }
  Serial.println("=== KONIEC DIAGNOSTYKI ===\n");
  // koniec diagnostyki LittleFS

  config.printConfig();
  
  // ===== DIAGNOSTYKA PRZYCISKU BOOT w celu uruchomienia AP =====
  int bootState = digitalRead(RESET_WIFI_pin);
  Serial.printf("🔘 Stan przycisku BOOT: %s\n", bootState == LOW ? "WCISNIĘTY" : "ZWOLNIONY");
  
  // ===== SPRAWDŹ CZY PRZYCISK JEST WCISNIĘTY (PRZED WiFi) =====
  bool forceAP = (digitalRead(RESET_WIFI_pin) == LOW);
  
  if (forceAP) {
    Serial.println("🔘 Przycisk BOOT wciśnięty! Wymuszam tryb AP...");
    
    // SYGNALIZACJA: szybkie miganie LED
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_BUILTIN_pin, HIGH);
      delay(150);
      digitalWrite(LED_BUILTIN_pin, LOW);
      delay(150);
    }
    
    // Opcjonalnie: wyczyść zapisane dane WiFi
    // config.saveWifiConfig("", "", "", "", "", "", true, true);
    
    // Uruchom WiFi w trybie AP (wymuszony)
    wifi.begin(true);  // ← forceAP = true
  } else {
    Serial.println("✅ Przycisk BOOT NIE wciśnięty - normalny start");
    wifi.begin();  // normalne łączenie
  }
    
  // ===== KONFIGURACJA OTA =====
  //ArduinoOTA.setHostname("ESP32-Grzalki");
  //ArduinoOTA.begin();
  //Serial.println("OTA gotowe!");
  
  // ========== INICJALIZACJA MODBUS (DODANE) ==========
  Serial.println("\n========== MODBUS INIT ==========");
  if (modbus.begin()) {
    Serial.println("✅ Modbus zainicjalizowany pomyślnie");
    modbus.startPeriodicRead();
  } else {
    Serial.println("❌ Modbus NIE został zainicjalizowany");
  }
  Serial.println("================================\n");

  
  // ===== INICJALIZACJA MUTEXÓW (przed zadaniami!) =====
  xMutexInverterData = xSemaphoreCreateMutex();
  xMutexTemperature  = xSemaphoreCreateMutex();
  xMutexLiczniki     = xSemaphoreCreateMutex();
  
  if (!xMutexInverterData || !xMutexTemperature || !xMutexLiczniki) {
    Serial.println("❌ BŁĄD: Nie można utworzyć mutexów!");
    // Bez mutexów system nie powinien startować
    ESP.restart();
  }
  Serial.println("✅ Mutexy zainicjalizowane");

  // ===== URUCHOMIENIE ZADAŃ FreeRTOS =====
  setupTasks(&wifi, &config);
  
  webServer.begin();  
  
  Serial.println("System uruchomiony!");
  Serial.print("IP: ");
  Serial.println(wifi.getLocalIP());
  Serial.print("Strona WWW: http://");
  Serial.println(wifi.getLocalIP());

  // ===== INICJALIZACJA LOGGERA =====
  logger.begin(LOG_INFO, STORAGE_RAM_ONLY);  // uruchomienie loggera w trybie RAM, poziom INFO
  LOG_INFO("Main", "System uruchomiony");

}

// ========== LOOP ==========
void loop() {
  // Obsługa OTA
  //ArduinoOTA.handle();
  
  // Obsługa WiFi (tylko w trybie AP)
  wifi.handle();

  // obsługa serwera www
  webServer.handle(); 

  // obsługa modbus tcp
  modbus.update();

  esp_task_wdt_reset();
    
  delay(10);
}