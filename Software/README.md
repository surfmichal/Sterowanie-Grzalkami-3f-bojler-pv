# Sterowanie Grzałkami 3F (bojler/PV) — dokumentacja projektu

Sterownik oparty na ESP32, którego zadaniem jest **zapobieganie wyłączeniom falownika PV z powodu zbyt wysokiego napięcia sieciowego**. Gdy napięcie w sieci rośnie (bo falownik oddaje nadwyżkę mocy, a sieć lokalnie jest "słaba"), sterownik załącza grzałki 3-fazowe (np. w bojlerze), obniżając w ten sposób lokalne napięcie i pozwalając falownikowi pracować dalej zamiast się wyłączyć.

Repozytorium: https://github.com/surfmichal/Sterowanie-Grzalkami-3f-bojler-pv

---

## 1. Architektura systemu — widok z lotu ptaka

```
┌──────────────────┐        ┌──────────────────────────────────────────┐
│  Falownik Sofar  │        │                  ESP32                   │
│  (Modbus TCP)    │◄──┐    │                                          │
└──────────────────┘   │    │  ┌─────────────┐   ┌──────────────────┐  │
                       ├────┼─►│ DataManager │──►│  HeaterControl   │  │
┌──────────────────┐   │    │  │(Modbus/HTTP)│   │ (logika grzałek, │  │
│  Serwer Python   │   │    │  └─────────────┘   │stycznik, blokady)│  │
│  (Flask + SQLite)│◄──┘    │                    └──────────────────┘  │
│  - odczyt Modbus │        │                              │           │
│  - zapis do bazy │        │                              ▼           │
│  - własny        │        │  ┌────────────┐      ┌──────────────┐    │
│    dashboard WWW │        │  │  Triaki    │ ◄──  │   Stycznik   │    │
└──────────────────┘        │  │ (3x GPIO)  │      │   główny     │    │
        ▲                   │  └────────────┘      └──────────────┘    │
        │ HTTP GET          │                                          │
        │ /api/data         │  Web Server (strona własna ESP32):       │
        └───────────────────┼──  index.html / settings.html / logi     │
                            │  Statystyki, historia temperatur (FIFO)  │
                            └──────────────────────────────────────────┘
```

Kluczowa decyzja projektowa: **dwa niezależne źródła danych z falownika**, przełączane w ustawieniach:

1. **Modbus TCP bezpośrednio** do falownika (`modbus_tcp.cpp`) — ESP32 sam pyta falownik o rejestry. (W tym projekcie odpytywany falownik to SOFAR KTLX-8.8)
2. **HTTP do zewnętrznego serwera Python** (`http_data_client.cpp`) — osobny skrypt Python (Flask) czyta falownik przez Modbus, zapisuje historię do SQLite i ma **własny, rozbudowany dashboard WWW z wykresami** (napięcia, moc, produkcja dobowa). ESP32 w tym trybie **nie odpytuje falownika bezpośrednio** — tylko raz na `readDataInterval` woła `GET /api/data` na tym serwerze. To odciąża falownik od nadmiarowych zapytań Modbus (dwa niezależne klienty odpytujące ten sam falownik jednocześnie bywają problematyczne).

Oba źródła danych, niezależnie od pochodzenia, trafiają do **jednej wspólnej struktury `InverterData inverterData`** (patrz sekcja 3) — cała reszta systemu (`HeaterControl`, strona WWW ESP32) nie musi wiedzieć, skąd dane pochodzą.

---

## 2. Struktura plików

| Plik | Rola |
|---|---|
| `main.cpp` | `setup()`/`loop()` — inicjalizacja pinów, watchdoga, LittleFS, WiFi, Modbus, mutexów, uruchomienie tasków i serwera WWW |
| `globals.h` / `globals.cpp` | Wszystkie struktury danych i zmienne globalne (patrz sekcja 3) |
| `config_manager.cpp/h` | Odczyt/zapis `config.json` z LittleFS (WiFi, źródło danych, Modbus, HTTP, ustawienia grzałek, NTP) |
| `wifi_manager.cpp/h` | Połączenie WiFi (STA) + tryb Access Point z portalem konfiguracyjnym, gdy brak zapisanej sieci |
| `data_manager.cpp/h` | Przełącznik między źródłem Modbus a HTTP, ujednolicone `fetchData()` |
| `modbus_tcp.cpp/h` | Klient Modbus TCP (biblioteka eModbus), odczyt rejestrów falownika Sofar |
| `http_data_client.cpp/h` | Klient HTTP pobierający dane z serwera Python (Flask) |
| `heater_control.cpp/h` | **Rdzeń logiki** — sterowanie 3 grzałkami + stycznikiem, system blokad bezpieczeństwa |
| `statistics.cpp/h` | Liczniki czasu pracy i liczby załączeń (dziś/total), zapis do `/statistics.json` |
| `temperature_fifo.cpp/h` | Bufor cykliczny (FIFO) 720 pomiarów temperatury bojlera i radiatora |
| `onewire_manager.cpp/h` | Obsługa czujników DS18B20 (bojler, radiator) |
| `ntp_manager.cpp/h` | Synchronizacja czasu (NTP), detekcja zmiany dnia |
| `logger.cpp/h` | Bufor logów w RAM (opcjonalnie też LittleFS), z deduplikacją powtarzających się komunikatów |
| `tasks.cpp/h` | Definicje i uruchamianie zadań FreeRTOS (patrz sekcja 5) |
| `web_server.cpp/h` | Serwer HTTP ESP32 — strony HTML + wszystkie endpointy `/api/...` |
| `index.html` | Strona główna — podgląd na żywo (napięcia, prądy, moc, grzałki, stycznik, temperatury) |
| `settings.html` | Strona ustawień (progi napięć, opóźnienia, źródło danych, WiFi, logi) |
| `logger.html` | Podgląd logów systemowych z filtrowaniem |

---

## 3. Kluczowe struktury danych (`globals.h`)

### `InverterData inverterData` — wspólny format danych z falownika

Niezależnie od źródła (Modbus lub HTTP), dane trafiają do tych samych pól:

|       Pole          |           Opis             |    Jednostka                 |
|---------------------|----------------------------|------------------------------|
| `connected`         | Czy dane są aktualne/ważne |      bool |
| `gridVoltage1/2/3`  | Napięcia fazowe L1/L2/L3   |       V |
| `gridCurrent1/2/3`  | Prądy fazowe L1/L2/L3      | A |
| `gridPower`         | Moc całkowita (grid, przez HTTP) / `total_pv_power` (przez Modbus) | W |
| `gridFrequency`     | Częstotliwość sieci | Hz |
| `pv1_voltage/current/power`, `pv2_voltage/current/power` | Parametry stringów PV1/PV2 | V / A / W |
| `total_pv_power`    | Suma mocy PV1+PV2          |      W |
| `dailyEnergy`       | Produkcja dzisiejsza       |      kWh |
| `totalEnergy`       | Produkcja całkowita        |      kWh |
| `innerTemp`         | Temperat wewnęt falownika  |       °C |
| `moduleTemp`        | Temperat wewnęt falownika  |        °C |
| `timestamp`         | `millis()` ostatniej aktualizacji | ms |
| *(tylko Modbus)* `status`, `alarm1-5`, `busVoltage`, `insulationPv1/2`, `insulationToGnd`, `country`, `comPhA/B/C`, `totalHours`, `todayTime` | Dodatkowe rejestry diagnostyczne, odczytywane tylko przy źródle Modbus | — |

**Ważne:** dostęp do `inverterData` z wielu tasków jest chroniony mutexem `xMutexInverterData`. `HeaterControl::update()` na początku każdego cyklu kopiuje całą strukturę do lokalnej kopii `localData` pod ochroną mutexa — reszta logiki operuje wyłącznie na tej kopii, żeby uniknąć niespójności przy jednoczesnym nadpisywaniu danych przez task pobierający dane.

### Format HTTP z serwera Python (Flask)

Gdy aktywne jest źródło `SOURCE_HTTP`, ESP32 odpytuje endpoint `GET {http_data.addr}` (domyślnie coś w stylu `http://<IP-serwera-python>:8080/api/data`) i oczekuje JSON:

```json
{
  "status": "Online",
  "Ua": 230.1, "Ub": 229.8, "Uc": 231.0,
  "Ia": 5.2,  "Ib": 5.1,  "Ic": 5.1,
  "Power": 3450,
  "Upv1": 380.2, "Ipv1": 8.1, "Power_Pv1": 3080,
  "Upv2": 375.0, "Ipv2": 0.9, "Power_Pv2": 337,
  "Today_Production": 12.4,
  "Total_Production": 4521,
  "Themp_Inner": 42,
  "Themp_Module": 38,
  "DataTime": "2026-07-16 14:32:01"
}
```

Gdy falownik jest offline, serwer Python zwraca `{"status": "Offline", "komunikat": "..."}` — ESP32 wtedy zeruje wszystkie pola liczbowe i ustawia `connected = false`.

Ten sam serwer Python (Flask) prowadzi też **niezależny, własny dashboard WWW** (osobne adresy: `/dashboard` — wykres napięć, `/moc` — wykres mocy z wyliczonym czasem przestoju falownika, `/produkcja` — tabela dobowej produkcji) korzystający z historii zapisanej w plikach SQLite (`RRRRMMDD.s3db`, jeden plik na dzień). To jest **odrębny system** od dashboardu ESP32 — służy do analizy historycznej i wykresów, podczas gdy ESP32 pokazuje stan bieżący plus własne (krótsze, 720-punktowe) statystyki temperatur.

### `Ustawienia U` — konfiguracja sterowania (persystowana w `config.json`)

|         Pole              |       Znaczenie                                                 |         Jednostka       |
| --------------------------|-----------------------------------------------------------------|-------------------------|
| `HeaterEnabled`           | Główny wyłącznik systemu grzania                                |       bool              |
| `Ugrid_on` / `Ugrid_off`  | Progi napięcia załączenia/wyłączenia grzałek                    |       V                 |
| `HeaterDelay_on_ms` / `HeaterDelay_off_ms` | Opóźnienia (histereza czasowa) załączenia/wyłączenia pojedynczej grzałki | ms |
| `ContactorDelay_off_ms`   | Opóźnienie wyłączenia stycznika głównego po ustaniu potrzeby grzania| ms                  |
| `MinPowerLock`/ `MinPower`| Blokada: grzałki załączają się tylko, gdy moc falownika ≥ `MinPower`| bool / W            |
| `bojlerTmax`/ `radiatorTmax`| Progi temperatur krytycznych                                  |       °C                |
| `radiatorT_critical`      | Czy brak/awaria czujnika radiatora ma blokować grzałki          |       bool              |
| `readDataInterval`        | Interwał odpytywania źródła danych (Modbus/HTTP)                |       ms                |
| `temperatureLogInterval`  | Interwał zapisu do bufora FIFO temperatur                       |       **sekundy**       |
| `serwer_www_port`         | Port serwera WWW ESP32                                          |       —                 |

### `LicznikiCzasu liczniki` — statystyki (persystowane w `statistics.json`)

Dla każdego elementu (`grzalka1/2/3`, `stycznik`) osobno: `dzis_*` (sekundy pracy dziś), `total_*` (sekundy pracy łącznie, zapisane historycznie), `zalaczenia_dzis_*` i `zalaczenia_total_*` (liczba załączeń). Patrz sekcja 6.

### `HeaterState` (×3) i `StycznikState`

Flagi stanów pojedynczej grzałki / stycznika: `state` (fizycznie ON/OFF), `waitingToTurnOn`/`waitingToTurnOff` (w trakcie odliczania histerezy), `turnOnTime`/`turnOffTime` (docelowy `millis()`).

### `TemperatureFIFO tempFIFO`

Bufor cykliczny na `MAX_TEMP_HISTORY = 720` pomiarów (`int8_t`, °C) dla bojlera (`values_boj`) i radiatora (`values_rad`), indeksowany przez `head`/`count`.

### `HeaterBlockFlags heaterBlocks`

Zbiór flag blokad bezpieczeństwa, przeliczany raz na cykl w `HeaterControl::updateBlockFlags()` — patrz sekcja 4.

---

## 4. Logika sterowania grzałkami (`heater_control.cpp`)

### 4.1 Cykl `HeaterControl::update()`

Wywoływany cyklicznie z `taskHeaterControl` (co `U.readDataInterval` ms):

1. **Kopiuje** `inverterData` → `localData` pod mutexem (spójny zrzut danych na cały cykl).
2. **`updateBlockFlags()`** — przelicza wszystkie blokady bezpieczeństwa **raz** na cykl (patrz 4.2).
3. **Walidacja danych** (`isInverterDataValid()`) — sprawdza `connected` oraz czy napięcia mieszczą się w sensownym zakresie (150–270 V). Jeśli nie — wymusza wyłączenie wszystkich grzałek i stycznika, `return`.
4. **Walidacja temperatur** (`isTemperatureSafe()`) — sprawdza czujnik bojlera (krytyczny zawsze) i radiatora (krytyczny tylko jeśli `radiatorT_critical == true`). Jeśli niebezpiecznie — wymusza wyłączenie, `return`.
5. **`updateContactor()`** — decyduje o załączeniu/wyłączeniu stycznika głównego na podstawie tego, czy którakolwiek grzałka jest potrzebna lub fizycznie pracuje.
6. **Pętla po 3 fazach** — dla każdej: sprawdza progi napięcia (`shouldTurnOn`/`shouldStartTurnOffTimer`), zarządza licznikami czasu (`startTurnOnTimer`/`executeTurnOn`/`startTurnOffTimer`), i finalnie `updateHeaterState()` — jedyne miejsce fizycznie zapisujące stan pinu GPIO oraz flagę `Z.heaterX_flag` (na podstawie **wykrytej zmiany** stanu pinu).

### 4.2 System blokad (`updateBlockFlags()` / `heaterBlocks`)

Grzałki mogą się załączyć tylko, gdy **żadna** z blokad nie jest aktywna:

|     Blokada             |     Warunek                                                               |
|-------------------------|---------------------------------------------------------------------------|
| `inverter_offline`      | Brak ważnych danych z falownika                                           |
| `temp_bojler_exceeded`  | Temperatura bojlera ≥ max, lub czujnik nie odpowiada (zawsze krytyczne)   | 
| `temp_bojler_sensor_error`| Temperatura bojlera ≥ max, lub czujnik nie odpowiada (zawsze krytyczne) |
| `temp_radiator_exceeded`| Jak wyżej dla radiatora, tylko jeśli `radiatorT_critical`                 |
| `radiator_sensor_error` | Jak wyżej dla radiatora, tylko jeśli `radiatorT_critical`                 |
| `minPowerInverter`      | Moc falownika poniżej `MinPower`, jeśli `MinPowerLock` aktywne            |
| `heater_system_disabled`| `U.HeaterEnabled == false`                                                |
| `manual_disable`        | Ręczne wyłączenie (rezerwa na przyszłość / API)                           |

Logi tekstowe (`🔴 Blokada: ...`) dla każdej flagi wypisują się **tylko przy zmianie stanu** (przejście false→true), dzięki statycznym zmiennym `prev_*` — inaczej przy stanie utrzymującym się długo (np. moc poniżej minimum przez całą noc) logi zalałyby konsolę.

`isHeaterAllowedCached()` czyta wynik bez przeliczania (używane wewnątrz pętli fazowej, żeby nie wywoływać `updateBlockFlags()` po kilka razy na cykl). `isHeaterAllowed()` (publiczne, przeliczające) jest do użycia poza głównym cyklem, np. w endpointzie `/api/block_status`.

### 4.3 Stycznik główny

Załącza się z opóźnieniem `STYCZNIK_DELAY_ON` (stała), zanim jakakolwiek grzałka zostanie fizycznie włączona (zabezpieczenie przed przełączaniem triaków bez zasilania z sieci). Wyłącza się dopiero, gdy **żadna** grzałka nie jest już potrzebna **i żadna** nie jest fizycznie załączona, po odczekaniu `ContactorDelay_off_ms`. Próba wyłączenia stycznika, gdy triaki są jeszcze fizycznie ON, jest logowana jednorazowo (`⚠️ Nie mogę wyłączyć - triaki są załączone!`), z deduplikacją analogiczną do blokad.

### 4.4 Ważna zasada projektowa: kto ustawia flagi/liczniki

Jest tylko **jedno** miejsce ustawiające `Z.heaterX_flag` (flaga używana przez moduł statystyk do liczenia czasu pracy) — `updateHeaterFlag()`, wywoływane z `updateHeaterState()` (przy wykryciu zmiany stanu pinu) **oraz** jawnie z `turnOnNow()` (bo tam pin jest zapisywany bezpośrednio, więc `updateHeaterState()` nie wykryłoby już żadnej zmiany). Podobnie `incrementHeaterCycles()` woła się dokładnie raz na realne zdarzenie załączenia — w `turnOnNow()` dla grzałek, w `turnOnContactor()` dla stycznika. To był częsty temat błędów w trakcie rozwoju — dwa niezależne miejsca próbujące "wykryć" to samo zdarzenie prowadziły do sytuacji, w której żadne z nich go nie wykrywało.

---

## 5. Zadania FreeRTOS (`tasks.cpp`)

| Task | Core | Priorytet | Interwał | Rola |
|---|---|---|---|---|
| `taskWiFiMonitor` | 1 | 1 | 10 s | Monitoruje połączenie WiFi, próbuje reconnect |
| `taskTemperature` | 0 | 1 | 5 s | Odczyt czujników DS18B20 (bojler, radiator) do struktury `T`, pod mutexem `xMutexTemperature` |
| `taskNTPSync` | 0 | 1 | 6 s | Synchronizacja NTP, detekcja nowego dnia (tylko informacyjnie — reset liczników **nie** jest tu wykonywany, żeby uniknąć wyścigu z `checkAndSaveDaily()`) |
| `taskDataFetch` | 1 | 1 | `U.readDataInterval` | Pobiera dane z aktywnego źródła (Modbus/HTTP) przez `DataManager` |
| `taskStatisticsMonitor` | 0 | 1 | 1 s (sprawdza co minutę) | `updateHeaterRuntime()` (czas pracy stycznika przez próbkowanie), `checkAndSaveDaily()` (zapis/reset dobowy) |
| `taskTemperatureLogger` | 1 | 1 | `U.temperatureLogInterval` (s) | Dopisuje pomiar do bufora FIFO |
| `taskLED` | 1 | 0 | 100 ms | Heartbeat LED |
| `taskAlarm` | 1 | 2 | 100 ms | Dioda alarmowa (rezerwa) |
| `taskHeaterControl` | 1 | 2 | `U.readDataInterval` | Główna logika — `heaterControl.update()` |
| `ModbusManager::taskModbus` | 0 | 2 | `U.readDataInterval` | (tylko gdy źródło = Modbus) cykliczne żądania rejestrów |

Wszystkie taski są zarejestrowane w watchdogu (`esp_task_wdt_add`) i wywołują `WDT_RESET()` w pętli — zabezpieczenie przed zawieszeniem się dowolnego zadania.

**Uwaga na przyszłość:** czas pracy grzałek (`dzis_grzalka1/2/3`) liczony jest **zdarzeniowo** — dokładny czas ON→OFF liczony przez `millis()` przy realnym przełączeniu pinu (patrz `addHeaterRuntimeSeconds()` w sekcji 6), a nie przez próbkowanie co sekundę jak stycznik. To ważne rozróżnienie: przy bardzo krótkich, częstych cyklach grzałek próbkowanie co 1s mogłoby w ogóle nie wykryć stanu ON (aliasing) — stąd inne podejście niż dla stycznika, gdzie cykle są zwykle znacznie dłuższe.

---

## 6. Statystyki (`statistics.cpp`)

### Model danych: "dziś" + "total"

- `dzis_*` — narastają w czasie rzeczywistym w trakcie dnia (sekundy pracy / liczba załączeń).
- `total_*` — suma historyczna, aktualizowana **tylko** przy zapisie dobowym (nie w czasie rzeczywistym), żeby ograniczyć liczbę zapisów do flash LittleFS.
- **API (`getStatisticsJSON()`) zwraca `total_*` "na żywo"** — jako `total_zapisany + dzis_bieżący` — dając realistyczny podgląd narastającej sumy bez faktycznego zapisu do flash przy każdym odczycie.

### Zapis dobowy (`saveDailyStatistics()` + `resetDailyCounters()`)

Wyzwalane przez `checkAndSaveDaily()` (wywoływane co ~1 minutę z `taskStatisticsMonitor`) w dwóch niezależnych sytuacjach, z których **każda** musi kończyć się resetem liczników dziennych:

1. **Wykrycie zmiany `dayOfYear`** (przejście przez północ) — zapisuje (jeśli jeszcze nie zapisano danego dnia) i **zawsze** resetuje liczniki, niezależnie od tego, czy zapis faktycznie się wykonał.
2. **Godzina 22:00** — dodatkowy, "zaplanowany" punkt zapisu w ciągu dnia (przed północą), też z resetem.

To zabezpiecza przed dwoma trybami awarii: (a) ESP32 wystartowane tuż przed północą pierwszego dnia pracy — nie ma jeszcze zapisu o 22:00, więc reset musi się wykonać przy wykryciu nowego dnia; (b) restart urządzenia w trakcie dnia — `loadStatistics()` poprawnie wczytuje wszystkie pola `total_*` i `zalaczenia_total_*` z pliku, żeby restart nie zerował historii.

### Czas pracy — dwa mechanizmy

- **Stycznik**: próbkowanie raz na sekundę (`updateHeaterRuntime()`), sprawdza `stycznik.state`.
- **Grzałki**: zdarzeniowo, dokładny pomiar `millis()` między ON a OFF (`addHeaterRuntimeSeconds()`, wywoływane z `updateHeaterFlag()` w `heater_control.cpp`) — odporne na bardzo krótkie cykle załączeń, których próbkowanie co 1s mogłoby nie wykryć.

### Plik `/statistics.json`

Przykładowa zawartość — patrz plik `statistics.json` w repozytorium. Zawiera wszystkie pola `dzis_*`, `total_*`, `zalaczenia_dzis_*`, `zalaczenia_total_*` dla `grzalka1/2/3` i `stycznik`, plus `last_save_day`/`last_save_date`.

---

## 7. Historia temperatur (`temperature_fifo.cpp`)

Bufor cykliczny 720 pomiarów (przy domyślnym interwale 120 s to ~24 godziny historii) dla bojlera i radiatora równolegle. Funkcje `getTemperatureFromHistory(index, sensor)` / `getLastTemperature(sensor)` przyjmują parametr `enum TempSensor { SENSOR_BOJLER, SENSOR_RADIATOR }` (domyślnie bojler, dla kompatybilności wstecznej wywołań bez parametru).

Każdy JSON zwracany przez API (`getTemperatureHistoryJSON`, `getLastNTemperaturesJSON`, `getTemperatureRangeJSON`) zawiera pole `interval_sec` — samodokumentujący się odstęp czasowy między próbkami, tak żeby dowolny klient (nie tylko strona WWW ESP32) mógł poprawnie odtworzyć oś czasu bez znajomości konfiguracji urządzenia.

**Ograniczenie do świadomości:** jeśli `temperatureLogInterval` zostanie zmieniony w trakcie działania urządzenia, stare wpisy w buforze (logowane przy poprzednim interwale) i tak będą interpretowane z **aktualnym** `interval_sec` — drobna nieścisłość osi czasu przy zmianie ustawienia, akceptowalna, jeśli interwał zmienia się rzadko.

---

## 8. Serwer WWW ESP32 (`web_server.cpp`) — mapa endpointów

### Strony HTML
| Endpoint | Plik |
|---|---|
| `/` | `index.html` — podgląd na żywo |
| `/settings` | `settings.html` — konfiguracja |
| `/logs` | `logger.html` — przeglądarka logów |

### API — odczyt danych
| Endpoint | Zwraca |
|---|---|
| `GET /api/data` | Pełne dane z falownika + konfiguracja Modbus/HTTP/grzałek |
| `GET /api/status` | Status systemu (WiFi, uptime, RAM, stan grzałek/stycznika, temperatury, wersja firmware) |
| `GET /api/temperatures` | Bieżące temperatury bojler/radiator |
| `GET /api/temperature/history` | Pełna historia FIFO (bojler+radiator+interwał) |
| `GET /api/temperature/last?n=N` | Ostatnie N pomiarów |
| `GET /api/temperature/summary` | Min/max/avg dla obu czujników |
| `GET /api/statistics` | Liczniki czasu pracy i załączeń (dziś/total) |
| `GET /api/heater_config` | Konfiguracja progów/opóźnień grzałek |
| `GET /api/heater/status` | Czy system grzania jest włączony |
| `GET /api/block_status` | Szczegółowy status wszystkich blokad bezpieczeństwa |
| `GET /api/data_source` | Aktywne źródło danych + konfiguracja Modbus i HTTP |
| `GET /api/config` | Konfiguracja WiFi |
| `GET /api/time` | Status NTP, bieżący czas |
| `GET /api/version` | Wersja firmware, data kompilacji |
| `GET /api/logs`, `/api/logs/download` | Logi (JSON / plik tekstowy) |
| `GET /api/logs_config` | Konfiguracja loggera |
| `GET /api/simulation` | Stan trybu symulacji (do testów bez realnego falownika) |

### API — zapis/akcje (`POST`)
| Endpoint | Rola |
|---|---|
| `/api/save_wifi` | Zapis WiFi + restart |
| `/api/save_modbus`, `/api/save_http_data`, `/api/save_data_source` | Konfiguracja źródeł danych |
| `/api/save_heater` | Zapis progów/opóźnień/blokad grzałek |
| `/api/heater/enable` | Włącz/wyłącz cały system grzania |
| `/api/save_logs_config`, `/api/logs/clear` | Konfiguracja i czyszczenie logów |
| `/api/restart` | Restart ESP32 |
| `/api/reset_wifi` | Reset ustawień WiFi + restart |
| `/api/simulation` | Ustawienie trybu symulacji napięć (testy bez falownika) |

---

## 9. Plik konfiguracyjny `config.json`

Zarządzany przez `ConfigManager`, przechowywany w LittleFS. Struktura (przykład w repozytorium): sekcje `wifi`, `data_source`, `modbus`, `http_data`, `Ustawienia` (mapuje się na strukturę `U`), `ntp`. Zapis odbywa się przez `config->updateConfig(lambda)` (jeden zbiorczy zapis, unikanie częściowych/niespójnych aktualizacji) albo dedykowane `saveWifiConfig()`, `saveModbusConfig()`, `saveHttpDataConfig()`, `saveUstawienia()`.

---

## 10. Tryb symulacji

Do testowania logiki bez podłączonego falownika: `simulationMode` (globalna flaga) pozwala wymusić dowolne `simVoltage1/2/3` i `simulationModbusConnected`, wstrzykiwane w `ModbusManager::taskModbus()` zamiast realnego odczytu. Sterowane przez `index.html` (panel "Tryb symulacji") i `POST /api/simulation`.

---

## 11. Znane ograniczenia / pomysły na przyszłość

- **Zmiana `temperatureLogInterval` w locie** nieznacznie zaburza interpretację starszych wpisów w buforze FIFO (brak zapisanych znaczników czasu per-próbka) — akceptowalne przy rzadkich zmianach configu.
- **Rozjazd interwałów** — warto pilnować, żeby `readDataInterval` używane przez `taskDataFetch`, `taskHeaterControl` i (przy Modbus) `ModbusManager::taskModbus` pozostawały spójne — obecnie wszystkie czytają tę samą wartość `U.readDataInterval`, ale zmiana w jednym miejscu bez przemyślenia całości może to rozjechać.
- **Serwer Python (Flask + SQLite)** to osobny projekt/proces — nie jest częścią tego repozytorium, ale stanowi zależność w trybie `SOURCE_HTTP`. Jego dashboard (`/dashboard`, `/moc`, `/produkcja`) korzysta z plików `.s3db` per dzień w katalogu bazy danych.
- **Brak automatycznych testów** — cała logika była iterowana i testowana ręcznie na sprzęcie; przy dalszej rozbudowie warto rozważyć choć proste testy jednostkowe logiki `HeaterControl` (można wydzielić czystą logikę decyzyjną od I/O GPIO).

---

## 12. Historia najważniejszych poprawek (skrót)

Dla przyszłego "ja" wracającego do projektu — te błędy już naprawiono, warto pamiętać wzorzec, żeby nie powtórzyć czegoś podobnego:

1. **Duplikacja zliczania cykli grzałek** — `executeTurnOn()` ustawiał `state->state = true` przed wywołaniem `turnOnNow()`, więc `turnOnNow()` (jedyne miejsce z `incrementHeaterCycles()`) nigdy nie widziało zmiany `false→true`. Rozwiązanie: tylko `turnOnNow()` ustawia stan.
2. **Ten sam wzorzec dla `Z.heaterX_flag`** — `updateHeaterState()` wykrywa zmianę przez `digitalRead(pin) != target`, ale `turnOnNow()` już wcześniej zapisuje pin — więc `updateHeaterState()` nie widzi różnicy. Rozwiązanie: `turnOnNow()` jawnie woła `updateHeaterFlag(index, true)`.
3. **Brak resetu liczników przy zmianie dnia** — `checkAndSaveDaily()` resetowało liczniki tylko w gałęzi "godzina 22:00", nie w gałęzi wykrycia zmiany `dayOfYear` — powodowało to, że liczniki dzienne nie zerowały się, jeśli urządzenie nie zdążyło "zobaczyć" godziny 22:00 danego dnia.
4. **`loadStatistics()` nie wczytywało wszystkich pól `total_*`** — po restarcie ESP32 część historycznych sum wracała do zera.
5. **Spam logów** — `updateBlockFlags()` i `updateContactor()` logowały przy **każdym** cyklu, gdy warunek był spełniony, zamiast tylko przy zmianie stanu — rozwiązanie: statyczne zmienne `prev_*` pamiętające poprzedni stan każdej flagi.
6. **Watchdog nie obejmował części tasków** — w `setupTasks()` przekazywano `NULL` zamiast `&handle` do `xTaskCreatePinnedToCore()`, więc `esp_task_wdt_add()` rejestrował błędne zadanie (aktualnie wykonujące się, nie nowo utworzone).
7. **Wyścig danych (race condition)** przy odczycie `inverterData` z wielu tasków bez spójnej kopii — rozwiązanie: `HeaterControl::update()` kopiuje całość do `localData` pod jednym mutexem na początku cyklu, cała reszta logiki operuje tylko na tej kopii.
