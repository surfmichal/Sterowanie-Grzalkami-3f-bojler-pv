Układ esp32 zrobiony w celu zbijania napiecia na sieci 3 fazowej,
x- pobieramy wartosci napieć fazowych bezpośrednio z rejestrów modbus falownika Sofar KTLX-8.8
- wysterowujemy 3 triaki BTA24 (po jednym na każdą fazę)
- pomiar temperatury bojlera przekroczenie Tmax - blokuje załączanie triaków
- dodatkowy wyłącznik termiczny na 80 stopni bezpośrednio zablokuje obwody sterowania triakami
x- dane dostępne będą na www
- 5 diód led (Power, Tmax, grzałka1..3 )
- liczniki czasu zadziałania poszczególnych grzałek i ilości załączeń
- ustawiane napięcia progrowe załączenia i wyłączenia
- ustawiane godziny zadziałania
- ustawiane Tmax
- ustawiane dane do polaczenia wifi na www 
x- odczyt danych z pliku littleFs do polaczenia z wifi oraz modbus oraz ustawienia parametrów
- zapis jw poprzez www
x- przy braku możliwości zalogowania do wifi - esp32 przechodzi w tryb AP
x- program z ładowaniem firmware przez wifi - OTA

- opcjonalnie pomiar prądu przez czujnik sct013 30A/1V
- pomiar mocy oraz zliczanie energii
- opcjonalnie mqtt do OpenHab
- opcjonalnie założony będzie pomiar prądu na przewodzie N - zmierzy prąd sumaryczny z wszystkich grzałek
- opcjonalnie wykrywanie przejscia przez zero i sterowanie impulsowe

projekt/
├── platformio.ini
├── include/                    # ← Pliki nagłówkowe (.h)
│   ├── globals.h
│   ├── config_manager.h
│   ├── wifi_manager.h
│   ├── tasks.h
│   ├── hardware_pins.h
|   ├── webserver.h
|   ├── modbus_tcp.h
│   └── (future)
│       ├── 
│       ├── 
│       └── 
├── src/                        # ← Pliki źródłowe (.cpp)
│   ├── main.cpp
│   ├── globals.cpp
│   ├── config_manager.cpp
│   ├── wifi_manager.cpp
│   ├── tasks.cpp
|   ├── webserver.cpp
|   ├── modbus_tcp.cpp
│   └── (future)
│       ├── 
│       ├── 
│       └── 
└── data/
    ├── config.json
    └── wlan.json


Algorytm załączania grzałek dla każdej z faz osobno:
Napięcie L1
     │
     │  U_on = 252V ──┼────────────────────────────────────────────────────
     │                │                                                    
     │                │  ⚡ NATYCHMIAST załącz grzałkę L1                  
     │                │                                                    
     │                │  Grzałka L1 = ON                                   
     │                │                                                    
     │  U_off = 250V ──┼────────────────────────────────────────────────────
     │                │        ⏱️ Rozpocznij odliczanie 5s                 
     │                │        ↓                                           
     │                │        ⏱️ 1s... (nadal <=250V)                     
     │                │        ⏱️ 2s...                                    
     │                │        ↓                                           
     │                │        ⚡ Napięcie wzrosło do 251V (>250V)          
     │                │        🔄 ANULUJ odliczanie!                       
     │                │        ↓                                           
     │                │        Grzałka L1 = ON (dalej)                     
     │                │                                                    
     │                │        ⏱️ Napięcie spadło do 249V                  
     │                │        ⏱️ Rozpocznij odliczanie 5s                 
     │                │        ⏱️ 1s, 2s, 3s, 4s, 5s...                   
     │                │        ↓                                           
     │                │        ❌ WYŁĄCZ grzałkę                           
     │                │                                                    
     └────────────────┴────────────────────────────────────────────────────▶ czas

📊 Architektura zadań FreeRTOS
┼────────────────────┼──────────┼───────────────┼───────────────────────────────────┐
│    Zadanie         |	Core    |	Priorytet   |	Odpowiedzialność                │
│────────────────────┼──────────┼───────────────┼───────────────────────────────────┼
│    WiFi Monitor    |	Core 0	|       1	    |   Monitorowanie i odtwarzanie WiFi│    
│    Modbus Task     |	Core 0  |	    2	    |   Odczyt danych z falownika       │
│    Temperature Task|	Core 0	|       1       |	Odczyt DS18B20                  │
│    Heater Control	 |  Core 1  |	    2	    |   Logika załączania grzałek       │
│    LED Task	     |  Core 1	|       0	    |   Miganie diodami                 │
│    Alarm Task      |	Core 1	|       2       |	Sygnalizacja alarmów            │
└────────────────────┴──────────────────────────────────────────────────────────────┘

Schemat podłączenia do esp32 - obwody niskoprądowe
┌────────────────────────────────────────────────────────────────────────────┐
│                           ESP32 DEV BOARD                                  │
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                            GPIO PINS                                │   │
│  │                                                                     │   │
│  │  GPIO0  ──┬── [Przycisk RESET WiFi] ─── GND                         │   │
│  │           │   (wciśnięty = LOW)                                     │   │
│  │           │                                                         │   │
│  │           └── rezystor 10kΩ pull-up (wewnętrzny)                    │   │
│  │                                                                     │   │
│  │  GPIO2  ──┬── [Rezystor 220Ω] ──┬── [+] LED ALARM                   │   │
│  │           │                      (anoda)                            │   │
│  │           └──────────────────────┴── [-] LED ALARM ─── GND          │   │
│  │                                                                     │   │
│  │  GPIO4  ──┬── [Rezystor 4.7kΩ pull-up] ─── 3.3V                     │   │
│  │           │                                                         │   │
│  │           └── [DS18B20] ─── GND                                     │   │
│  │                   │                                                 │   │
│  │                   └── VDD ─── 3.3V                                  │   │
│  │                                                                     │   │
│  │  GPIO5  ──┬── [PC817 + rel12V] ─┬── [+] stycznik                    │   │
│  │           │                      |                                  │   │
│  │           └──────────────────────┴── [-]                            │   │
│  │                                                                     │   │
│  │  GPIO13 ──┬── [Rezystor 220Ω] ──┬── [+] OPTOGRZALKA1 (dioda LED)    │   │
│  │           │                      (anoda optotriaka)                 │   │
│  │           └──────────────────────┴── [-] OPTOGRZALKA1 ─── GND       │   │
│  │                                                                     │   │
│  │  GPIO14 ──┬── [Rezystor 220Ω] ──┬── [+] OPTOGRZALKA2                │   │
│  │           └──────────────────────┴── [-] OPTOGRZALKA2 ─── GND       │   │
│  │                                                                     │   │
│  │  GPIO15 ──┬── [Rezystor 220Ω] ──┬── [+] LED AL1                     │   │
│  │           └──────────────────────┴── [-] LED AL1 ─── GND            │   │
│  │                                                                     │   │
│  │  GPIO18 ──┬── [Rezystor 220Ω] ──┬── [+] LED GRZALKA2                │   │
│  │           └──────────────────────┴── [-] LED GRZALKA2 ─── GND       │   │
│  │                                                                     │   │
│  │  GPIO19 ──┬── [Rezystor 220Ω] ──┬── [+] LED GRZALKA3                │   │
│  │           └──────────────────────┴── [-] LED GRZALKA3 ─── GND       │   │
│  │                                                                     │   │
│  │  GPIO27 ──┬── [Rezystor 220Ω] ──┬── [+] OPTOGRZALKA3                │   │
│  │           └──────────────────────┴── [-] OPTOGRZALKA3 ─── GND       │   │
│  │                                                                     │   │ 
│  │                                                                     │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                           POWER PINS                                │   │
│  │                                                                     │   │
│  │  3.3V ──┬── DS18B20 VDD                                             │   │
│  │         ├── Rezystor pull-up 4.7kΩ dla GPIO4                        │   │
│  │         └── Dzielnik napięcia dla SCT013 (przez 10kΩ)               │   │
│  │                                                                     │   │
│  │  GND  ──┬── DS18B20 GND                                             │   │
│  │         ├── Przycisk RESET WiFi                                     │   │
│  │         ├── Wszystkie katody LED                                    │   │
│  │         ├── Optotriaki (katody diod LED)                            │   │
│  │         └── Dzielnik napięcia SCT013 (przez 10kΩ)                   │   │
│  │                                                                     │   │
│  │  VIN (5V) ── (opcjonalnie dla zewnętrznych urządzeń 5V)             │   │
│  │                                                                     │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────┘


Przycisk RESET WiFi	GPIO0	Drugi koniec do GND. Używa wewnętrznego pull-up.
LED ALARM	GPIO2	Przez rezystor 220Ω. Katoda do GND.
DS18B20 (1-Wire)	GPIO4	VDD → 3.3V, GND → GND. Rezystor 4.7kΩ między GPIO4 a 3.3V.
LED GRZALKA1	GPIO5	Przez rezystor 220Ω. Katoda do GND.
Optotriak GRZALKA1	GPIO13	Przez rezystor 220Ω. Katoda do GND.
Optotriak GRZALKA2	GPIO14	Przez rezystor 220Ω. Katoda do GND.
LED AL1	GPIO15	Przez rezystor 220Ω. Katoda do GND.
LED GRZALKA2	GPIO18	Przez rezystor 220Ω. Katoda do GND.
LED GRZALKA3	GPIO19	Przez rezystor 220Ω. Katoda do GND.
Optotriak GRZALKA3	GPIO27	Przez rezystor 220Ω. Katoda do GND.
SCT013 (prąd AC)	GPIO34 (ADC1)	Wymaga dzielnika napięcia (2x10kΩ) i kondensatora 10µF.

podłączenie czujnika prądu SCT013

                     ESP32 3.3V
                         │
                         ├──[10kΩ]──┐
                         │          │
                    ┌────┴──────────┼────┐
                    │               │    │
                    │            ┌──┴──┐ │
                    │            │ 10µF│ │
                    │            │  │  │ │
                    │            └──┬──┘ │
                    │               │    │
                    │    SCT013     │    │
                    │    (wyjście)  │    │
                    └──────┬────────┼────┘
                           │        │
                         ┌─┴────────┴─┐
                         │  10kΩ      │
                         └─────┬──────┘
                               │
                             ESP32 GND