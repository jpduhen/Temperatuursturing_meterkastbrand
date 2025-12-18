# Code Index - Arduino Temperatuur Cyclus Controller

**Versie:** 4.01  
**Platform:** ESP32 (CYD 2.8 inch display)  
**Hardware:** MAX6675 thermocouple, LVGL graphics, touchscreen, SSR relais  
**Architectuur:** Modulair (8 modules)

---

## 1. Modules/Bestanden

### Hoofdbestand
- **`3_5_Display_Temperature_MAX6675_2SSR.ino`** (~2496 regels)
  - Orchestratie laag: initialisatie, module integratie, callbacks
  - Bevat nog enkele helper functies voor backward compatibility
  - Grafiek data management (circulaire arrays)
  - WiFi/Network setup

### Module Structuur

#### 1. **SystemClock** (`src/SystemClock/`)
- **Bestanden:** `SystemClock.h`, `SystemClock.cpp`
- **Functionaliteit:**
  - NTP tijd synchronisatie
  - Timestamp conversie (millis() → Unix tijd → [yy-mm-dd hh:mm:ss])
  - Tijdzone support (GMT offset)
- **Interface:**
  - `begin(int timezoneOffset)` - Initialiseer met tijdzone
  - `sync()` - Synchroniseer met NTP server
  - `getTimestamp(char* buffer, size_t bufferSize)` - Huidige tijd als string
  - `getTimestampFromMillis(char* buffer, size_t bufferSize, unsigned long timestampMs)` - Converteer millis naar timestamp

#### 2. **SettingsStore** (`src/SettingsStore/`)
- **Bestanden:** `SettingsStore.h`, `SettingsStore.cpp`
- **Functionaliteit:**
  - Non-volatile opslag via ESP32 Preferences
  - Settings en Google credentials beheer
  - Type-safe settings management
- **Data Structs:**
  ```cpp
  struct Settings {
      float tTop;
      float tBottom;
      int cycleMax;
      float tempOffset;
  };
  
  struct GoogleCredentials {
      char clientEmail[128];
      char projectId[64];
      char privateKey[2048];
      char spreadsheetId[128];
  };
  ```
- **Interface:**
  - `load()` - Laad settings uit Preferences
  - `save(const Settings&)` - Sla settings op
  - `saveAndLog(const Settings&, const char* logMsg)` - Sla op en log
  - `loadGoogleCredentials()` - Laad Google credentials
  - `saveGoogleCredentials(const GoogleCredentials&)` - Sla credentials op

#### 3. **TempSensor** (`src/TempSensor/`)
- **Bestanden:** `TempSensor.h`, `TempSensor.cpp`
- **Functionaliteit:**
  - MAX6675 sensor communicatie (software SPI)
  - Mediaan filter (laatste 7 metingen, circulaire array)
  - Open-circuit detectie
  - Temperatuur offset correctie
  - Majority voting voor kritieke metingen
  - Conversietijd respectering (250ms minimum)
- **Interface:**
  - `begin()` - Initialiseer sensor
  - `setOffset(float offset)` - Stel kalibratie offset in
  - `sample()` - Voer temperatuurmeting uit (aanroepen vanuit loop)
  - `getCurrent()` - Huidige temperatuur (raw)
  - `getMedian()` - Mediaan temperatuur (voor display)
  - `getCritical()` - Kritieke meting (majority voting, 3 samples)
  - `getLastValid()` - Laatste geldige waarde

#### 4. **Logger** (`src/Logger/`)
- **Bestanden:** `Logger.h`, `Logger.cpp`
- **Functionaliteit:**
  - FreeRTOS logging task op Core 1
  - Google Sheets API integratie
  - Rate limiting (min 2 seconden tussen logs)
  - Retry mechanisme (max 3 pogingen)
  - Token refresh management
- **Data Struct:**
  ```cpp
  struct LogRequest {
      char status[50];
      float temp;
      int cyclus_teller;
      int cyclus_max;
      float T_top;
      float T_bottom;
      char fase_tijd[10];
      char cyclus_tijd[10];
      unsigned long timestamp_ms;
  };
  ```
- **Interface:**
  - `begin(clientEmail, projectId, privateKey, spreadsheetId, systemClock)` - Initialiseer met credentials
  - `log(const LogRequest&)` - Voeg log entry toe aan queue
  - `isTokenReady()` - Check of Google token klaar is
  - `hasLogSuccess()` - Check of laatste log succesvol was
  - `getLogSuccessTime()` - Tijd van laatste succesvolle log

#### 5. **CycleController** (`src/CycleController/`)
- **Bestanden:** `CycleController.h`, `CycleController.cpp`
- **Functionaliteit:**
  - Opwarmen/afkoelen state machine
  - Beveiligingen (opwarmtijd, temperatuur stagnatie)
  - Veiligheidskoeling met naloop
  - Cyclusteller management
  - Relais besturing
- **Interface:**
  - `begin(tempSensor, logger, relaisKoelenPin, relaisVerwarmingPin)` - Initialiseer
  - `update()` - Update state machine (aanroepen vanuit loop)
  - `start()` - Start cyclus
  - `stop()` - Stop cyclus
  - `reset()` - Reset state
  - `setTargetTop(float)`, `setTargetBottom(float)`, `setMaxCycles(int)` - Settings
  - `setTransitionCallback(TransitionCallback)` - Callback voor faseovergangen
  - Getters: `isActive()`, `isHeating()`, `isSystemOff()`, `isSafetyCooling()`, etc.

#### 6. **UIController** (`src/UIController/`)
- **Bestanden:** `UIController.h`, `UIController.cpp`
- **Functionaliteit:**
  - LVGL schermen (hoofdscherm, grafiekscherm)
  - Touch events afhandeling
  - Temperatuur display met kleurcodering
  - Grafiek updates (real-time)
  - Status labels (WiFi, Google Sheets, AP)
- **Interface:**
  - `begin(firmwareVersionMajor, firmwareVersionMinor)` - Initialiseer UI
  - `update()` - Update displays (aanroepen vanuit loop)
  - `logGraphData()` - Log grafiek data (aanroepen vanuit loop)
  - `updateGraph()` - Update grafiek display
  - `showGSStatus(const char* text, bool success)` - Toon Google Sheets status
  - `hideInitStatus()` - Verberg initialisatie status
  - Callbacks: `setStartCallback()`, `setStopCallback()`, `setSettingChangeCallback()`, etc.

#### 7. **NtfyNotifier** (`src/NtfyNotifier/`)
- **Bestanden:** `NtfyNotifier.h`, `NtfyNotifier.cpp`, `README_NTFY.md`
- **Functionaliteit:**
  - Push notificaties via NTFY.sh service
  - Automatische notificaties bij logging events
  - Configureerbare melding types (per type in/uitschakelbaar)
  - Topic-based notificaties
  - HTTP client voor NTFY.sh API
- **Data Structs:**
  ```cpp
  enum class NtfyNotificationType {
      LOG_INFO, LOG_START, LOG_STOP, LOG_TRANSITION,
      LOG_SAFETY, LOG_ERROR, LOG_WARNING
  };
  
  struct NtfyNotificationSettings {
      bool enabled;
      bool logInfo, logStart, logStop, logTransition;
      bool logSafety, logError, logWarning;
  };
  ```
- **Interface:**
  - `begin(const char* topic)` - Initialiseer met topic naam
  - `setTopic(const char* topic)` - Wijzig topic naam
  - `setSettings(const NtfyNotificationSettings&)` - Stel melding instellingen in
  - `send(title, message, type, colorTag)` - Verstuur notificatie
  - Helper functies: `sendInfo()`, `sendStart()`, `sendStop()`, `sendTransition()`, `sendSafety()`, `sendError()`, `sendWarning()`
  - `isEnabled()` - Check of NTFY is ingeschakeld

#### 8. **WebServer** (`src/WebServer/`)
- **Bestanden:** `WebServer.h`, `WebServer.cpp`
- **Functionaliteit:**
  - HTTP webserver voor configuratie
  - Real-time status API (JSON)
  - Instellingen beheer (HTML formulier)
  - START/STOP controle
  - Google Sheets credentials beheer
  - NTFY notificatie configuratie
- **Interface:**
  - `begin()` - Start webserver
  - `handleClient()` - Verwerk HTTP requests (aanroepen vanuit loop)
  - Callbacks: `setStartCallback()`, `setStopCallback()`, `setSettingsChangeCallback()`
  - Status getters: `setGetCurrentTempCallback()`, `setIsActiveCallback()`, etc.
  - NTFY callbacks: `setGetNtfyTopicCallback()`, `setGetNtfySettingsCallback()`, `setSaveNtfySettingsCallback()`
- **Endpoints:**
  - `GET /` - HTML configuratie pagina
  - `GET /settings` - Huidige instellingen (JSON, inclusief NTFY)
  - `GET /status` - Real-time systeemstatus (JSON)
  - `POST /start` - Start systeem
  - `POST /stop` - Stop systeem
  - `POST /save` - Sla instellingen op (inclusief NTFY)

### Ondersteunende bestanden
- **`CHANGELOG.md`** - Versiegeschiedenis en wijzigingen
- **`README.md`** - Project documentatie
- **`REFACTORING_PLAN.md`** - Refactoring plan (historisch)
- **`CODE_INDEX.md`** - Dit document
- **`WEBSERVER_IMPLEMENTATIE_GIDS.md`** - WebServer implementatie gids
- **`src/NtfyNotifier/README_NTFY.md`** - NTFY Notifier gebruikersgids
- **`MAX6675_ANALYSE.md`** - MAX6675 sensor analyse
- **`MAX6675_LIBRARY_REVIEW.md`** - Library review documentatie

### Externe Libraries
- **LVGL** (Light and Versatile Graphics Library) - GUI framework
- **TFT_eSPI** - Display driver
- **XPT2046_Touchscreen** - Touchscreen driver
- **ESP_Google_Sheet_Client** - Google Sheets API client
- **WiFiManager** - WiFi configuratie (AP mode)
- **MAX6675** (Rob Tillaart) - Thermocouple sensor library
- **FreeRTOS** - Multi-tasking framework
- **Preferences** - Non-volatile storage (via ESP32 Arduino Core)
- **WebServer** - HTTP webserver (via ESP32 Arduino Core)

---

## 2. Globale Structs & Flags

### Data Structs

```cpp
// Settings (SettingsStore module)
struct Settings {
    float tTop;        // Bovenste temperatuur
    float tBottom;     // Onderste temperatuur
    int cycleMax;      // Max cycli (0 = oneindig)
    float tempOffset;  // Kalibratie offset
};

// Google Credentials (SettingsStore module)
struct GoogleCredentials {
    char clientEmail[128];
    char projectId[64];
    char privateKey[2048];
    char spreadsheetId[128];
};

// Logging request (Logger module)
struct LogRequest {
    char status[50];           // Status tekst
    float temp;                // Temperatuur
    int cyclus_teller;         // Huidige cyclus
    int cyclus_max;            // Max cycli
    float T_top;               // T_top instelling
    float T_bottom;            // T_bottom instelling
    char fase_tijd[10];        // Fase tijd [mm:ss]
    char cyclus_tijd[10];      // Cyclus tijd [mm:ss]
    unsigned long timestamp_ms; // Timestamp in millis
};

// NTFY Notification Settings (NtfyNotifier module)
struct NtfyNotificationSettings {
    bool enabled;              // Algemene NTFY functionaliteit
    bool logInfo;              // LOG_INFO meldingen
    bool logStart;             // LOG_START meldingen
    bool logStop;              // LOG_STOP meldingen
    bool logTransition;       // LOG_TRANSITION meldingen
    bool logSafety;            // LOG_SAFETY meldingen
    bool logError;             // LOG_ERROR meldingen
    bool logWarning;           // LOG_WARNING meldingen
};
```

### Systeem Status Flags (in hoofdprogramma)

```cpp
// Cyclus controle (nu in CycleController, maar globaal voor backward compatibility)
bool cyclus_actief = false;        // Of er een cyclus draait
bool verwarmen_actief = true;      // true = verwarmen, false = koelen
bool systeem_uit = false;          // Of het systeem uitgeschakeld is
bool koelingsfase_actief = false;   // Veiligheidskoeling bij STOP

// Grafiek status
bool graph_data_ready = false;     // Of grafiek data beschikbaar is
bool graph_force_rebuild = false;  // Flag om grafiek opnieuw op te bouwen
```

### Timing Variabelen (in CycleController)

```cpp
// Fase timers
unsigned long verwarmen_start_tijd = 0;
unsigned long koelen_start_tijd = 0;
unsigned long veiligheidskoeling_start_tijd = 0;
unsigned long veiligheidskoeling_naloop_start_tijd = 0;

// Fase duur opslag
unsigned long last_opwarmen_duur = 0;
unsigned long last_koelen_duur = 0;
unsigned long last_opwarmen_start_tijd = 0;
unsigned long last_koelen_start_tijd = 0;

// Beveiliging tracking
unsigned long gemiddelde_opwarmen_duur = 0;  // Gemiddelde opwarmtijd
int opwarmen_telling = 0;                    // Aantal opwarmfases
unsigned long stagnatie_start_tijd = 0;      // Starttijd temperatuur stagnatie
float laatste_temp_voor_stagnatie = NAN;     // Laatste temp voor stagnatie check
```

### Temperatuur Variabelen (in TempSensor)

```cpp
// Temperatuur cache
float currentTemp;      // Huidige temperatuur (raw)
float medianTemp;       // Mediaan temperatuur (voor display)
float lastValidTemp;    // Laatste geldige waarde

// Mediaan berekening (circulaire array)
float samples[7];       // 7 samples voor mediaan
int sampleIndex;        // Huidige index
int sampleCount;        // Aantal geldige metingen
```

### Grafiek Data (in hoofdprogramma)

```cpp
float* graph_temps = nullptr;            // Temperatuur waarden (120 punten)
unsigned long* graph_times = nullptr;    // Tijdstempels (120 punten)
int graph_write_index = 0;               // Write index (0-119, wrapt rond)
int graph_count = 0;                     // Aantal punten in buffer (0-120)
unsigned long graph_last_log_time = 0;   // Tijd van laatste grafiek log
```

### Instellingen (in hoofdprogramma, geladen uit SettingsStore)

```cpp
float T_top = 80.0;        // Bovenste temperatuur (default)
float T_bottom = 25.0;     // Onderste temperatuur (default)
int cyclus_teller = 1;     // Huidige cyclus (start bij 1)
int cyclus_max = 0;        // Max cycli (0 = oneindig)
float temp_offset = 0.0;   // Kalibratie offset
```

---

## 3. Hoofd Dataflow

### Initialisatie Flow (`setup()`)

```
1. Hardware Initialisatie
   ├─ SPI.begin()
   ├─ tempSensor.begin() (MAX6675 warm-up)
   ├─ GPIO pins (RELAIS_KOELEN, RELAIS_VERWARMING)
   └─ MAX6675 warm-up (500ms + 1000ms + 3x conversie)

2. Memory Allocatie
   ├─ allocate_buffers()
   │   ├─ draw_buf (LVGL display buffer, ~15KB)
   │   ├─ graph_temps[120] (float array, ~480 bytes)
   │   └─ graph_times[120] (unsigned long array, ~480 bytes)
   └─ init_graph_data()

3. LVGL Initialisatie
   ├─ lv_init()
   ├─ TFT display setup (240x320, rotation 270°)
   ├─ Touchscreen setup (XPT2046)
   └─ uiController.begin() (GUI creatie)

4. Module Initialisatie
   ├─ systemClock.begin(3600) (GMT+1)
   ├─ systemClock.sync() (NTP synchronisatie)
   ├─ settingsStore.begin()
   ├─ Settings settings = settingsStore.load()
   ├─ T_top, T_bottom, cyclus_max, temp_offset = settings
   ├─ tempSensor.setOffset(temp_offset)
   └─ cycleController.begin(tempSensor, logger, RELAIS_KOELEN, RELAIS_VERWARMING)

5. Network Initialisatie
   ├─ WiFi.mode(WIFI_STA) (probeer eerst Station)
   ├─ WiFiManager.autoConnect("ESP32-TC")
   ├─ Als verbonden:
   │   ├─ GoogleCredentials creds = settingsStore.loadGoogleCredentials()
   │   ├─ logger.begin(creds, systemClock)
   │   └─ WiFi.mode(WIFI_AP_STA) (AP+STA mode)
   ├─ WiFi.softAP("ESP32-TC") (Access Point voor web interface)
   └─ webServer.begin() (webserver starten)

6. Callback Setup
   ├─ UIController callbacks (start, stop, settings, etc.)
   ├─ CycleController callbacks (transition logging)
   └─ WebServer callbacks (start, stop, settings, getters)
```

### Main Loop Flow (`loop()`)

```
loop() [Core 1]
│
├─ yield() [Watchdog feed]
│
├─ lv_task_handler() [2x voor responsiviteit]
│
├─ uiController.update() [Elke 300ms]
│   ├─ Temperatuur display (kleurcodering)
│   ├─ Fase timers (m:ss formaat)
│   ├─ Status tekst
│   ├─ Cyclus teller
│   └─ WiFi/Google Sheets status
│
├─ uiController.updateGSStatusReset() [Google Sheets status reset na 1s]
│
├─ tempSensor.sample() [Elke 285ms]
│   ├─ readSingle() [met retry]
│   ├─ Voeg toe aan circulaire array (samples[7])
│   ├─ Bereken mediaan (calculateMedian)
│   └─ Update currentTemp, medianTemp, lastValidTemp
│
├─ cycleController.update() [Cyclus state machine]
│   ├─ handleSafetyCooling() [als koelingsfase_actief]
│   │   ├─ Check temp < 35°C
│   │   ├─ Start naloop timer (2 minuten)
│   │   └─ Schakel uit na naloop (stopAll())
│   │
│   ├─ handleHeating() [als verwarmen_actief]
│   │   ├─ Beveiliging: opwarmtijd > 2x gemiddelde?
│   │   ├─ Beveiliging: temperatuur stagnatie > 2 min? (alleen > 35°C)
│   │   ├─ Check temp >= T_top
│   │   ├─ Log "Opwarmen tot Afkoelen" (via callback)
│   │   └─ Schakel naar koelen
│   │
│   └─ handleCooling() [als !verwarmen_actief]
│       ├─ Check temp <= T_bottom
│       ├─ cyclus_teller++
│       ├─ Log "Afkoelen tot Opwarmen" (via callback)
│       ├─ Check cyclus_max bereikt?
│       └─ Schakel naar verwarmen
│
├─ uiController.logGraphData() [Elke 5 seconden]
│   ├─ getMedianTemp()
│   ├─ Schrijf naar graph_temps[graph_write_index]
│   ├─ Update graph_write_index (circular)
│   └─ uiController.updateGraph() [als screen_graph actief]
│
├─ uiController.updateGraph() [Elke 500ms, alleen screen_graph]
│   ├─ fill_chart_completely() [als graph_force_rebuild]
│   └─ Voeg nieuwe punten toe [tijd-gebaseerd]
│
└─ webServer.handleClient() [HTTP request verwerking]
```

### Logging Flow (FreeRTOS Task op Core 1)

```
Logger::task() [Core 1, prioriteit 1]
│
├─ xQueueReceive(queue, &req, 10ms timeout)
│
├─ Rate limiting (min 2 seconden tussen logs)
│
├─ logInternal(&req)
│   ├─ WiFi status check
│   ├─ Token check (tokenReady)
│   ├─ systemClock.getTimestampFromMillis() (timestamp generatie)
│   ├─ FirebaseJson objecten maken
│   │   ├─ valueRange.add() [9 kolommen]
│   │   └─ sheetClient.values.append() [met retry, max 3 pogingen]
│   └─ logSuccessFlag = true [voor visuele feedback]
│
├─ sendNtfyNotification(&req) [onafhankelijk van Google Sheets succes]
│   ├─ Check ntfyNotifier != nullptr && isEnabled()
│   ├─ Bepaal notificatie type op basis van status
│   │   ├─ "START" → LOG_START
│   │   ├─ "STOP"/"Uit" → LOG_STOP
│   │   ├─ "Veiligheidskoeling" of "Beveiliging:" → LOG_SAFETY
│   │   ├─ "tot" in status → LOG_TRANSITION
│   │   └─ Anders → LOG_INFO
│   ├─ Maak title en message met relevante informatie
│   └─ ntfyNotifier->send(title, message, type)
│
└─ Token refresh (elke 10 seconden)
    └─ sheetClient.ready() [non-blocking, altijd aanroepen]
```

### Temperatuur Meting Flow

```
tempSensor.sample() [Elke 285ms]
│
├─ read() [met retry mechanisme]
│   ├─ readSingle() [3x retry bij fout]
│   │   ├─ Conversietijd check (250ms minimum)
│   │   ├─ sensor.read()
│   │   ├─ Open circuit detectie (bit 2)
│   │   ├─ Data validatie (-200°C tot 1200°C)
│   │   └─ Kalibratie offset toepassen
│   └─ Return temperatuur of NAN
│
├─ Voeg toe aan circulaire array
│   └─ samples[sampleIndex] = temp
│
├─ Bereken mediaan
│   └─ calculateMedian() [insertion sort, chronologisch]
│
└─ Update interne variabelen
    ├─ currentTemp = temp
    ├─ medianTemp = mediaan
    └─ lastValidTemp = temp [als geldig]
```

### Grafiek Update Flow

```
uiController.updateGraph() [Elke 500ms of bij nieuwe data]
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
- **AP Mode:** "ESP32-TC" (bij geen credentials of voor web interface)
- **Station Mode:** Eerst proberen verbinding met opgeslagen credentials
- **AP+STA Mode:** Tegelijkertijd Station (voor Google Sheets) en AP (voor web interface)
- **Timeout:** 180 seconden
- **Auto-connect:** Met opgeslagen credentials
- **Status feedback:** Via LVGL labels (grijs/rood/groen)

**Google Sheets API:**
- **Service Account:** Via GoogleCredentials struct (opgeslagen in Preferences)
- **Spreadsheet ID:** Via GoogleCredentials struct
- **Tabblad:** "DataLog-K"
- **Kolommen:** Timestamp, Meettemperatuur, Status, Huidige cyclus, Totaal cycli, T_top, T_dal, Fase tijd, Cyclus tijd
- **Token refresh:** Elke 10 minuten (prerefresh)
- **Rate limiting:** Minimaal 2 seconden tussen logs
- **Retry mechanisme:** Max 3 pogingen met exponential backoff (500ms, 1000ms)

**NTP Synchronisatie (SystemClock):**
- **Server:** pool.ntp.org
- **Timezone:** GMT+1 (3600 seconden offset)
- **Sync tracking:** Via SystemClock module
- **Timestamp berekening:** millis() → Unix tijd via offset → [yy-mm-dd hh:mm:ss]

**WebServer:**
- **Port:** 80 (HTTP)
- **Mode:** AP+STA (toegankelijk via AP IP of Station IP)
- **Endpoints:** `/`, `/settings`, `/status`, `/start`, `/stop`, `/save`
- **Real-time updates:** Status API elke 2 seconden (JavaScript polling)

### UI (LVGL Graphics via UIController)

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
- Versienummer (rechter onderhoek, y=-50)
- WiFi status (linksonder, y=-50)
- Instellen AP status (linksonder, y=-65): "Instellen AP: ESP32-TC IP: [IP]"
- Google Sheets status (rechtsboven, y=34): "Google: [status]"

**Grafiek Scherm:**
- LVGL chart object (280x155px)
- 2 series: rood (stijgend), blauw (dalend)
- Dynamische Y-as labels (20°C tot T_top, 6 labels)
- X-as minuten labels (9-0)
- Real-time updates (direct bij nieuwe data + backup elke 500ms)

**Touch Events (via UIController callbacks):**
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

### Alert Engine (Beveiligingen in CycleController)

**1. Opwarmtijd Beveiliging:**
- **Trigger:** Opwarmtijd > 2x gemiddelde opwarmtijd
- **Actie:** Veiligheidskoeling + systeem uit (stopAll())
- **Logging:** "Beveiliging: Opwarmen te lang"
- **Tracking:** Exponentiële moving average (70% oude + 30% nieuwe)

**2. Temperatuur Stagnatie Beveiliging:**
- **Trigger:** Temperatuur binnen 3°C bandbreedte voor > 2 minuten (alleen > 35°C)
- **Actie:** Veiligheidskoeling + systeem uit (stopAll())
- **Logging:** "Beveiliging: Temperatuur stagnatie"
- **Reset:** Bij faseovergang, START, of temp buiten bandbreedte

**3. Veiligheidskoeling:**
- **Trigger:** STOP bij temp > 35°C, of beveiligingsacties
- **Actie:** Koelen tot < 35°C + 2 minuten naloop
- **Logging:** "Veiligheidskoeling" + "Uit"
- **Naloop:** 2 minuten extra koeling (voor inductie-verwarming restwarmte)
- **Na naloop:** Systeem volledig uit (geen nieuwe cyclus start)

**4. Open Circuit Detectie (TempSensor):**
- **Trigger:** MAX6675 bit 2 (thermocouple open)
- **Actie:** Return NAN, skip meting
- **Validatie:** -200°C tot 1200°C bereik check

### Warm-Start (Non-Volatile Storage via SettingsStore)

**Preferences (ESP32 NVS):**
- **Namespace:** "tempctrl"
- **Keys:**
  - `t_top` (float) - Bovenste temperatuur
  - `t_bottom` (float) - Onderste temperatuur
  - `cyclus_max` (int) - Maximaal aantal cycli
  - `temp_offset` (float) - Kalibratie offset
  - `client_email` (string) - Google Sheets client email
  - `project_id` (string) - Google Sheets project ID
  - `private_key` (string) - Google Sheets private key
  - `spreadsheet_id` (string) - Google Sheets spreadsheet ID
  - `ntfy_topic` (string) - NTFY topic naam
  - `ntfy_enabled` (bool) - NTFY functionaliteit aan/uit
  - `ntfy_log_info` (bool) - LOG_INFO meldingen
  - `ntfy_log_start` (bool) - LOG_START meldingen
  - `ntfy_log_stop` (bool) - LOG_STOP meldingen
  - `ntfy_log_transition` (bool) - LOG_TRANSITION meldingen
  - `ntfy_log_safety` (bool) - LOG_SAFETY meldingen
  - `ntfy_log_error` (bool) - LOG_ERROR meldingen
  - `ntfy_log_warning` (bool) - LOG_WARNING meldingen
  - `cyclus_teller` (int) - Huidige cyclus teller (voor persistentie bij reboot)

**Load Flow (`SettingsStore::load()`):**
```
1. prefs.begin("tempctrl", false) [read-write]
2. settings.tTop = prefs.getFloat("t_top", 80.0)
3. settings.tBottom = prefs.getFloat("t_bottom", 25.0)
4. settings.cycleMax = prefs.getInt("cyclus_max", 0)
5. settings.tempOffset = prefs.getFloat("temp_offset", 0.0)
6. T_top validatie tegen TEMP_MAX (auto-correctie)
7. prefs.end()
```

**Save Flow (`SettingsStore::save()`):**
```
1. prefs.begin("tempctrl", false) [read-write]
2. prefs.putFloat("t_top", settings.tTop)
3. prefs.putFloat("t_bottom", settings.tBottom)
4. prefs.putInt("cyclus_max", settings.cycleMax)
5. prefs.putFloat("temp_offset", settings.tempOffset)
6. prefs.end()
```

**Google Credentials Load (`SettingsStore::loadGoogleCredentials()`):**
```
1. prefs.begin("tempctrl", true) [read-only]
2. creds.clientEmail = prefs.getString("client_email", "")
3. creds.projectId = prefs.getString("project_id", "")
4. creds.privateKey = prefs.getString("private_key", "")
5. creds.spreadsheetId = prefs.getString("spreadsheet_id", "")
6. prefs.end()
```

**Auto-save:** Bij elke instelling wijziging (via `saveAndLog()` of web interface)

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

**3. FreeRTOS Queue (Logger module):**
```cpp
queue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogRequest));
// ~1,000 bytes (20 * 50 bytes, geschat)
```

**4. FirebaseJson Objects (tijdelijk, Logger module):**
```cpp
FirebaseJson response;      // Stack allocatie
FirebaseJson valueRange;    // Stack allocatie
// Heap fragmentatie mogelijk tijdens JSON operaties
// Scope-based cleanup voorkomt memory leaks
```

**5. LVGL Objects (UIController):**
- Alle LVGL objecten worden intern gealloceerd door LVGL
- Automatisch cleanup bij object verwijdering
- Display buffer is grootste allocatie

**6. WebServer (ConfigWebServer):**
- WebServer object gebruikt ESP32 WebServer library (intern geheugen)
- HTML strings gegenereerd in `generateHTML()` (stack/string allocatie)
- JSON responses gegenereerd in `generateStatusJSON()` (stack allocatie)

### Stack Allocaties

**1. Logging Task Stack (Logger module):**
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

**4. Queue Overflow Protection (Logger):**
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

**6. GoogleCredentials Apart (SettingsStore):**
- GoogleCredentials niet in Settings struct (bespaart RAM)
- Aparte load/save methoden voor credentials
- Voorkomt DRAM overflow

### Memory Hotspots (Risico Gebieden)

**1. FirebaseJson Operaties (Logger module):**
- **Locatie:** `Logger::logInternal()`
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

**4. FreeRTOS Queue (Logger):**
- **Locatie:** `Logger::queue`
- **Grootte:** ~1KB
- **Risico:** Queue overflow (mitigatie: cleanup bij overflow)

**5. Stack Overflow Risico:**
- **Locatie:** Logging task (16KB stack)
- **Risico:** FirebaseJson operaties kunnen diep stack gebruiken
- **Mitigatie:** Verhoogde stack size (16384 bytes)

**6. WebServer HTML Generatie:**
- **Locatie:** `ConfigWebServer::generateHTML()`
- **Risico:** String concatenatie kan heap fragmentatie veroorzaken
- **Mitigatie:** String objecten worden automatisch vrijgegeven na gebruik

### Memory Monitoring

**Geen expliciete memory monitoring**, maar:
- Watchdog feeding tussen allocaties
- Foutafhandeling bij allocatie failures
- Queue overflow detection (Logger)
- Scope-based cleanup voor automatische destructors
- Char arrays i.p.v. String waar mogelijk

---

## Architectuur Overzicht

### Module Dependencies

```
Main Program
├─ SystemClock (geen dependencies)
├─ SettingsStore (afhankelijk van NtfyNotifier voor structs)
├─ TempSensor (geen dependencies)
├─ Logger (afhankelijk van SystemClock, NtfyNotifier optioneel)
├─ CycleController (afhankelijk van TempSensor, Logger)
├─ UIController (afhankelijk van CycleController via callbacks)
├─ NtfyNotifier (geen dependencies, alleen WiFi vereist)
└─ WebServer (afhankelijk van alle modules via callbacks, NtfyNotifier voor structs)
```

### Core Affinity

- **Core 0:** WiFi stack, systeemtaken (real-time)
- **Core 1:** Main loop, logging task (lagere prioriteit)

### Task Prioriteiten

- **Main Loop:** Default prioriteit (1)
- **Logging Task (Logger):** Prioriteit 1 (verlaagd voor knopbediening responsiviteit)
- **IDLE Task:** Prioriteit 0 (FreeRTOS default)

### Timing Constraints

- **Temperatuur sampling:** 285ms (MAX6675 conversietijd respect)
- **Display update:** 285ms (synchroon met sampling)
- **Grafiek logging:** 5000ms (5 seconden)
- **Grafiek display update:** 500ms (real-time backup)
- **GUI update:** 300ms
- **Logging rate limit:** 2000ms (minimaal tussen logs)
- **Token refresh:** 600 seconden (10 minuten)
- **Google Sheets status reset:** 1000ms (1 seconde)

### Callback Architectuur

**UIController Callbacks:**
- `StartCallback` - Start cyclus
- `StopCallback` - Stop cyclus
- `SettingChangeCallback` - Instelling wijziging
- `GraphResetCallback` - Grafiek reset
- Status getters: `IsActiveCallback`, `IsHeatingCallback`, etc.

**CycleController Callbacks:**
- `TransitionCallback` - Faseovergang (voor logging)

**WebServer Callbacks:**
- `StartCallback`, `StopCallback` - Acties
- `SettingsChangeCallback` - Instellingen wijziging
- Status getters: `GetCurrentTempCallback`, `IsActiveCallback`, etc.

**Logger:**
- Geen callbacks (directe integratie via `log()` methode)

---

**Laatste update:** Versie 3.99 (Modulaire Architectuur)
