#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "freertos/semphr.h"

// ========== STEROWANIE (odwrócona logika) ==========
#define ON  LOW      // LOW = załączone (aktywne)
#define OFF HIGH     // HIGH = wyłączone

// Lub bezpośrednio:
#define STYCZNIK_ON  LOW
#define STYCZNIK_OFF HIGH
#define GRZALKA_ON   LOW
#define GRZALKA_OFF  HIGH
#define LED_ON       LOW
#define LED_OFF      HIGH

// ========== DEFINICJE PINÓW ==========
// (Twoje definicje pozostają bez zmian)
#define LED_BUILTIN_pin 2
#define LED_GRZALKA1_pin 22
#define LED_GRZALKA2_pin 18
#define LED_GRZALKA3_pin 19
#define GRZALKA1_pin 13
#define GRZALKA2_pin 14
#define GRZALKA3_pin 27
//#define DAC_pin 34      // UWAGA: GPIO34 jest TYLKO WEJŚCIOWE!
#define ONE_WIRE_A_pin 4
#define ONE_WIRE_B_pin 16
#define RESET_WIFI_pin 0
#define STYCZNIK_PIN 5          // GPIO5 - sterowanie stycznikiem
#define STYCZNIK_STAN_PIN 21    // wejście monitorujące stan napięcia sterującego stycznikiem przechodzi przez termostat i przekaźnik
#define STYCZNIK_DELAY_ON 500   // ms - czas po załączeniu stycznika przed triakami
#define STYCZNIK_DELAY_OFF 500  // ms - czas po wyłączeniu triaków przed stycznikiem

// ========== DEFINICJE INNEGO RODZAJU ==========
#define WDT_RESET() esp_task_wdt_reset()

// ========== DEKLARACJE STRUKTUR (zostają) ==========
struct APConfig {
  char ssid[32];        // Nazwa sieci AP (domyślnie "ESP_Config")
  char password[64];    // Hasło do AP (puste = brak)
  bool active;          // Czy tryb AP jest aktywny
};

struct Bledy {
  bool czujnik_Tmax;
  bool LittleFS_init;
  bool LittleFS_read;
  bool LittleFS_write;
  bool modbusTCP_read;
};

struct Alarmy {
  bool ogolny;
  bool al1;
  bool al2;
  bool al3;
};

struct Zmienne {
  bool heater1_flag;     // stan pracy grzalki 1
  bool heater2_flag;     // stan pracy grzalki 2
  bool heater3_flag;     // stan pracy grzalki 3
  bool Tmax_flag;         // stan osiagniecia temperatury max
  bool Contractor_flag;    // stan stycznika
  //bool modbus_state_ok;  // stan polaczenia modbus
  //float T_current;       // Aktualna temperatura bojlera(°C)
  //bool T_sensor_ok;      // Czy czujnik działa  
};

struct Ustawienia {
  bool HeaterEnabled;           // ← Aktywacja systemu grzałek (true=aktywne, false=nieaktywne)
  float Ugrid_on;               // ← float
  float Ugrid_off;              // ← float
  uint16_t HeaterDelay_on_ms;   // ← uint16_t
  uint16_t HeaterDelay_off_ms;  // ← uint16_t
  uint16_t ContactorDelay_off_ms; // czas do wylaczenia stycznika
  int8_t bojlerTmax;             // ← int8
  int8_t radiatorTmax;           // ← int8 
  bool radiatorT_critical;      // ← bool (flaga czy używać temperatury z radiatora do blokowania grzałek)
  uint8_t serwer_www_port;      // ← uint8_t lub int
};
// ========== STRUKTURA CZASU ==========
struct CzasNTP {
  int year;           // rok (np. 2025)
  int month;          // miesiąc (1-12)
  int day;            // dzień miesiąca (1-31)
  int hour;           // godzina (0-23)
  int minute;         // minuta (0-59)
  int second;         // sekunda (0-59)
  int dayOfWeek;      // dzień tygodnia (1-7, gdzie 1 = poniedziałek)
  int dayOfYear;      // dzień roku (0-365)
  unsigned long timestamp;  // UNIX timestamp
  bool synced;        // czy czas jest zsynchronizowany
};

struct WifiConfig {
  char ip[16];
  char gate[16];
  char mask[16];
  char dns[16];
  char ssid[30];
  char pass[30];
  bool dhcp;
  bool active;
};

struct HeaterState {
  bool state;                 // Aktualny stan (true=ON)
  unsigned long turnOnTime;   // Czas kiedy należy włączyć
  unsigned long turnOffTime;  // Czas kiedy należy wyłączyć
  bool waitingToTurnOn;       // Czy czeka na włączenie
  bool waitingToTurnOff;      // Czy czeka na wyłączenie  
};

struct InverterData {
  // Podstawowe parametry falownika
  uint16_t status;           // ST - status pracy
  uint16_t alarm1;           // AL1
  uint16_t alarm2;           // AL2
  uint16_t alarm3;           // AL3
  uint16_t alarm4;           // AL4
  uint16_t alarm5;           // AL5
  
  // PV stringi
  float pv1_voltage;         // Upv1 (V)
  float pv1_current;         // Ipv1 (A)
  float pv2_voltage;         // Upv2 (V)
  float pv2_current;         // Ipv2 (A)

  float pv1_power;           // P1 = Upv1 * Ipv1 (W)
  float pv2_power;           // P2 = Upv2 * Ipv2 (W)
  float total_pv_power;      // P1 + P2 (W)
  
  // Sieć
  float gridFrequency;       // Freq (Hz)
  float gridVoltage1;        // Ua (V) - faza 1
  float gridCurrent1;        // Ia (A) - faza 1
  float gridVoltage2;        // Ub (V) - faza 2
  float gridCurrent2;        // Ib (A) - faza 2
  float gridVoltage3;        // Uc (V) - faza 3
  float gridCurrent3;        // Ic (A) - faza 3
  
  // Produkcja
  float totalEnergy;         // Total_Production (kWh)
  uint32_t totalHours;       // Total_Hours (h)
  float dailyEnergy;         // Today_Production (kWh)
  uint16_t todayTime;        // Today_Time (min)
  
  // Temperatury
  uint16_t moduleTemp;       // Themp_Module
  uint16_t innerTemp;        // Themp_Inner
  float busVoltage;          // Bus_Voltage (V)
  
  // Izolacja
  uint16_t insulationPv1;    // Ins_Pv1
  uint16_t insulationPv2;    // Ins_Pv2
  uint16_t insulationToGnd;  // Ins_To_Gnd
  
  uint16_t country;          // Country
  uint16_t comPhA;           // Com_Ph_A
  uint16_t comPhB;           // Com_Ph_B
  uint16_t comPhC;           // Com_Ph_C
  
  float power;               // Obliczona moc chwilowa (W)
  //bool mbConnected;          // Flaga połączenia Modbus (true=połączono)
  //bool httpConnected;        // Flaga połączenia (true=połączono)
  bool connected;            // Flaga połączenia (true=połączono)
  unsigned long timestamp;   // Czas ostatniego odczytu (millis)
};


struct ModbusConfig {
  char ip[16];             // IP falownika
  uint16_t port;           // Port (domyślnie 502)
  uint8_t unitId;          // ID urządzenia (domyślnie 255 dla Sofar)
  uint16_t readInterval;   // Interwał odczytu (ms)
  uint16_t timeout;        //
  uint16_t maxRetries;    //
  uint16_t retryDelay;    //
  bool enabled;            // Czy Modbus jest aktywny
};

// ========== KONFIGURACJA HTTP DATA ==========
struct HttpDataConfig {
  bool enabled;
  char addr[100];
  uint16_t interval;      // ms
  uint16_t timeout;       // ms
  uint8_t maxRetries;
  uint16_t retryDelay;   // ms
};


// ========== ŹRÓDŁO DANYCH ==========
enum DataSource : uint8_t {
  SOURCE_MODBUS = 0,
  SOURCE_HTTP = 1,
  SOURCE_NONE = 2
};

/// ========== LICZNIKI CZASU PRACY ==========
struct LicznikiCzasu {
  // Dzienny czas pracy (w sekundach) – resetowane codziennie
  uint32_t dzis_grzalka1;
  uint32_t dzis_grzalka2;
  uint32_t dzis_grzalka3;
  
  // Całkowity czas pracy (w sekundach) – akumulowane z LittleFS
  uint32_t total_grzalka1;
  uint32_t total_grzalka2;
  uint32_t total_grzalka3;
  
  // Liczba załączeń w ciągu dnia
  uint16_t zalaczenia_dzis_grzalka1;
  uint16_t zalaczenia_dzis_grzalka2;
  uint16_t zalaczenia_dzis_grzalka3;
  
  // Liczba załączeń total (opcjonalnie)
  uint32_t zalaczenia_total_grzalka1;
  uint32_t zalaczenia_total_grzalka2;
  uint32_t zalaczenia_total_grzalka3;
  
  // Data ostatniego zapisu (dzień roku)
  uint16_t last_save_day;
  
  // Flaga: czy dziś już zapisaliśmy
  bool saved_today;
};

// ========== STAN STYCZNIKA ==========
struct StycznikState {
  bool state;                 // true=załączony, false=wyłączony
  bool requested;             // czy ktoś chce załączyć triaki
  unsigned long lastChange;   // czas ostatniej zmiany
  bool waitingToTurnOn;       // opóźnienie załączenia trika
  bool waitingToTurnOff;      // opóźnienie wyłączenia trika
};

// ========== BUFOR TEMPERATUR (FIFO) ==========
#define MAX_TEMP_HISTORY 720   // 24 godziny * (60 minut / 2 minuty) = 720

struct TemperatureFIFO {
  int8_t values[MAX_TEMP_HISTORY];  // wartości temperatur (-128 do 127°C)
  uint16_t head;                     // indeks gdzie zapisać następną wartość
  uint16_t count;                    // liczba zapisanych pomiarów (max 720)
  uint16_t last_index;               // ostatni odczytany indeks (do API)
};



// ========== STRUKTURA DLA CZUJNIKA TEMPERATURY ==========
struct CzujnikTemp {
  float temperatura;
  bool ok;
  uint8_t adres[8];
  char nazwa[20];
  unsigned long lastReadTime;
};

// ========== STRUKTURA TEMPERATUR (rozszerzona) ==========
struct Temperatury {
    
  // Czujniki
  CzujnikTemp bojler;
  CzujnikTemp radiator;
  
  // Istniejące tablice (zostawiamy)
  float temperatury[2][12];
  float dT5s[6];
  float dT10s[6];
  float dT30s[6];
  float dT60s[6];
};

// ========== SYMULACJA ==========
extern bool simulationMode;
extern bool simulationModbusConnected; 
extern float simVoltage1;
extern float simVoltage2;
extern float simVoltage3;

// ========== ZMIENNE GLOBALNE ==========
extern LicznikiCzasu liczniki;
extern unsigned long lastHeaterCheckTime;  // do zliczania czasu pracy
extern Bledy       E_teraz;
extern Bledy       E_byle;
extern Alarmy      A_teraz;
extern Alarmy      A_byle;

extern APConfig    apConfig;
extern WifiConfig  wifiCfg;
extern InverterData inverterData; 
extern ModbusConfig modbusCfg;
extern HttpDataConfig httpDataCfg;
extern DataSource activeDataSource; 

extern Ustawienia  U;
extern Zmienne     Z;
extern Temperatury T;
extern HeaterState heater1_state;
extern HeaterState heater2_state;
extern HeaterState heater3_state;
extern CzasNTP czasNTP;
extern StycznikState stycznik;
extern TemperatureFIFO tempFIFO;

// ========== MUTEXY WSPÓŁDZIELONYCH DANYCH ==========
extern SemaphoreHandle_t xMutexInverterData;  // chroni inverterData
extern SemaphoreHandle_t xMutexTemperature;   // chroni T (bojler, radiator)
extern SemaphoreHandle_t xMutexLiczniki;      // chroni liczniki


// ========== ZMIENNE POMOCNICZE ==========
extern unsigned long currentTime;
extern unsigned long previousTime;
extern const long timeoutTime;

// ========== FUNKCJE INLINE ==========
inline void GRZALKA1_grzanie(bool value) { 
  digitalWrite(GRZALKA1_pin, value); 
  digitalWrite(LED_GRZALKA1_pin, value); 
  Z.heater1_flag = value; 
}

inline void GRZALKA2_grzanie(bool value) { 
  digitalWrite(GRZALKA2_pin, value); 
  digitalWrite(LED_GRZALKA2_pin, value); 
  Z.heater2_flag = value; 
}

inline void GRZALKA3_grzanie(bool value) { 
  digitalWrite(GRZALKA3_pin, value); 
  digitalWrite(LED_GRZALKA3_pin, value); 
  Z.heater3_flag = value; 
}

// Poprawiona funkcja copyChars (bez warnings)
inline void copyChars(const char *source, char *destination, int length) { 
  for (int i = 0; i < length && source[i] != '\0'; i++) { 
    destination[i] = source[i];  
  }
}



// ========== WERSJA OPROGRAMOWANIA ==========
#define FIRMWARE_VERSION "1.0.0"  // Główna wersja
#define FIRMWARE_NAME "ESP32_HeaterController_OV"  // Nazwa projektu

// Te zmienne zostaną automatycznie wypełnione przez kompilator
extern const char* COMPILE_DATE;     // Data kompilacji (YYYY-MM-DD)
extern const char* COMPILE_TIME;     // Czas kompilacji (HH:MM:SS)
extern const char* COMPILE_DATETIME; // Pełna data i czas



#endif