# Code Index - Arduino Temperatuur Cyclus Controller

**Versie:** 3.85  
**Platform:** ESP32 (CYD 2.8 inch display)  
**Hardware:** MAX6675 thermocouple, LVGL graphics, touchscreen, SSR relais

---

## 1. Modules/Bestanden

### Hoofdbestand
- **`3_5_Display_Temperature_MAX6675_2SSR.ino`** (2820 regels)
  - Monolithisch bestand met alle functionaliteit
  - Geen modulaire structuur - alles in één bestand

### Ondersteunende bestanden
- **`CHANGELOG.md`** - Versiegeschiedenis en wijzigingen
- **`MAX6675_ANALYSE.md`** - MAX6675 sensor analyse
- **`MAX6675_LIBRARY_REVIEW.md`** - Library review documentatie
- **`meterkast-v2-072955b2cb25.json`** - Google Sheets service account credentials

### Externe Libraries
- **LVGL** (Light and Versatile Graphics Library) - GUI framework
- **TFT_eSPI** - Display driver
- **XPT2046_Touchscreen** - Touchscreen driver
- **ESP_Google_Sheet_Client** - Google Sheets API client
- **WiFiManager** - WiFi configuratie (AP mode)
- **MAX6675** (Rob Tillaart) - Thermocouple sensor library
- **FreeRTOS** - Multi-tasking framework
- **Preferences** - Non-volatile storage

---

## 2. Globale Structs & Flags

### Data Structs

```cpp
// Logging request struct (voor FreeRTOS queue)
typedef struct {
  char status[LOG_STATUS_MAX_LEN];        // 50 bytes
  float temp;                             // 4 bytes
  int cyclus_teller;                      // 4 bytes
  int cyclus_max;                         // 4 bytes
  float T_top;                            // 4 bytes
  float T_bottom;                         // 4 bytes
  char fase_tijd[LOG_FASE_TIJD_MAX_LEN];  // 10 bytes
  char cyclus_tijd[LOG_CYCLUS_TIJD_MAX_LEN]; // 10 bytes
  unsigned long timestamp_ms;             // 4 bytes
} LogRequest;                             // Totaal: ~90 bytes
```

### Systeem Status Flags

```cpp
// Cyclus controle
bool cyclus_actief = false;              // Of er een cyclus draait
bool verwarmen_actief = true;            // true = verwarmen, false = koelen
bool systeem_uit = false;                // Of het systeem uitgeschakeld is
bool koelingsfase_actief = false;        // Veiligheidskoeling bij STOP

// Google Sheets authenticatie
bool g_googleAuthTokenReady = false;     // Token authenticatie status

// Grafiek status
bool graph_data_ready = false;           // Of grafiek data beschikbaar is
bool graph_force_rebuild = false;        // Flag om grafiek opnieuw op te bouwen
```

### Timing Variabelen

```cpp
// Fase timers
unsigned long verwarmen_start_tijd = 0;   // Starttijd opwarmen
unsigned long koelen_start_tijd = 0;     // Starttijd afkoelen
unsigned long veiligheidskoeling_start_tijd = 0;
unsigned long veiligheidskoeling_naloop_start_tijd = 0;

// Fase duur opslag
unsigned long last_opwarmen_duur = 0;     // Duur van laatste opwarmen fase
unsigned long last_koelen_duur = 0;      // Duur van laatste koelen fase
unsigned long last_opwarmen_start_tijd = 0;
unsigned long last_koelen_start_tijd = 0;

// Beveiliging tracking
unsigned long gemiddelde_opwarmen_duur = 0; // Gemiddelde opwarmtijd
int opwarmen_telling = 0;                // Aantal opwarmfases
unsigned long stagnatie_start_tijd = 0;  // Starttijd temperatuur stagnatie

// NTP synchronisatie
unsigned long ntp_sync_time_ms = 0;       // millis() waarde bij NTP sync
time_t ntp_sync_unix_time = 0;          // Unix tijd bij NTP sync
```

### Temperatuur Variabelen

```cpp
// Temperatuur cache
volatile float g_currentTempC = NAN;     // Huidige temperatuur (raw)
static float g_avgTempC = NAN;            // Mediaan temperatuur (voor display)
static float g_lastValidTempC = NAN;      // Laatste geldige waarde
float last_transition_temp = NAN;        // Temp bij faseovergang

// Mediaan berekening (circulaire array)
static float g_tempSamples[TEMP_MEDIAN_SAMPLES]; // 7 samples
static int g_tempSampleIndex = 0;        // Huidige index
static int g_tempSampleCount = 0;        // Aantal geldige metingen

// Stagnatie detectie
float laatste_temp_voor_stagnatie = NAN; // Laatste temp voor stagnatie check
```

### Grafiek Data (Circulaire Array)

```cpp
float* graph_temps = nullptr;            // Temperatuur waarden (120 punten)
unsigned long* graph_times = nullptr;    // Tijdstempels (120 punten)
int graph_write_index = 0;               // Write index (0-119, wrapt rond)
int graph_count = 0;                     // Aantal punten in buffer (0-120)
unsigned long graph_last_log_time = 0;  // Tijd van laatste grafiek log
```

### Instellingen

```cpp
float T_top = 80.0;                      // Bovenste temperatuur (default)
float T_bottom = 25.0;                  // Onderste temperatuur (default)
int cyclus_teller = 1;                  // Huidige cyclus (start bij 1)
int cyclus_max = 0;                     // Max cycli (0 = oneindig)
float temp_offset = 0.0;                // Kalibratie offset
```

### FreeRTOS Objects

```cpp
QueueHandle_t logQueue = nullptr;       // FreeRTOS queue voor logging
TaskHandle_t loggingTaskHandle = nullptr; // Task handle
```

### LVGL Objects

```cpp
lv_obj_t* screen_main = nullptr;        // Hoofdscherm
lv_obj_t* screen_graph = nullptr;      // Grafiek scherm
lv_chart_t* chart = nullptr;           // Grafiek object
lv_chart_series_t* chart_series_rising = nullptr;  // Rode serie (stijgend)
lv_chart_series_t* chart_series_falling = nullptr; // Blauwe serie (dalend)
// ... vele andere LVGL objecten voor labels, knoppen, etc.
```

---

## 3. Hoofd Dataflow

### Initialisatie Flow (`setup()`)

```
1. Hardware Initialisatie
   ├─ SPI.begin()
   ├─ thermocouple.begin()
   ├─ GPIO pins (RELAIS_KOELEN, RELAIS_VERWARMING)
   └─ MAX6675 warm-up (500ms + 1000ms + 3x conversie)

2. Memory Allocatie
   ├─ allocate_buffers()
   │   ├─ draw_buf (LVGL display buffer)
   │   ├─ graph_temps[120] (float array)
   │   └─ graph_times[120] (unsigned long array)
   └─ init_graph_data()

3. LVGL Initialisatie
   ├─ lv_init()
   ├─ TFT display setup (240x320, rotation 270°)
   ├─ Touchscreen setup (XPT2046)
   └─ GUI creatie (lv_create_main_gui())

4. Settings Load
   └─ loadSettings() (Preferences: T_top, T_bottom, cyclus_max, temp_offset)

5. Network Initialisatie
   ├─ WiFiManager.autoConnect()
   ├─ NTP tijd synchronisatie (configTime)
   └─ Google Sheets authenticatie (sheet.begin())

6. FreeRTOS Setup
   ├─ xQueueCreate() (logQueue, 20 entries)
   └─ xTaskCreatePinnedToCore() (loggingTask op Core 1, prioriteit 1)
```

### Main Loop Flow (`loop()`)

```
loop() [Core 1]
│
├─ yield() [Watchdog feed]
│
├─ lv_task_handler() [2x voor responsiviteit]
│
├─ Google Sheets feedback verwerking
│   └─ showGSSuccessCheckmark() (groen vinkje)
│
├─ sampleMax6675() [Elke 285ms]
│   ├─ readTempC() [met retry]
│   ├─ Voeg toe aan circulaire array (g_tempSamples[7])
│   ├─ Bereken mediaan (calculateMedianFromArray)
│   └─ Update g_avgTempC, g_currentTempC
│
├─ cyclusLogica() [Cyclus state machine]
│   ├─ handleSafetyCooling() [als koelingsfase_actief]
│   │   ├─ Check temp < 35°C
│   │   ├─ Start naloop timer (2 minuten)
│   │   └─ Schakel uit na naloop
│   │
│   ├─ handleHeatingPhase() [als verwarmen_actief]
│   │   ├─ Beveiliging: opwarmtijd > 2x gemiddelde?
│   │   ├─ Beveiliging: temperatuur stagnatie > 2 min?
│   │   ├─ Check temp >= T_top
│   │   ├─ Log "Opwarmen tot Afkoelen"
│   │   └─ Schakel naar koelen
│   │
│   └─ handleCoolingPhase() [als !verwarmen_actief]
│       ├─ Check temp <= T_bottom
│       ├─ cyclus_teller++
│       ├─ Log "Afkoelen tot Opwarmen"
│       ├─ Check cyclus_max bereikt?
│       └─ Schakel naar verwarmen
│
├─ log_graph_data() [Elke 5 seconden]
│   ├─ getMedianTemp()
│   ├─ Schrijf naar graph_temps[graph_write_index]
│   ├─ Update graph_write_index (circular)
│   └─ update_graph_display() [als screen_graph actief]
│
├─ updateGUI() [Elke 300ms, alleen screen_main]
│   ├─ Temperatuur display (kleurcodering)
│   ├─ Fase timers (m:ss formaat)
│   ├─ Status tekst
│   └─ Cyclus teller
│
└─ update_graph_display() [Elke 500ms, alleen screen_graph]
    ├─ fill_chart_completely() [als graph_force_rebuild]
    └─ Voeg nieuwe punten toe [tijd-gebaseerd]
```

### Logging Flow (FreeRTOS Task op Core 1)

```
loggingTask() [Core 1, prioriteit 1]
│
├─ xQueueReceive(logQueue, &req, 10ms timeout)
│
├─ Rate limiting (min 2 seconden tussen logs)
│
├─ logToGoogleSheet_internal(&req)
│   ├─ WiFi status check
│   ├─ Token check (g_googleAuthTokenReady)
│   ├─ Timestamp generatie (getTimestampFromMillisChar)
│   ├─ FirebaseJson objecten maken
│   │   ├─ valueRange.add() [9 kolommen]
│   │   └─ sheet.values.append() [met retry, max 3 pogingen]
│   └─ g_logSuccessFlag = true [voor visuele feedback]
│
└─ Token refresh (elke 10 seconden)
    └─ sheet.ready() [non-blocking]
```

### Temperatuur Meting Flow

```
sampleMax6675() [Elke 285ms]
│
├─ readTempC() [met retry mechanisme]
│   ├─ readTempC_single() [3x retry bij fout]
│   │   ├─ Conversietijd check (220ms minimum)
│   │   ├─ thermocouple.read()
│   │   ├─ Open circuit detectie (bit 2)
│   │   ├─ Data validatie (-200°C tot 1200°C)
│   │   └─ Kalibratie offset toepassen
│   └─ Return temperatuur of NAN
│
├─ Voeg toe aan circulaire array
│   └─ g_tempSamples[g_tempSampleIndex] = temp
│
├─ Bereken mediaan
│   └─ calculateMedianFromArray() [insertion sort]
│
└─ Update globale variabelen
    ├─ g_currentTempC = temp
    ├─ g_avgTempC = mediaan
    └─ g_lastValidTempC = temp [als geldig]
```

### Grafiek Update Flow

```
update_graph_display() [Elke 500ms of bij nieuwe data]
│
├─ graph_force_rebuild check
│   └─ fill_chart_completely()
│       ├─ clear_chart()
│       ├─ Loop door alle punten (chronologisch)
│       └─ add_chart_point_with_trend() [kleurcodering]
│
└─ Continue update (tijd-gebaseerd)
    ├─ Loop door alle punten
    ├─ Check graph_times[i] > last_displayed_time
    ├─ Voeg nieuwe punten toe
    └─ Update last_displayed_time
```

---

## 4. Netwerk / UI / Alert Engine / Warm-Start

### Netwerk (WiFi & Google Sheets)

**WiFi Manager:**
- **AP Mode:** "ESP32-TemperatuurCyclus" (bij geen credentials)
- **Timeout:** 180 seconden
- **Auto-connect:** Met opgeslagen credentials
- **Status feedback:** Via LVGL labels (grijs/rood)

**Google Sheets API:**
- **Service Account:** meterkast-datalogging@meterkast-v2.iam.gserviceaccount.com
- **Spreadsheet ID:** 1y3iIj6bsRgDEx1ZMGhXvuBZU5Nw1GjQsqsm4PQUTFy0
- **Tabblad:** "DataLog-K"
- **Kolommen:** Timestamp, Meettemperatuur, Status, Huidige cyclus, Totaal cycli, T_top, T_dal, Fase tijd, Cyclus tijd
- **Token refresh:** Elke 10 minuten (prerefresh)
- **Rate limiting:** Minimaal 2 seconden tussen logs
- **Retry mechanisme:** Max 3 pogingen met exponential backoff (500ms, 1000ms)

**NTP Synchronisatie:**
- **Server:** pool.ntp.org
- **Timezone:** GMT+1 (3600 seconden offset)
- **Sync tracking:** ntp_sync_time_ms, ntp_sync_unix_time
- **Timestamp berekening:** millis() → Unix tijd via offset

### UI (LVGL Graphics)

**Schermen:**
- **screen_main:** Hoofdscherm met temperatuur, timers, knoppen
- **screen_graph:** Grafiek scherm (280x155px, 120 punten)

**Hoofdscherm Componenten:**
- Temperatuur display (groot, kleurcodering: groen<37°C, rood≥37°C)
- Opwarmen/Afkoelen timers (m:ss formaat)
- Status tekst (rechts uitgelijnd, 230px breed)
- Cyclus teller (xxx/yyy of xxx/inf)
- T_top/T_bottom instellingen (+/- knoppen)
- START/STOP/GRAFIEK knoppen (dynamische kleuren)
- Versienummer (rechter onderhoek)
- WiFi/Google Sheets status labels

**Grafiek Scherm:**
- LVGL chart object (280x155px)
- 2 series: rood (stijgend), blauw (dalend)
- Dynamische Y-as labels (20°C tot T_top, 6 labels)
- X-as minuten labels (9-0)
- Real-time updates (direct bij nieuwe data + backup elke 500ms)

**Touch Events:**
- `start_button_event()` - Start cyclus
- `stop_button_event()` - Stop cyclus (met veiligheidskoeling)
- `graph_button_event()` - Schakel naar grafiek scherm
- `back_button_event()` - Terug naar hoofdscherm
- `t_top_plus/minus_event()` - T_top aanpassen
- `t_bottom_plus/minus_event()` - T_bottom aanpassen
- `temp_plus/minus_event()` - Kalibratie offset aanpassen

**Responsiviteit:**
- Meerdere `lv_task_handler()` calls per loop iteratie
- Touch events verwerkt tijdens delays
- Watchdog feeding tussen LVGL operaties

### Alert Engine (Beveiligingen)

**1. Opwarmtijd Beveiliging:**
- **Trigger:** Opwarmtijd > 2x gemiddelde opwarmtijd
- **Actie:** Veiligheidskoeling + systeem uit
- **Logging:** "Beveiliging: Opwarmen te lang"
- **Tracking:** Exponentiële moving average (70% oude + 30% nieuwe)

**2. Temperatuur Stagnatie Beveiliging:**
- **Trigger:** Temperatuur binnen 3°C bandbreedte voor > 2 minuten
- **Actie:** Veiligheidskoeling + systeem uit
- **Logging:** "Beveiliging: Temperatuur stagnatie"
- **Reset:** Bij faseovergang, START, of temp buiten bandbreedte

**3. Veiligheidskoeling:**
- **Trigger:** STOP bij temp > 35°C, of beveiligingsacties
- **Actie:** Koelen tot < 35°C + 2 minuten naloop
- **Logging:** "Veiligheidskoeling" + "Uit"
- **Naloop:** 2 minuten extra koeling (voor inductie-verwarming restwarmte)

**4. Open Circuit Detectie:**
- **Trigger:** MAX6675 bit 2 (thermocouple open)
- **Actie:** Return NAN, skip meting
- **Validatie:** -200°C tot 1200°C bereik check

### Warm-Start (Non-Volatile Storage)

**Preferences (ESP32 NVS):**
- **Namespace:** "tempctrl"
- **Keys:**
  - `t_top` (float) - Bovenste temperatuur
  - `t_bottom` (float) - Onderste temperatuur
  - `cyclus_max` (int) - Maximaal aantal cycli
  - `temp_offset` (float) - Kalibratie offset

**Load Flow (`loadSettings()`):**
```
1. preferences.begin("tempctrl", true) [read-only]
2. T_top = preferences.getFloat("t_top", 80.0)
3. T_bottom = preferences.getFloat("t_bottom", 25.0)
4. cyclus_max = preferences.getInt("cyclus_max", 0)
5. temp_offset = preferences.getFloat("temp_offset", 0.0)
6. thermocouple.setOffset(temp_offset)
7. preferences.end()
```

**Save Flow (`saveSettings()`):**
```
1. preferences.begin("tempctrl", false) [read-write]
2. preferences.putFloat("t_top", T_top)
3. preferences.putFloat("t_bottom", T_bottom)
4. preferences.putInt("cyclus_max", cyclus_max)
5. preferences.putFloat("temp_offset", temp_offset)
6. preferences.end()
```

**Auto-save:** Bij elke instelling wijziging (via `saveAndLogSetting()`)

---

## 5. Memory Hotspots

### Heap Allocaties

**1. Display Buffer (draw_buf):**
```cpp
size_t draw_buf_bytes = SCREEN_WIDTH * SCREEN_HEIGHT / 10 * 2;
draw_buf = (uint32_t*)malloc(draw_buf_bytes);
// ~15,360 bytes (240 * 320 / 10 * 2)
```

**2. Grafiek Buffers:**
```cpp
graph_temps = (float*)malloc(GRAPH_POINTS * sizeof(float));
graph_times = (unsigned long*)malloc(GRAPH_POINTS * sizeof(unsigned long));
// ~960 bytes (120 * 4 + 120 * 4)
```

**3. FreeRTOS Queue:**
```cpp
logQueue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogRequest));
// ~1,800 bytes (20 * 90 bytes)
```

**4. FirebaseJson Objects (tijdelijk):**
```cpp
FirebaseJson response;      // Stack allocatie
FirebaseJson valueRange;    // Stack allocatie
// Heap fragmentatie mogelijk tijdens JSON operaties
// Scope-based cleanup voorkomt memory leaks
```

**5. LVGL Objects:**
- Alle LVGL objecten worden intern gealloceerd door LVGL
- Automatisch cleanup bij object verwijdering
- Display buffer is grootste allocatie

### Stack Allocaties

**1. Logging Task Stack:**
```cpp
xTaskCreatePinnedToCore(..., 16384, ...); // 16KB stack
// Verhoogd voor FirebaseJson operaties
```

**2. Main Loop Stack:**
- Default ESP32 loop stack (~8KB)
- Geen expliciete stack allocatie

**3. Local Buffers:**
```cpp
char timestamp[20];           // Stack
char totaal_cycli[16];       // Stack
char fase_tijd_str[10];      // Stack
char cyclus_tijd_str[10];    // Stack
// Veel char arrays gebruikt i.p.v. String (voorkomt heap fragmentatie)
```

### Memory Management Strategieën

**1. Char Arrays i.p.v. String:**
- Voorkomt heap fragmentatie
- Gebruikt stack of static storage
- Expliciete null-termination

**2. Scope-based Cleanup:**
```cpp
{
  FirebaseJson response;
  FirebaseJson valueRange;
  // ... operaties
} // Automatische destructor cleanup
```

**3. Watchdog Feeding:**
- `yield()` en `vTaskDelay()` tussen heap operaties
- Voorkomt watchdog timeouts tijdens allocaties

**4. Queue Overflow Protection:**
```cpp
if (queue_count >= LOG_QUEUE_SIZE - 1) {
  // Verwijder oude entries (LOG_QUEUE_CLEANUP_COUNT = 5)
}
```

**5. Buffer Bounds Checking:**
```cpp
strncpy(req.status, status, LOG_STATUS_MAX_LEN - 1);
req.status[LOG_STATUS_MAX_LEN - 1] = '\0';
```

### Memory Hotspots (Risico Gebieden)

**1. FirebaseJson Operaties:**
- **Locatie:** `logToGoogleSheet_internal()`
- **Risico:** Heap fragmentatie tijdens JSON build
- **Mitigatie:** Scope-based cleanup, watchdog feeding, stack allocatie waar mogelijk

**2. LVGL Display Buffer:**
- **Locatie:** `allocate_buffers()`
- **Grootte:** ~15KB (grootste allocatie)
- **Risico:** Out of memory bij startup
- **Mitigatie:** Foutafhandeling met while(1) loop

**3. Grafiek Data:**
- **Locatie:** `graph_temps[]`, `graph_times[]`
- **Grootte:** ~960 bytes
- **Risico:** Laag (kleine allocatie)

**4. FreeRTOS Queue:**
- **Locatie:** `logQueue`
- **Grootte:** ~1.8KB
- **Risico:** Queue overflow (mitigatie: cleanup bij overflow)

**5. Stack Overflow Risico:**
- **Locatie:** Logging task (16KB stack)
- **Risico:** FirebaseJson operaties kunnen diep stack gebruiken
- **Mitigatie:** Verhoogde stack size (16384 bytes)

### Memory Monitoring

**Geen expliciete memory monitoring**, maar:
- Watchdog feeding tussen allocaties
- Foutafhandeling bij allocatie failures
- Queue overflow detection
- Scope-based cleanup voor automatische destructors

---

## Architectuur Overzicht

### Core Affinity

- **Core 0:** WiFi stack, systeemtaken (real-time)
- **Core 1:** Main loop, logging task (lagere prioriteit)

### Task Prioriteiten

- **Main Loop:** Default prioriteit (1)
- **Logging Task:** Prioriteit 1 (verlaagd voor knopbediening responsiviteit)
- **IDLE Task:** Prioriteit 0 (FreeRTOS default)

### Timing Constraints

- **Temperatuur sampling:** 285ms (MAX6675 conversietijd respect)
- **Display update:** 285ms (synchroon met sampling)
- **Grafiek logging:** 5000ms (5 seconden)
- **Grafiek display update:** 500ms (real-time backup)
- **GUI update:** 300ms
- **Logging rate limit:** 2000ms (minimaal tussen logs)
- **Token refresh:** 600 seconden (10 minuten)

---

**Laatste update:** Versie 3.85

