#include "globals.h"

// ========== DEFINICJE ZMIENNYCH GLOBALNYCH ==========
Bledy       E_teraz = {0};
Bledy       E_byle  = {0};
Alarmy      A_teraz = {0};
Alarmy      A_byle  = {0};
Ustawienia  U       = {0};
Zmienne     Z       = {0};

APConfig    apCfg     = {0};
WifiConfig  wifiCfg       = {0};
ModbusConfig modbusCfg    = {0};
HttpDataConfig httpDataCfg = {false, "http://192.168.0.251:8080/api/data", 5000, 5000, 3, 1000};
DataSource activeDataSource = SOURCE_NONE;
InverterData inverterData = {0};
NtpConfig ntpCfg = {0};

HeaterState heater1_state = {false, 0, 0, false, false};
HeaterState heater2_state = {false, 0, 0, false, false};
HeaterState heater3_state = {false, 0, 0, false, false};
StycznikState stycznik = {false, false, 0, false, false};

CzasNTP czasNTP = {0, 0, 0, 0, 0, 0, 0, 0, 0, false};
LicznikiCzasu liczniki = {0};
TemperatureFIFO tempFIFO = {{0}, 0, 0, 0};
Temperatury T = {  
  .bojler = {0.0f, false, {0}, "Bojler", 0},
  .radiator = {0.0f, false, {0}, "Radiator", 0}
};
unsigned long lastHeaterCheckTime = 0;


// ========== DEFINICJE STAŁYCH DO SYMULACJI ==========
bool simulationMode = false;
bool simulationModbusConnected = false;
float simVoltage1 = 250.0;
float simVoltage2 = 250.0;
float simVoltage3 = 250.0;

// ========== ZMIENNE POMOCNICZE ==========
unsigned long currentTime = 0;
unsigned long previousTime = 0;
const long timeoutTime = 5000;  // 5 sekund timeout


// ========== MUTEXY ==========
SemaphoreHandle_t xMutexInverterData = NULL;
SemaphoreHandle_t xMutexTemperature  = NULL;
SemaphoreHandle_t xMutexLiczniki     = NULL;

// ========== DATA KOMPILACJI (automatycznie z kompilatora) ==========
const char* COMPILE_DATE = __DATE__;      // "Mmm DD YYYY"
const char* COMPILE_TIME = __TIME__;      // "HH:MM:SS"
const char* COMPILE_DATETIME = __DATE__ " " __TIME__;