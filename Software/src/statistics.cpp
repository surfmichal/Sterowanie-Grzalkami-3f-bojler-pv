#include "statistics.h"
#include "globals.h"
#include "ntp_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

extern NTPManager ntp;


// ========== ODCZYT TOTAL Z LITTLEFS ==========
bool loadStatistics() {
  File file = LittleFS.open("/statistics.json", "r");
  if (!file) {
    Serial.println("Brak statistics.json, tworzę nowy");
    liczniki.total_stycznik = 0;
    liczniki.total_grzalka1 = 0;
    liczniki.total_grzalka2 = 0;
    liczniki.total_grzalka3 = 0;
    liczniki.zalaczenia_total_grzalka1 = 0;
    liczniki.zalaczenia_total_grzalka2 = 0;
    liczniki.zalaczenia_total_grzalka3 = 0;
    liczniki.zalaczenia_total_stycznik = 0;
    liczniki.last_save_day = 0;
    liczniki.saved_today = false;
    return false;
  }
  
  String content = file.readString();
  file.close();
  
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, content);
  
  if (error) {
    Serial.println("Błąd parsowania statistics.json");
    return false;
  }
  
  liczniki.total_grzalka1 = doc["total_grzalka1"] | 0;
  liczniki.total_grzalka2 = doc["total_grzalka2"] | 0;
  liczniki.total_grzalka3 = doc["total_grzalka3"] | 0;
  liczniki.total_stycznik = doc["total_stycznik"] | 0;              // 
  
  liczniki.zalaczenia_total_grzalka1 = doc["zalaczenia_total_grzalka1"] | 0;  //
  liczniki.zalaczenia_total_grzalka2 = doc["zalaczenia_total_grzalka2"] | 0;  //
  liczniki.zalaczenia_total_grzalka3 = doc["zalaczenia_total_grzalka3"] | 0;  // 
  liczniki.zalaczenia_total_stycznik = doc["zalaczenia_total_stycznik"] | 0;  //
  
  liczniki.last_save_day = doc["last_save_day"] | 0;
  
  if (ntp.isSynced() && ntp.getDayOfYear() == liczniki.last_save_day) {
    liczniki.saved_today = true;
  } else {
    liczniki.saved_today = false;
  }
  
  Serial.printf("Wczytano statystyki: total L1=%lu, L2=%lu, L3=%lu, Stycznik=%lu\n", 
                liczniki.total_grzalka1, liczniki.total_grzalka2, liczniki.total_grzalka3, liczniki.total_stycznik);
  return true;
}
// ========== ZAPIS DZIENNYCH DANYCH ==========
bool saveDailyStatistics() {
  // Dodaj dzisiejsze wartości do total (przed zapisem)
  liczniki.total_grzalka1 += liczniki.dzis_grzalka1;
  liczniki.total_grzalka2 += liczniki.dzis_grzalka2;
  liczniki.total_grzalka3 += liczniki.dzis_grzalka3;
  liczniki.total_stycznik += liczniki.dzis_stycznik;
  
  liczniki.zalaczenia_total_grzalka1 += liczniki.zalaczenia_dzis_grzalka1;
  liczniki.zalaczenia_total_grzalka2 += liczniki.zalaczenia_dzis_grzalka2;
  liczniki.zalaczenia_total_grzalka3 += liczniki.zalaczenia_dzis_grzalka3;
  liczniki.zalaczenia_total_stycznik += liczniki.zalaczenia_dzis_stycznik;
  
  // Przygotuj JSON do zapisu (WSZYSTKIE DANE)
  DynamicJsonDocument doc(1024);
  
  // DZIŚ (wartości z tego dnia)
  doc["dzis_grzalka1"] = liczniki.dzis_grzalka1;
  doc["dzis_grzalka2"] = liczniki.dzis_grzalka2;
  doc["dzis_grzalka3"] = liczniki.dzis_grzalka3;
  doc["dzis_stycznik"] = liczniki.dzis_stycznik;
  
  // TOTAL (dotychczasowe)
  doc["total_grzalka1"] = liczniki.total_grzalka1;
  doc["total_grzalka2"] = liczniki.total_grzalka2;
  doc["total_grzalka3"] = liczniki.total_grzalka3;
  doc["total_stycznik"] = liczniki.total_stycznik;
  
  // ZAŁĄCZENIA dziś
  doc["zalaczenia_dzis_grzalka1"] = liczniki.zalaczenia_dzis_grzalka1;
  doc["zalaczenia_dzis_grzalka2"] = liczniki.zalaczenia_dzis_grzalka2;
  doc["zalaczenia_dzis_grzalka3"] = liczniki.zalaczenia_dzis_grzalka3;
  doc["zalaczenia_dzis_stycznik"] = liczniki.zalaczenia_dzis_stycznik;
  
  // ZAŁĄCZENIA total
  doc["zalaczenia_total_grzalka1"] = liczniki.zalaczenia_total_grzalka1;
  doc["zalaczenia_total_grzalka2"] = liczniki.zalaczenia_total_grzalka2;
  doc["zalaczenia_total_grzalka3"] = liczniki.zalaczenia_total_grzalka3;
  doc["zalaczenia_total_stycznik"] = liczniki.zalaczenia_total_stycznik;
  
  // Data zapisu (dla kontroli)
  doc["last_save_day"] = ntp.getDayOfYear();
  doc["last_save_date"] = ntp.getDateString();
  
  // Zapisz do pliku
  File file = LittleFS.open("/statistics.json", "w");
  if (!file) {
    Serial.println("❌ Błąd zapisu statistics.json!");
    return false;
  }
  
  String output;
  serializeJson(doc, output);
  file.print(output);
  file.close();
  
  liczniki.saved_today = true;
  liczniki.last_save_day = ntp.getDayOfYear();
  
  Serial.printf("✅ Zapisano dzienne statystyki (dzień %d):\n", liczniki.last_save_day);
  Serial.printf("   Dziś: L1=%lu, L2=%lu, L3=%lu\n", 
                liczniki.dzis_grzalka1, liczniki.dzis_grzalka2, liczniki.dzis_grzalka3);
  Serial.printf("   Total: L1=%lu, L2=%lu, L3=%lu\n", 
                liczniki.total_grzalka1, liczniki.total_grzalka2, liczniki.total_grzalka3);
  Serial.printf("   Załączenia dziś: L1=%u, L2=%u, L3=%u, Stycznik=%u\n",
              liczniki.zalaczenia_dzis_grzalka1, 
              liczniki.zalaczenia_dzis_grzalka2, 
              liczniki.zalaczenia_dzis_grzalka3,
              liczniki.zalaczenia_dzis_stycznik);

Serial.printf("   Załączenia total: L1=%u, L2=%u, L3=%u, Stycznik=%u\n",
              liczniki.zalaczenia_total_grzalka1,
              liczniki.zalaczenia_total_grzalka2,
              liczniki.zalaczenia_total_grzalka3,
              liczniki.zalaczenia_total_stycznik);
  
  return true;
}

// ========== RESET DZIENNYCH LICZNIKÓW ==========
void resetDailyCounters() {
  liczniki.dzis_grzalka1 = 0;
  liczniki.dzis_grzalka2 = 0;
  liczniki.dzis_grzalka3 = 0;
  liczniki.dzis_stycznik = 0;
  liczniki.zalaczenia_dzis_grzalka1 = 0;
  liczniki.zalaczenia_dzis_grzalka2 = 0;
  liczniki.zalaczenia_dzis_grzalka3 = 0;
  liczniki.zalaczenia_dzis_stycznik = 0;
  liczniki.saved_today = false;
  
  Serial.println("Reset dziennych liczników (nowy dzień)");
}

// ========== AKTUALIZACJA CZASU PRACY (co sekundę, tylko RAM) ==========
void updateHeaterRuntime() {
  static bool lastState1 = false;
  static bool lastState2 = false;
  static bool lastState3 = false;
  static bool lastStateSt = false;
  static unsigned long lastCheck = 0;
  
  unsigned long now = millis();
  unsigned long delta = (now - lastCheck) / 1000;
  
  if (delta > 0 && delta < 10) {  // max 10 sekund przerwy
    //if (lastState1) liczniki.dzis_grzalka1 += delta;
    //if (lastState2) liczniki.dzis_grzalka2 += delta;
    //if (lastState3) liczniki.dzis_grzalka3 += delta;
    if (lastStateSt) liczniki.dzis_stycznik += delta;
  }
  
  //lastState1 = Z.heater1_flag;
  //lastState2 = Z.heater2_flag;
  //lastState3 = Z.heater3_flag;
  lastStateSt = stycznik.state;
  lastCheck = now;
}

// ========== ZLICZANIE ZAŁĄCZEŃ ==========
void incrementHeaterCycles(int heaterNum) {
  switch(heaterNum) {
    case 1: liczniki.zalaczenia_dzis_grzalka1++; break;
    case 2: liczniki.zalaczenia_dzis_grzalka2++; break;
    case 3: liczniki.zalaczenia_dzis_grzalka3++; break;
    case 4: liczniki.zalaczenia_dzis_stycznik++; break; // 4 wywolane podczas zalaczenia stycznika
  }
}

// ========== SPRAWDŹ CZY TRZEBA ZAPISAĆ (co minutę) ==========
void checkAndSaveDaily() {
  if (!ntp.isSynced()) return;
  
  int hour = ntp.getHour();
  int minute = ntp.getMinute();
  int dayOfYear = ntp.getDayOfYear();
  
  // Zmienne statyczne do zapobiegania wielokrotnym zapisom
  static int lastSaveHour = -1;
  static int lastSaveDay = -1;
  
  // Sprawdź czy to nowy dzień
  if (dayOfYear != liczniki.last_save_day) {
    if (liczniki.last_save_day != 0 && !liczniki.saved_today) {
      Serial.println("📅 Nowy dzień - zapisuję wczorajsze statystyki...");
      saveDailyStatistics();
    }
    resetDailyCounters();
    liczniki.saved_today = false;
    liczniki.last_save_day = dayOfYear;
    lastSaveDay = dayOfYear;
  }
  
  // Zapisz o 22:00, ale tylko raz dziennie
  if (hour == 22 && !liczniki.saved_today && (lastSaveHour != 22 || lastSaveDay != dayOfYear)) {
    Serial.println("🕙 Godzina 22:00 - zapisuję dzienne statystyki...");
    saveDailyStatistics();
    resetDailyCounters();
    liczniki.saved_today = true;
    lastSaveHour = hour;
    lastSaveDay = dayOfYear;
  }
}

// ========== POBRZ STATYSTYKI W FORMACIE JSON ==========
String getStatisticsJSON() {
  DynamicJsonDocument doc(1024);
  
  // Dziś
  doc["dzis_grzalka1"] = liczniki.dzis_grzalka1;
  doc["dzis_grzalka2"] = liczniki.dzis_grzalka2;
  doc["dzis_grzalka3"] = liczniki.dzis_grzalka3;
  doc["dzis_stycznik"] = liczniki.dzis_stycznik;
  doc["zalaczenia_dzis_grzalka1"] = liczniki.zalaczenia_dzis_grzalka1;
  doc["zalaczenia_dzis_grzalka2"] = liczniki.zalaczenia_dzis_grzalka2;
  doc["zalaczenia_dzis_grzalka3"] = liczniki.zalaczenia_dzis_grzalka3;
  doc["zalaczenia_dzis_stycznik"] = liczniki.zalaczenia_dzis_stycznik;
  
   // Total "na żywo" = ostatnio zapisany total + postęp dzisiejszy (jeszcze niezapisany)
  doc["total_grzalka1"] = liczniki.total_grzalka1 + liczniki.dzis_grzalka1;
  doc["total_grzalka2"] = liczniki.total_grzalka2 + liczniki.dzis_grzalka2;
  doc["total_grzalka3"] = liczniki.total_grzalka3 + liczniki.dzis_grzalka3;
  doc["total_stycznik"] = liczniki.total_stycznik + liczniki.dzis_stycznik;
  
  doc["zalaczenia_total_grzalka1"] = liczniki.zalaczenia_total_grzalka1 + liczniki.zalaczenia_dzis_grzalka1;
  doc["zalaczenia_total_grzalka2"] = liczniki.zalaczenia_total_grzalka2 + liczniki.zalaczenia_dzis_grzalka2;
  doc["zalaczenia_total_grzalka3"] = liczniki.zalaczenia_total_grzalka3 + liczniki.zalaczenia_dzis_grzalka3;
  doc["zalaczenia_total_stycznik"] = liczniki.zalaczenia_total_stycznik + liczniki.zalaczenia_dzis_stycznik;
  
  
  // Info o zapisie
  doc["last_save_day"] = liczniki.last_save_day;
  doc["saved_today"] = liczniki.saved_today;
  
  String response;
  serializeJson(doc, response);
  return response;
}

// naliczanie czasu pracy
void addHeaterRuntimeSeconds(int heaterIndex, uint32_t seconds) {
  switch (heaterIndex) {
    case 0: liczniki.dzis_grzalka1 += seconds; break;
    case 1: liczniki.dzis_grzalka2 += seconds; break;
    case 2: liczniki.dzis_grzalka3 += seconds; break;
  }
}