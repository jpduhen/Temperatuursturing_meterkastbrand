/*  versie 4.01
    Jan Pieter Duhen
    Meterkastbrand onderzoek: Kooiklem maximaaltest

    Hardware:
    + CYD 2.8 inch display (LVGL graphics, touch bediening, MAX6675 temperatuurmeting)
    + GPIO 5/23 Solid State Relais (SSR) besturing (koelen/verwarmen)

    Basis functionaliteit:
    + Cyclus logica: verwarmen tot Top, koelen tot Dal, automatisch stoppen bij max cycli
    + Temperatuur instellingen via +/- knoppen (0-350°C, min 5°C verschil)
    + Cycli insteller (0 = oneindig), cyclus teller formaat "xxx/yyy" of "xxx/inf"
    + Dynamische Start/Stop knoppen (kleur op basis van systeemstatus)
    + Intelligente start modus op basis van huidige temperatuur
    + Veiligheidskoeling: koel tot <35°C voordat systeem uitgaat bij STOP

    Display:
    + Temperatuur update elke 285ms met mediaan van laatste 7 metingen
    + Temperatuur kleurcodering: groen<37°C (veilig), rood≥37°C (niet veilig aanraken)
    + Opwarmen/afkoelen timers (m:ss formaat)
    + Status tekst rechts uitgelijnd, breedte 230px
    + Versienummer klein in rechter onderhoek

    Grafiek:
    + Temperatuur/tijd grafiek (280x155px, 120 datapunten, 6 divisielijnen)
    + Automatische logging elke 5s, kleurcodering: rood stijgend, blauw dalend
    + Dynamische Y-as labels: 20°C tot T_top (6 labels, verdeeld over bereik)
    + Chart range past automatisch aan bij T_top wijziging (20°C - T_top)
    + X-as minuten labels (9-0)
    + GRAFIEK knop tussen START en STOP, TERUG knop naar hoofdscherm

    Google Sheets logging:
    + FreeRTOS task op Core 1 (lagere prioriteit) zodat Core 0 beschikbaar blijft voor real-time taken
    + Logging bij: START, verwarmen→koelen, koelen→verwarmen, STOP, veiligheidskoeling afgerond
    + Tabblad "DataLog-K", 9 kolommen: Timestamp, Meettemperatuur, Status, Huidige cyclus, Totaal cycli, T_top, T_dal, Fase tijd, Cyclus tijd
    + Cyclus tijd: totaaltijd van volledige cyclus (opwarmen + afkoelen) wordt gelogd bij "Afkoelen tot Opwarmen"
    + Timestamp formaat: [yy-mm-dd hh:mm:ss], WiFi en NTP tijd synchronisatie

    Instellingen:
    + Opslag in non-volatile memory (Preferences): T_top, T_bottom, cyclus_max
    + Automatisch laden bij opstarten, opslaan bij wijziging
*/


// FS.h moet VÓÓR andere includes worden geïncludeerd om namespace conflicten te voorkomen
#include <FS.h>
#include <time.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
// Install the "XPT2046_Touchscreen" library by Paul Stoffregen to use the Touchscreen - https://github.com/PaulStoffregen/XPT2046_Touchscreen - Note: this library doesn't require further configuration
#include <XPT2046_Touchscreen.h>
// Google Sheets logging - volgens https://randomnerdtutorials.com/esp32-datalogging-google-sheets/
// INSTALLEER EERST: 
//   - Sketch → Include Library → Manage Libraries → zoek "ESP-Google-Sheet-Client" → Install
//   - Sketch → Include Library → Manage Libraries → zoek "WiFiManager by tzapu" → Install
#include <WiFi.h>
#include <WiFiManager.h>  // WiFiManager voor eenvoudige WiFi configuratie (AP: "ESP32-TemperatuurCyclus")
#include <ESP_Google_Sheet_Client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
// Preferences worden nu beheerd door SettingsStore module
#include <MAX6675.h>
#include "src/SystemClock/SystemClock.h"
#include "src/SettingsStore/SettingsStore.h"
#include "src/TempSensor/TempSensor.h"
#include "src/Logger/Logger.h"
#include "src/CycleController/CycleController.h"
#include "src/UIController/UIController.h"
#include "src/NtfyNotifier/NtfyNotifier.h"
// Include WebServer.h moet NA andere includes om naamconflict te voorkomen
#include "src/WebServer/WebServer.h"
/* --- Rob Tillaart MAX6675 (software SPI) ---
   Constructor order (since v0.2.0): MAX6675(select, miso, clock)
   Our pins: CS=21, SO(MISO)=35, SCK=22
*/
#define MAX6675_CS   22  // 2e stekker GND-[22]-27-3.3V
#define MAX6675_SO   35  // 1e stekker GND-[35]-22-21
#define MAX6675_SCK  27  // 2e stekker GND-22-[27]-3.3V - 21 bij 2.4 inch, 27 bij 2.8 inch

// Relais pins (Solid State Relais - alleen NO contact)
#define RELAIS_KOELEN 5      // SSR voor koeling (HIGH = koelen aan, LOW = uit)
#define RELAIS_VERWARMING 23 // SSR voor verwarming (HIGH = verwarming aan, LOW = uit)

// Versienummer - VERHOOG BIJ ELKE WIJZIGING
#define FIRMWARE_VERSION_MAJOR 4
#define FIRMWARE_VERSION_MINOR 1

// Temperatuur constanten
#define TEMP_SAFE_THRESHOLD 37.0        // Temperatuur grens voor veilig aanraken (groen < 37°C, rood >= 37°C)
#define TEMP_SAFETY_COOLING 35.0        // Veiligheidskoeling temperatuur (koel tot < 35°C bij STOP)
#define TEMP_MAX 350.0                  // Maximum temperatuur (verwarming limiet)
#define TEMP_MIN_DIFF 5.0               // Minimum temperatuur verschil tussen T_top en T_bottom
#define TEMP_SAMPLE_INTERVAL_MS 285     // Temperatuur sampling interval (285ms)
#define TEMP_DISPLAY_UPDATE_MS 285      // Temperatuur display update interval (285ms - synchroon met sampling)
#define TEMP_GRAPH_LOG_INTERVAL_MS 5000 // Grafiek data logging interval (5 seconden)

// Timing constanten
#define MAX6675_POWERUP_DELAY_MS 500      // MAX6675 opstart vertraging
#define MAX6675_CONVERSION_TIME_MS 250    // MAX6675 conversietijd (220ms typisch + marge)
#define MAX6675_WARMUP_TIME_MS 1000       // MAX6675 warm-up tijd na power-up
#define MAX6675_SW_SPI_DELAY_US 1         // Software SPI delay voor stabiliteit (microseconden)
#define MAX6675_READ_RETRIES 3            // Aantal retries bij communicatiefouten
#define MAX6675_RETRY_DELAY_MS 10         // Delay tussen retries (milliseconden)
#define MAX6675_CRITICAL_SAMPLES 3        // Aantal samples voor majority voting bij kritieke metingen (verlaagd voor snelheid)
#define MAX6675_CRITICAL_SAMPLE_DELAY_MS 30 // Delay tussen samples bij majority voting (milliseconden, verlaagd voor snelheid)

// GUI update intervals
#define GUI_UPDATE_INTERVAL_MS 300       // GUI update interval (0.3 seconde)
#define GRAPH_UPDATE_INTERVAL_MS 500     // Grafiek update interval (0.5 seconde) - real-time updates

// Queue en logging constanten
#define LOG_QUEUE_WARN_THRESHOLD 2       // Waarschuwing bij queue bijna vol (LOG_QUEUE_SIZE - 2)
#define LOG_QUEUE_CLEANUP_COUNT 5        // Aantal entries om te verwijderen bij overflow

MAX6675 thermocouple(MAX6675_CS, MAX6675_SO, MAX6675_SCK);

// Module instanties
SystemClock systemClock;
SettingsStore settingsStore;
TempSensor tempSensor(MAX6675_CS, MAX6675_SO, MAX6675_SCK);
Logger logger;
CycleController cycleController;
UIController uiController;
NtfyNotifier ntfyNotifier;
ConfigWebServer webServer(80);

// Kalibratie offset voor MAX6675 (wordt geladen uit Preferences)
float temp_offset = 0.0; // Kalibratie offset in °C

// Timer voor conversietijd respectering
static unsigned long g_lastMax6675ReadTime = 0;

// VERPLAATST NAAR TempSensor module - functie verwijderd, gebruik tempSensor.getCurrent() of tempSensor.getMedian()
// readTempC_single() wordt niet meer gebruikt - alle aanroepen zijn vervangen

// Mediaan berekening constanten
#define TEMP_MEDIAN_SAMPLES 7              // Aantal metingen voor mediaan (7 waarden)

// Bereken mediaan uit array van temperatuurmetingen
// Ondersteunt zowel 3 samples (readTempC_critical) als 7 samples (circulaire array)
static float calculateMedianFromArray(float* samples, int count) {
  if (count == 0 || samples == nullptr) return NAN;
  
  // Limiteer count om stack overflow te voorkomen (max 7 voor circulaire array)
  if (count > TEMP_MEDIAN_SAMPLES) {
    count = TEMP_MEDIAN_SAMPLES;
  }
  
  // Maak kopie van array voor sorteren
  float sorted[TEMP_MEDIAN_SAMPLES];
  int valid_count = 0;
  for (int i = 0; i < count; i++) {
    if (!isnan(samples[i])) {
      sorted[valid_count++] = samples[i];
    }
  }
  
  if (valid_count == 0) return NAN;
  if (valid_count == 1) return sorted[0];
  
  // Insertion sort (efficiënt voor kleine arrays ≤7)
  for (int i = 1; i < valid_count; i++) {
    float key = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > key) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = key;
  }
  
  // Mediaan is middelste waarde (of gemiddelde van twee middelste bij even aantal)
  if (valid_count % 2 == 1) {
    return sorted[valid_count / 2];
  } else {
    return (sorted[valid_count / 2 - 1] + sorted[valid_count / 2]) / 2.0;
  }
}

// VERPLAATST NAAR TempSensor module - functie verwijderd, gebruik tempSensor.getCurrent() of tempSensor.getMedian()
// readTempC() wordt niet meer gebruikt - alle aanroepen zijn vervangen door tempSensor.sample()

// VERPLAATST NAAR TempSensor module - functie verwijderd, gebruik tempSensor.getCritical()
// readTempC_critical() wordt niet meer gebruikt - alle aanroepen zijn vervangen door tempSensor.getCritical()

// Temperature cache (updated in loop)
volatile float g_currentTempC = NAN;
static unsigned long g_lastSampleMs = 0;
static unsigned long g_lastDisplayMs = 0;  // Timer voor temperatuur weergave
static float g_avgTempC = NAN;             // Mediaan temperatuur (voor display en logging)
static float g_lastValidTempC = NAN;       // Laatste geldige temperatuurwaarde (voor display bij fouten)

// Mediaan berekening: circulaire array voor laatste 7 metingen
static float g_tempSamples[TEMP_MEDIAN_SAMPLES]; // Circulaire array voor temperatuurmetingen
static int g_tempSampleIndex = 0;         // Huidige index in circulaire array
static int g_tempSampleCount = 0;          // Aantal geldige metingen in buffer (0-7)

// Cyclus en temperatuur instellingen
float T_top = 80.0;        // Bovenste temperatuur voor verwarmen (default, wordt geladen uit Preferences)
float T_bottom = 25.0;     // Onderste temperatuur voor koelen (default, wordt geladen uit Preferences)
int cyclus_teller = 1;     // Huidige cyclus (start bij 1)
int cyclus_max = 0;        // Maximaal aantal cycli (0 = oneindig) (default, wordt geladen uit Preferences)

// Preferences worden nu beheerd door SettingsStore module
bool cyclus_actief = false; // Of er een cyclus draait
bool verwarmen_actief = true; // true = verwarmen, false = koelen
bool systeem_uit = false;  // Of het systeem uitgeschakeld is
bool koelingsfase_actief = false; // Veiligheidskoeling bij STOP
unsigned long verwarmen_start_tijd = 0; // Starttijd opwarmen
unsigned long koelen_start_tijd = 0;    // Starttijd afkoelen

// Tijdelijke opslag voor fasetijd bij fase wijziging (voordat timer wordt gereset)
unsigned long last_opwarmen_duur = 0; // Duur van laatste opwarmen fase
unsigned long last_koelen_duur = 0;   // Duur van laatste koelen fase
// Starttijden van fasen voor correcte timestamp bij logging
unsigned long last_opwarmen_start_tijd = 0; // Starttijd van laatste opwarmen fase
unsigned long last_koelen_start_tijd = 0;   // Starttijd van laatste koelen fase
// Temperatuur op moment van faseovergang (voor correcte logging)
float last_transition_temp = NAN; // Temperatuur op moment van laatste faseovergang
unsigned long veiligheidskoeling_start_tijd = 0; // Starttijd van veiligheidskoeling fase
unsigned long veiligheidskoeling_naloop_start_tijd = 0; // Starttijd van naloop (wanneer temp < 35°C is bereikt)
#define VEILIGHEIDSKOELING_NALOOP_MS (2 * 60 * 1000) // 2 minuten naloop
// Gemiddelde opwarmtijd tracking voor beveiliging
unsigned long gemiddelde_opwarmen_duur = 0; // Gemiddelde duur van opwarmfases (in ms)
int opwarmen_telling = 0; // Aantal opwarmfases voor gemiddelde berekening
// Temperatuur stagnatie detectie voor beveiliging (losse thermokoppel)
float laatste_temp_voor_stagnatie = NAN; // Laatste temperatuur voor stagnatie check
unsigned long stagnatie_start_tijd = 0; // Starttijd wanneer temperatuur binnen bandbreedte blijft
#define TEMP_STAGNATIE_BANDWIDTH 3.0 // Bandbreedte voor stagnatie detectie (graden)
#define TEMP_STAGNATIE_TIJD_MS (2 * 60 * 1000) // 2 minuten stagnatie tijd
// NTP offset voor timestamp berekening (millis() -> Unix tijd)
// VERPLAATST NAAR SystemClock module

// Grafiek data - EENVOUDIGE CIRCULAIRE ARRAY
#define GRAPH_POINTS 120  // Aantal data punten in grafiek
float* graph_temps = nullptr;  // Temperatuur waarden
unsigned long* graph_times = nullptr; // Tijdstempels
int graph_write_index = 0;  // Write index: wijst naar volgende positie om te schrijven (0-119, wrapt rond)
int graph_count = 0; // Aantal punten in buffer (0-120, wordt 120 na wrap-around)
bool graph_data_ready = false; // Of grafiek data beschikbaar is (true zodra eerste punt is geschreven)
unsigned long graph_last_log_time = 0; // Tijd van laatste grafiek data log (voor reset bij START)
static unsigned long g_lastGraphUpdateMs = 0; // Timer voor grafiek update
bool graph_force_rebuild = false; // Flag om grafiek opnieuw op te bouwen (bij scherm wissel)

// Google Sheets logging
// Credentials uit meterkast-v2-072955b2cb25.json
#define PROJECT_ID "meterkast-v2"
#define CLIENT_EMAIL "meterkast-datalogging@meterkast-v2.iam.gserviceaccount.com"
const char PRIVATE_KEY[] PROGMEM = "-----BEGIN PRIVATE KEY-----\nMIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC9sp8QBuGd7VJe\ng8IQxZEdiiMi/Bm/4M5hxCMnHVv+bqGzx98bWqDvG4euetYPvYtDx94bPOa99eKz\nJxM1KRvTAYw8tuYG9febqDIQClOQbZKGuq52h3msvTMj8kpO3znMoEMoZcDWvlr7\nQUO/p0wXMK3Trmlsj4yZNyCVjFHc4KivsyhPBnikdNLEmrR4P0n9+D3iUYKemsXv\nBci/pCeQ/yteifg2YSyKMroLTh5GwdkKORaEgBmBwcH0e8itcedfAcLxAZf2YNRy\nBlpun3dQHdBsjoSrVtH1wY5tN1lUagd+jJbBu0rfh+fKKWWEmC+8goFXp3EWQbqo\nMLi2tYFRAgMBAAECggEAT6so6o45Sx5UraUgZ/LRk1pkj1jZZ6B+jMjXCqJl9GF6\nQAr9RHu3gzRIs5qbNFkimADk4wOc1MLjxuHuSzFMoj3QK9+Uk+/RYeotvFbZ6Zpb\nI4JbqyQSkd2UENC9LIrLt4jHK7wwVP/1Lfim/2K/ri2fk3t4g0N2hRKF+MjJyL1G\nrlU3V2oPCPdN/PCUlTrr0tPrvxKL4lh4zncXKrPiuymqZnJCMud7gl9RYbd186pV\npjrEdsRyCJo4CzeHaiWXo0CtCOpKWWy8nNfCgKGfBh66PJJEJlWmkHtsRrIjdHTo\nCYeqM/rDh+xnfoEweQP4ueJgXImjbnhT2DlPQt/+0QKBgQD9iZuBB393aHfirhLW\n9A7Avh6rPoQV4xntmiMV3sqA8GAkSjvUANYUnLYtlU+qVhiQQVuOlzeL+jRnd38o\nZMmImIQMC4idy5zKhYRMc80ZgkPlCxweJ92JDGRX542mhAoxYC1PMifGLX1sdPF1\nnDjeFbt5+n3etJh26vc7lsp4mwKBgQC/ikiPnKMIpaAWLroAgiXi0fO0BWd1ZIRs\nrGPbATBvKk8i6CNZ2Kqxv6ABmzorENcIaqlFinYew4XCA2iRPORTiCvwBtnJ4QlB\nmnhQMvYV2quY5/izJeOkc1W6Wl9O0q158SKQtP6O+dhWGhhcTKH43Z/VCYJqPMtb\n6qPI+1D+gwKBgQDQWiJZobDFjyteNFE1JnFtQY/wiDqBxhSLpuyIT4M4/ND1Ya/S\n5DEJ1VL9GMPUJlafTaaRAoHaXW5tTM1Jg7H+t99kCqJlkmsyHxD+xvdYrC1hb4TW\n30n2EyHu/1Hv8nhx5Si2+W+oM7/rvhqj3RL6pv9fGVQRDXZx21v7M0sGWwKBgAW2\nt1FpZ8ejQTJINI49U6n/f1iYbKyo0fZ38gafc3Vteqzc5ekROI5S3BAQNF0ChJeg\nrun/JmTmij/uYBktCtafEkh3u9l02BTX6cziqEqgmkvWZ6nYcIEAW8dSWNN+H2Sq\n19AfRhS3yUdQQffG5XIKEnGFdhl7NMnKJxagwJrHAoGAUPS5VN8gbqFjtPK33ySQ\n2XxsiTOqAfuTmv21o4KDKHQHnIaRSfevRe0N71+f+zhoEfoGwJKpNOo9/+AvaVeo\nzLxmZzr3Fhul2J5NcPIxBRCRQ0nUrpYK589H4RE4oCHle9CKJD0di/RQCioAa+m1\n3iyoAUpeZI8qrLSIM76wWRs=\n-----END PRIVATE KEY-----\n";
// Spreadsheet ID - moet nog worden aangepast met de juiste ID
const char SPREADSHEET_ID[] = "1y3iIj6bsRgDEx1ZMGhXvuBZU5Nw1GjQsqsm4PQUTFy0"; // Vervang met jouw Spreadsheet ID

// Google Sheets client object
ESP_Google_Sheet_Client sheet;

// Globale vlag voor Google Sheets authenticatie status
bool g_googleAuthTokenReady = false;

// FreeRTOS Queue en Task voor logging op andere core (betere responsiviteit)
#define LOG_QUEUE_SIZE 20
#define LOG_STATUS_MAX_LEN 50
#define LOG_FASE_TIJD_MAX_LEN 10
#define LOG_CYCLUS_TIJD_MAX_LEN 10

// VERPLAATST NAAR Logger module - LogRequest struct is nu in Logger.h gedefinieerd
// Gebruik LogRequest uit Logger.h (via #include "src/Logger/Logger.h")

QueueHandle_t logQueue = nullptr;
TaskHandle_t loggingTaskHandle = nullptr;

// VERPLAATST NAAR Logger module - forward declaration verwijderd

// Preferences functies voor opslag van instellingen
// Wrapper functies voor backward compatibility - SettingsStore module handelt de daadwerkelijke opslag af
void loadSettings() {
  Settings settings = settingsStore.load();
  T_top = settings.tTop;
  T_bottom = settings.tBottom;
  cyclus_max = settings.cycleMax;
  temp_offset = settings.tempOffset;
  // Pas offset toe op MAX6675 library (oude instantie)
  thermocouple.setOffset(temp_offset);
  // Pas offset toe op TempSensor module
  tempSensor.setOffset(temp_offset);
  
  // Update CycleController settings (als al geïnitialiseerd)
  if (cycleController.isActive() || true) { // Altijd updaten (CycleController kan al geïnitialiseerd zijn)
    cycleController.setTargetTop(T_top);
    cycleController.setTargetBottom(T_bottom);
    cycleController.setMaxCycles(cyclus_max);
  }
}

void saveSettings() {
  Settings settings;
  settings.tTop = T_top;
  settings.tBottom = T_bottom;
  settings.cycleMax = cyclus_max;
  settings.tempOffset = temp_offset;
  settingsStore.save(settings);
}

// Token status callback functie
// VERPLAATST NAAR Logger module - functie blijft tijdelijk voor backward compatibility
static void gs_tokenStatusCallback(TokenInfo info) {
  // Logger module handelt dit nu af
  if (info.status == token_status_ready) {
    g_googleAuthTokenReady = true; // Update globale variabele voor backward compatibility
  }
}

// Display dimensies
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// Display buffer
uint32_t* draw_buf = nullptr;

// Globale variabelen voor status display op hoofdscherm
lv_obj_t* init_status_label = nullptr;
lv_obj_t* wifi_status_label = nullptr;
lv_obj_t* gs_status_label = nullptr;

// Globale variabelen voor logging feedback (gebruikt tussen logging task en main loop, beide op Core 1)
volatile bool g_logSuccessFlag = false;  // Flag om succesvolle logging aan te geven
volatile unsigned long g_logSuccessTime = 0;  // Tijdstempel van succesvolle logging
char g_lastGSStatusText[LOG_STATUS_MAX_LEN] = "";  // Bewaar laatste Google Sheets status tekst (char array i.p.v. String)

// Toon initialisatie status op display
void showInitStatus(const char* message, uint32_t color = 0x000000) {
  if (init_status_label == nullptr) return;
  lv_label_set_text(init_status_label, message);
  lv_obj_set_style_text_color(init_status_label, lv_color_hex(color), LV_PART_MAIN);
  // Geen lv_task_handler() hier - wordt al frequent aangeroepen in loop()
}

// Verberg initialisatie status
void hideInitStatus() {
  if (init_status_label == nullptr) return;
  lv_label_set_text(init_status_label, ""); // Lege tekst = onzichtbaar
  // Geen lv_task_handler() hier - wordt al frequent aangeroepen in loop()
}

// Forward declarations voor knop functies (moeten na globale declaraties)
void setAllButtonsGray();
void setAllButtonsNormal();

// Toon WiFi status op display (standaard grijs, rood bij fout)
void showWifiStatus(const char* message, bool isError = false) {
  if (wifi_status_label == nullptr) return;
  lv_label_set_text(wifi_status_label, message);
  uint32_t color = isError ? 0xFF0000 : 0x888888; // Rood bij fout, anders grijs
  lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(color), LV_PART_MAIN);
  // Geen lv_task_handler() hier - wordt al frequent aangeroepen in loop()
}

// Toon Google Sheets status op display (standaard grijs, rood bij fout)
void showGSStatus(const char* message, bool isError = false) {
  if (gs_status_label == nullptr) return;
  // Bewaar originele tekst (zonder vinkje) voor later gebruik - gebruik char array
  strncpy(g_lastGSStatusText, message, sizeof(g_lastGSStatusText) - 1);
  g_lastGSStatusText[sizeof(g_lastGSStatusText) - 1] = '\0'; // Null-terminate
  lv_label_set_text(gs_status_label, message);
  uint32_t color = isError ? 0xFF0000 : 0x888888; // Rood bij fout, anders grijs
  lv_obj_set_style_text_color(gs_status_label, lv_color_hex(color), LV_PART_MAIN);
  // Geen lv_task_handler() hier - wordt al frequent aangeroepen in loop()
}

// Toon tijdelijk "LOGGING" tekst bij Google Sheets status (1 seconde)
void showGSSuccessCheckmark() {
  if (gs_status_label == nullptr) return;
  // Vervang "GEREED" door "LOGGING" in de status tekst - gebruik char array
  char text_with_logging[LOG_STATUS_MAX_LEN];
  strncpy(text_with_logging, g_lastGSStatusText, sizeof(text_with_logging) - 1);
  text_with_logging[sizeof(text_with_logging) - 1] = '\0';
  // Vervang "GEREED" door "LOGGING"
  char* gereed_pos = strstr(text_with_logging, "GEREED");
  if (gereed_pos != nullptr) {
    // BELANGRIJK: Check buffer bounds voordat we overschrijven
    size_t gereed_pos_offset = gereed_pos - text_with_logging;
    if (gereed_pos_offset + 7 < sizeof(text_with_logging)) {
      strncpy(gereed_pos, "LOGGING", 7);
      gereed_pos[7] = '\0'; // Null-terminate
    }
  }
  lv_label_set_text(gs_status_label, text_with_logging);
  lv_obj_set_style_text_color(gs_status_label, lv_color_hex(0x00AA00), LV_PART_MAIN); // Groen voor LOGGING tekst
}

// Alloceer buffers in DRAM
bool allocate_buffers() {
  // Bereken draw_buf grootte (moet hier gebeuren omdat LV_COLOR_DEPTH pas na LVGL include beschikbaar is)
  // Standaard 16-bit color depth (RGB565), maar dit wordt later gecorrigeerd
  size_t draw_buf_bytes = SCREEN_WIDTH * SCREEN_HEIGHT / 10 * 2; // 2 bytes per pixel voor RGB565
  
  // Alloceer draw_buf
  draw_buf = (uint32_t*)malloc(draw_buf_bytes);
  
  // Alloceer grafiek buffers
  graph_temps = (float*)malloc(GRAPH_POINTS * sizeof(float));
  graph_times = (unsigned long*)malloc(GRAPH_POINTS * sizeof(unsigned long));
  
  if (draw_buf && graph_temps && graph_times) {
    return true;
  }
  return false;
}

// Initialiseer grafiek data arrays
void init_graph_data() {
  if (graph_temps == nullptr || graph_times == nullptr) {
    return;
  }
  for (int i = 0; i < GRAPH_POINTS; i++) {
    graph_temps[i] = NAN;  // Start met geen data
    graph_times[i] = 0;
  }
  graph_write_index = 0;
  graph_count = 0;
  graph_data_ready = false;
  graph_last_log_time = 0;
}

// VERPLAATST NAAR TempSensor module - functie blijft tijdelijk voor backward compatibility
static float getMedianTemp() {
  return tempSensor.getMedian();
}

// VERPLAATST NAAR TempSensor module - functie blijft tijdelijk voor backward compatibility
static void sampleMax6675()
{
  tempSensor.sample();
  // Update globale variabelen voor backward compatibility
  g_currentTempC = tempSensor.getCurrent();
  g_avgTempC = tempSensor.getMedian();
  g_lastValidTempC = tempSensor.getLastValid();
}

// Relais besturingsfuncties (Solid State Relais - alleen NO contact)
// UIT: 5 laag, 23 laag
// KOELEN: 5 hoog, 23 laag
// VERWARMEN: 5 laag, 23 hoog
void systeemAan() {
  // Systeem aan - beide relais laag (systeem gereed, maar nog niet actief)
  // De keuze tussen verwarmen/koelen wordt direct daarna gemaakt
  digitalWrite(RELAIS_KOELEN, LOW);
  digitalWrite(RELAIS_VERWARMING, LOW);
  systeem_uit = false;
}

void verwarmenAan() {
  // Feed watchdog voor en na digitalWrite om crashes te voorkomen
  yield();
  // VERWARMEN: 5 laag, 23 hoog
  digitalWrite(RELAIS_KOELEN, LOW);
  digitalWrite(RELAIS_VERWARMING, HIGH);
  yield();
  verwarmen_actief = true;
}

void koelenAan() {
  // Feed watchdog voor en na digitalWrite om crashes te voorkomen
  yield();
  // KOELEN: 5 hoog, 23 laag
  digitalWrite(RELAIS_KOELEN, HIGH);
  digitalWrite(RELAIS_VERWARMING, LOW);
  yield();
  verwarmen_actief = false;
}

void alleRelaisUit() {
  // UIT: 5 laag, 23 laag
  digitalWrite(RELAIS_KOELEN, LOW);
  digitalWrite(RELAIS_VERWARMING, LOW);
  systeem_uit = true;
  cyclus_actief = false;
}

void systeemGereed() {
  // Systeem gereed - beide relais laag (nog niet actief)
  digitalWrite(RELAIS_KOELEN, LOW);
  digitalWrite(RELAIS_VERWARMING, LOW);
  systeem_uit = false;  // Systeem is gereed, niet uitgeschakeld
  cyclus_actief = false;
}

// Helper functie voor tijd formatting (char array versie)
void formatTijdChar(unsigned long milliseconden, char* buffer, size_t buffer_size) {
  unsigned long seconden = milliseconden / 1000;
  unsigned long minuten = seconden / 60;
  unsigned long sec = seconden % 60;
  snprintf(buffer, buffer_size, "%lu:%02lu", minuten, sec);
}

// Helper functie om fase tijd te resetten naar "0:00"
static void resetFaseTijd(char* buffer, size_t buffer_size) {
  strncpy(buffer, "0:00", buffer_size - 1);
  buffer[buffer_size - 1] = '\0';
}

// Helper functie voor Google Sheets timestamp (char array versie)
// VERPLAATST NAAR SystemClock module
// Functies worden vervangen door systemClock.getTimestamp() en systemClock.getTimestampFromMillis()
// Oude functies blijven tijdelijk voor backward compatibility tijdens refactoring
void getTimestampChar(char* buffer, size_t buffer_size) {
  systemClock.getTimestamp(buffer, buffer_size);
}

void getTimestampFromMillisChar(unsigned long timestamp_ms, char* buffer, size_t buffer_size) {
  systemClock.getTimestampFromMillis(timestamp_ms, buffer, buffer_size);
}

// VERPLAATST NAAR Logger module - functie verwijderd, Logger::task() wordt nu gebruikt

// Publieke logging functie - stuurt request naar Logger module (niet-blokkerend)
void logToGoogleSheet(const char* status) {
  // Logger module handelt queue management af
  
  // Null pointer check
  if (status == nullptr) {
    return;
  }
  
  // Maak logging request struct
  LogRequest req;
  strncpy(req.status, status, LOG_STATUS_MAX_LEN - 1);
  req.status[LOG_STATUS_MAX_LEN - 1] = '\0'; // Null-terminate
  
  // Kopieer huidige systeem state
  // Gebruik temperatuur op moment van faseovergang als beschikbaar
  // Dit zorgt ervoor dat de temperatuur correct is (T_top bij opwarmen→afkoelen, T_bottom bij afkoelen→opwarmen)
  if (!isnan(last_transition_temp)) {
    req.temp = last_transition_temp; // Gebruik temperatuur op moment van overgang
    last_transition_temp = NAN; // Reset na gebruik
  } else if (!isnan(g_avgTempC)) {
    req.temp = g_avgTempC; // Fallback naar gemiddelde (voor START, STOP, etc.)
  } else {
    req.temp = isnan(g_currentTempC) ? 0.0 : g_currentTempC; // Fallback naar laatste meting
  }
  
  // BELANGRIJK: Bij "Afkoelen tot Opwarmen" is cyclus_teller al verhoogd (cyclus_teller++)
  // maar de cyclus_tijd die wordt gelogd is van de vorige cyclus die net is afgerond
  // Dus gebruik cyclus_teller - 1 voor de logging
  if (strcmp(status, "Afkoelen tot Opwarmen") == 0) {
    req.cyclus_teller = (cyclus_teller > 1) ? (cyclus_teller - 1) : 1; // Vorige cyclus (minimaal 1)
  } else {
    req.cyclus_teller = cyclus_teller; // Normale cyclus teller
  }
  
  req.cyclus_max = cyclus_max;
  req.T_top = T_top;
  req.T_bottom = T_bottom;
  
  
  // Bereken fase tijd [mm:ss] en timestamp - op basis van de status die we loggen
  // De status die we loggen is de fase die net is afgelopen
  // De fasetijd is de duur van die fase, en de timestamp is de starttijd van die fase
  char fase_tijd_str[LOG_FASE_TIJD_MAX_LEN];
  unsigned long log_timestamp_ms = millis(); // Default: huidige tijd
  
  // Bereken cyclus_tijd (totaaltijd van volledige cyclus: opwarmen + afkoelen)
  char cyclus_tijd_str[LOG_CYCLUS_TIJD_MAX_LEN];
  
  if (strcmp(status, "Afkoelen tot Opwarmen") == 0) {
    // "Afkoelen tot Opwarmen" wordt gelogd wanneer we overgaan van afkoelen naar opwarmen
    // Dit betekent dat de afkoelen fase net is afgelopen, gebruik dus last_koelen_duur
    if (last_koelen_duur > 0 && last_koelen_start_tijd > 0) {
      formatTijdChar(last_koelen_duur, fase_tijd_str, sizeof(fase_tijd_str));
      log_timestamp_ms = last_koelen_start_tijd; // Gebruik starttijd van Afkoelen fase
      
      // Bereken totaaltijd van volledige cyclus (opwarmen + afkoelen)
      // last_opwarmen_duur is nog beschikbaar omdat we deze pas resetten na logging
      if (last_opwarmen_duur > 0) {
        unsigned long totaal_cyclus_duur = last_opwarmen_duur + last_koelen_duur;
        formatTijdChar(totaal_cyclus_duur, cyclus_tijd_str, sizeof(cyclus_tijd_str));
      } else {
        // Geen vorige opwarmen fase (bijvoorbeeld bij eerste cyclus na START)
        resetFaseTijd(cyclus_tijd_str, sizeof(cyclus_tijd_str));
      }
      
      // Reset beide na gebruik (nu pas resetten zodat we totaaltijd kunnen berekenen)
      last_koelen_duur = 0;
      last_koelen_start_tijd = 0;
      last_opwarmen_duur = 0; // Reset nu pas (was al gereset bij "Opwarmen tot Afkoelen", maar nu expliciet)
      last_opwarmen_start_tijd = 0;
    } else {
      resetFaseTijd(fase_tijd_str, sizeof(fase_tijd_str));
      resetFaseTijd(cyclus_tijd_str, sizeof(cyclus_tijd_str));
      log_timestamp_ms = millis(); // Huidige tijd bij START (geen vorige fase)
    }
  } else if (strcmp(status, "Opwarmen tot Afkoelen") == 0) {
    // "Opwarmen tot Afkoelen" wordt gelogd wanneer we overgaan van opwarmen naar afkoelen
    // Dit betekent dat de opwarmen fase net is afgelopen, gebruik dus last_opwarmen_duur
    // BELANGRIJK: Reset last_opwarmen_duur NIET hier - we hebben deze nodig voor cyclus_tijd bij "Afkoelen tot Opwarmen"
    if (last_opwarmen_duur > 0 && last_opwarmen_start_tijd > 0) {
      formatTijdChar(last_opwarmen_duur, fase_tijd_str, sizeof(fase_tijd_str));
      log_timestamp_ms = last_opwarmen_start_tijd; // Gebruik starttijd van Opwarmen fase
      // last_opwarmen_duur wordt NIET gereset - we hebben deze nodig voor cyclus_tijd bij volgende "Afkoelen tot Opwarmen"
    } else {
      resetFaseTijd(fase_tijd_str, sizeof(fase_tijd_str));
      log_timestamp_ms = millis(); // Huidige tijd (geen vorige fase)
    }
    // Geen cyclus_tijd bij "Opwarmen tot Afkoelen" - cyclus is nog niet compleet
    // Zet lege string (niet "0:00") zodat kolom leeg blijft in Google Sheets
    cyclus_tijd_str[0] = '\0'; // Lege string
  } else if (strcmp(status, "Veiligheidskoeling") == 0) {
    // Bij veiligheidskoeling loggen we de tijd van de veiligheidskoeling fase
    if (veiligheidskoeling_start_tijd > 0) {
      formatTijdChar(millis() - veiligheidskoeling_start_tijd, fase_tijd_str, sizeof(fase_tijd_str));
      log_timestamp_ms = veiligheidskoeling_start_tijd; // Starttijd van veiligheidskoeling fase
    } else if (koelen_start_tijd > 0) {
      // Fallback: gebruik koelen_start_tijd als veiligheidskoeling_start_tijd niet is ingesteld
      formatTijdChar(millis() - koelen_start_tijd, fase_tijd_str, sizeof(fase_tijd_str));
      log_timestamp_ms = koelen_start_tijd;
    } else {
      strncpy(fase_tijd_str, "0:00", sizeof(fase_tijd_str) - 1);
      fase_tijd_str[sizeof(fase_tijd_str) - 1] = '\0';
      log_timestamp_ms = millis();
    }
    // Geen cyclus_tijd bij veiligheidskoeling
    resetFaseTijd(cyclus_tijd_str, sizeof(cyclus_tijd_str));
  } else if (strcmp(status, "Uit") == 0) {
    // Bij "Uit" na veiligheidskoeling: log de fasetijd van veiligheidskoeling
    if (veiligheidskoeling_start_tijd > 0) {
      formatTijdChar(millis() - veiligheidskoeling_start_tijd, fase_tijd_str, sizeof(fase_tijd_str));
      log_timestamp_ms = veiligheidskoeling_start_tijd; // Starttijd van veiligheidskoeling fase
      veiligheidskoeling_start_tijd = 0; // Reset na gebruik
  } else {
      resetFaseTijd(fase_tijd_str, sizeof(fase_tijd_str));
      log_timestamp_ms = millis();
    }
    // Geen cyclus_tijd bij "Uit"
    resetFaseTijd(cyclus_tijd_str, sizeof(cyclus_tijd_str));
    } else {
    // Voor andere statussen (START, STOP, etc.): gebruik 0:00 en huidige tijd
    resetFaseTijd(fase_tijd_str, sizeof(fase_tijd_str));
    resetFaseTijd(cyclus_tijd_str, sizeof(cyclus_tijd_str));
    log_timestamp_ms = millis();
    }
  strncpy(req.fase_tijd, fase_tijd_str, LOG_FASE_TIJD_MAX_LEN - 1);
  req.fase_tijd[LOG_FASE_TIJD_MAX_LEN - 1] = '\0'; // Null-terminate
  strncpy(req.cyclus_tijd, cyclus_tijd_str, LOG_CYCLUS_TIJD_MAX_LEN - 1);
  req.cyclus_tijd[LOG_CYCLUS_TIJD_MAX_LEN - 1] = '\0'; // Null-terminate
  req.timestamp_ms = log_timestamp_ms; // Sla timestamp op voor gebruik in logToGoogleSheet_internal
  
  // VERPLAATST NAAR Logger module - gebruik logger.log() in plaats van direct naar queue
  logger.log(req);
}

// VERPLAATST NAAR Logger module - functie verwijderd, Logger::logInternal() wordt nu gebruikt
// logToGoogleSheet_internal() wordt niet meer gebruikt - Logger module handelt dit af

// Forward declaration voor globale GUI variabelen (nodig voor static functies)
extern lv_obj_t * text_label_temp_value;

// Helper functie: update temperatuur display met specifieke waarde en kleur
static void updateTempDisplay(float temp) {
  if (text_label_temp_value == nullptr) return;
  
  char temp_text[16];
  snprintf(temp_text, sizeof(temp_text), "%.1f°C", temp);
  lv_label_set_text(text_label_temp_value, temp_text);
  
  // Color coding: groen onder TEMP_SAFE_THRESHOLD, rood vanaf TEMP_SAFE_THRESHOLD
  if (temp < TEMP_SAFE_THRESHOLD) {
    lv_obj_set_style_text_color(text_label_temp_value, lv_color_hex(0x00AA00), 0);
  } else {
    lv_obj_set_style_text_color(text_label_temp_value, lv_color_hex(0xCC0000), 0);
  }
  
  // Force LVGL update
  lv_task_handler();
}

// Helper functie: haal kritieke temperatuur op met fallback
// Retourneert NAN als geen geldige meting beschikbaar is
static float getCriticalTemp() {
  // VERPLAATST NAAR TempSensor module
  float temp = tempSensor.getCritical();
  if (isnan(temp)) {
    // Fallback naar normale mediaan als critical read faalt
    temp = getMedianTemp();
  }
  return temp;
}

// Cyclus logica helper functies
static void handleSafetyCooling() {
  // Gebruik majority voting voor KRITIEKE meting (veiligheidskoeling)
  float temp_for_check = getCriticalTemp();
  if (isnan(temp_for_check)) {
    return; // Wacht op geldige temperatuur meting
  }
  
  if (temp_for_check < TEMP_SAFETY_COOLING) {
    // Veiligheidstemperatuur bereikt - start naloop timer als die nog niet loopt
    if (veiligheidskoeling_naloop_start_tijd == 0) {
      veiligheidskoeling_naloop_start_tijd = millis();
      // Log dat veiligheidstemperatuur is bereikt en naloop start
    logToGoogleSheet("Veiligheidskoeling");
    }
    
    // Check of naloop periode (2 minuten) is verstreken
    unsigned long naloop_verstreken = millis() - veiligheidskoeling_naloop_start_tijd;
    if (naloop_verstreken >= VEILIGHEIDSKOELING_NALOOP_MS) {
      // Naloop periode is verstreken - schakel uit
    koelingsfase_actief = false;
    alleRelaisUit();
    verwarmen_start_tijd = 0;
    koelen_start_tijd = 0;
    veiligheidskoeling_start_tijd = 0;
      veiligheidskoeling_naloop_start_tijd = 0;
    logToGoogleSheet("Uit");
  } else {
      // Naloop periode nog niet verstreken - blijf koelen
    koelenAan();
    }
  } else {
    // Temperatuur nog niet veilig - blijf koelen en reset naloop timer
    koelenAan();
    if (veiligheidskoeling_naloop_start_tijd > 0) {
      // Temperatuur is weer gestegen boven veiligheidsniveau - reset naloop
      veiligheidskoeling_naloop_start_tijd = 0;
    }
  }
}

static void handleHeatingPhase() {
  // Start timer als die nog niet loopt
  if (verwarmen_start_tijd == 0) {
    verwarmen_start_tijd = millis();
  }
  
  // BEVEILIGING: Check of opwarmtijd > 2x gemiddelde opwarmtijd
  // Alleen controleren als we al minstens 1 opwarmfase hebben gehad
  if (gemiddelde_opwarmen_duur > 0 && verwarmen_start_tijd > 0) {
    unsigned long huidige_opwarmen_duur = millis() - verwarmen_start_tijd;
    unsigned long max_opwarmen_duur = gemiddelde_opwarmen_duur * 2;
    
    if (huidige_opwarmen_duur > max_opwarmen_duur) {
      // Opwarmen duurt te lang - activeer veiligheidskoeling
      yield(); // Feed watchdog
      
      // Log de beveiligingsactie
      logToGoogleSheet("Beveiliging: Opwarmen te lang");
      
      // Stop cyclus en activeer veiligheidskoeling
      cyclus_actief = false;
      systeem_uit = true;
      verwarmen_actief = false;
      alleRelaisUit();
      
      // Start veiligheidskoeling
      koelingsfase_actief = true;
      koelenAan();
      veiligheidskoeling_start_tijd = millis();
      veiligheidskoeling_naloop_start_tijd = 0; // Reset naloop timer (wordt gestart wanneer temp < 35°C)
      
      // Reset opwarmen timer en stagnatie tracking
      verwarmen_start_tijd = 0;
      laatste_temp_voor_stagnatie = NAN;
      stagnatie_start_tijd = 0;
      
      yield(); // Feed watchdog
      return; // Stop verdere verwerking
    }
  }
  
  // Check of we T_top bereikt hebben
  // Gebruik majority voting voor KRITIEKE meting (faseovergang opwarmen→afkoelen)
  float temp_for_check = getCriticalTemp();
  if (isnan(temp_for_check)) {
    // Reset stagnatie tracking bij ongeldige temperatuur
    laatste_temp_voor_stagnatie = NAN;
    stagnatie_start_tijd = 0;
    return; // Wacht op geldige temperatuur meting
  }
  
  // BEVEILIGING: Detecteer temperatuur stagnatie (losse thermokoppel)
  // Check of temperatuur binnen bandbreedte blijft voor langer dan 2 minuten
  if (isnan(laatste_temp_voor_stagnatie)) {
    // Eerste meting: start tracking
    laatste_temp_voor_stagnatie = temp_for_check;
    stagnatie_start_tijd = millis();
  } else {
    // Check of temperatuur binnen bandbreedte blijft
    float temp_verschil = abs(temp_for_check - laatste_temp_voor_stagnatie);
    if (temp_verschil <= TEMP_STAGNATIE_BANDWIDTH) {
      // Temperatuur blijft binnen bandbreedte
      unsigned long stagnatie_duur = millis() - stagnatie_start_tijd;
      if (stagnatie_duur >= TEMP_STAGNATIE_TIJD_MS) {
        // Temperatuur is langer dan 2 minuten constant - mogelijke losse thermokoppel
        yield(); // Feed watchdog
        
        // Log de beveiligingsactie
        logToGoogleSheet("Beveiliging: Temperatuur stagnatie");
        
        // Stop cyclus en activeer veiligheidskoeling
        cyclus_actief = false;
        systeem_uit = true;
        verwarmen_actief = false;
        alleRelaisUit();
        
        // Start veiligheidskoeling
        koelingsfase_actief = true;
        koelenAan();
        veiligheidskoeling_start_tijd = millis();
        veiligheidskoeling_naloop_start_tijd = 0; // Reset naloop timer (wordt gestart wanneer temp < 35°C)
        
        // Reset opwarmen timer en stagnatie tracking
        verwarmen_start_tijd = 0;
        laatste_temp_voor_stagnatie = NAN;
        stagnatie_start_tijd = 0;
        
        yield(); // Feed watchdog
        return; // Stop verdere verwerking
      }
    } else {
      // Temperatuur is veranderd buiten bandbreedte - reset stagnatie tracking
      laatste_temp_voor_stagnatie = temp_for_check;
      stagnatie_start_tijd = millis();
    }
  }
  
  if (temp_for_check >= T_top) {
    // Kritieke overgang - extra watchdog feeding en error handling
    // Feed watchdog VOORDAT we beginnen met overgang
    yield();
    
    // Bereken en sla fasetijd op VOORDAT we overgaan
    last_opwarmen_duur = (verwarmen_start_tijd > 0) ? (millis() - verwarmen_start_tijd) : 0;
    last_opwarmen_start_tijd = verwarmen_start_tijd;
    
    // Reset stagnatie tracking bij faseovergang
    laatste_temp_voor_stagnatie = NAN;
    stagnatie_start_tijd = 0;
    
    // Update gemiddelde opwarmtijd voor beveiliging
    // Gebruik exponentiële moving average voor robuustheid
    if (opwarmen_telling == 0) {
      // Eerste meting: gebruik direct als gemiddelde
      gemiddelde_opwarmen_duur = last_opwarmen_duur;
      opwarmen_telling = 1;
    } else {
      // Update gemiddelde: 70% oude waarde + 30% nieuwe waarde
      // Dit geeft recentere metingen meer gewicht maar behoudt historie
      gemiddelde_opwarmen_duur = (gemiddelde_opwarmen_duur * 7 + last_opwarmen_duur * 3) / 10;
      opwarmen_telling++;
    }
    
    // Sla temperatuur op op moment van overgang (T_top bereikt)
    last_transition_temp = temp_for_check;
    // Update g_avgTempC direct zodat scherm en log dezelfde waarde tonen
    g_avgTempC = temp_for_check;
    g_lastValidTempC = temp_for_check;
    
    // Update display direct met nieuwe temperatuur (zonder te wachten op normale update interval)
    updateTempDisplay(temp_for_check);
    
    // Feed watchdog na berekening
    yield();
    
    // Log "Opwarmen tot Afkoelen" wanneer we overgaan van opwarmen naar afkoelen
    // De temperatuur is de temperatuur op het moment van overgang (T_top bereikt)
    logToGoogleSheet("Opwarmen tot Afkoelen");
    
    // Geef logging task tijd om request te verwerken (non-blocking)
    yield();
    
    // Schakel over naar koelen
    verwarmen_actief = false;
    koelenAan();
    
    // Feed watchdog na relais schakelen
    yield();
    
    // Update timers
    koelen_start_tijd = millis();
    verwarmen_start_tijd = 0;
    
    // Extra watchdog feeding aan het einde van kritieke overgang
    yield();
  }
}

static void handleCoolingPhase() {
  // Start timer als die nog niet loopt
  if (koelen_start_tijd == 0) {
    koelen_start_tijd = millis();
  }
  
  // Check of we T_bottom bereikt hebben
  // Gebruik majority voting voor KRITIEKE meting (faseovergang afkoelen→opwarmen)
  float temp_for_check = getCriticalTemp();
  if (isnan(temp_for_check)) {
    return; // Wacht op geldige temperatuur meting
  }
  
  if (temp_for_check <= T_bottom) {
    // Kritieke overgang - extra watchdog feeding
    yield();
    cyclus_teller++;
    
    // Bereken en sla fasetijd op (moet VOOR logging gebeuren)
    last_koelen_duur = (koelen_start_tijd > 0) ? (millis() - koelen_start_tijd) : 0;
    last_koelen_start_tijd = koelen_start_tijd;
    
    // Sla temperatuur op op moment van overgang (T_bottom bereikt)
    last_transition_temp = temp_for_check;
    // Update g_avgTempC direct zodat scherm en log dezelfde waarde tonen
    g_avgTempC = temp_for_check;
    g_lastValidTempC = temp_for_check;
    
    // Update display direct met nieuwe temperatuur (zonder te wachten op normale update interval)
    updateTempDisplay(temp_for_check);
    
    // Log "Afkoelen tot Opwarmen" wanneer we overgaan van afkoelen naar opwarmen
    // De temperatuur is de temperatuur op het moment van overgang (T_bottom bereikt)
    // Dit moet ALTIJD gebeuren, ook als het de laatste cyclus is
    logToGoogleSheet("Afkoelen tot Opwarmen");
    
    // Check of max aantal cycli bereikt is (NA logging van laatste afkoelfase)
    // Stop pas NA de laatste cyclus is afgerond
    // Bij cyclus_max = 2: stop pas NA cyclus 2 is afgerond (cyclus_teller = 3)
    // Dus check moet zijn: cyclus_teller > cyclus_max (niet >=)
    if (cyclus_max > 0 && cyclus_teller > cyclus_max) {
      // Laatste cyclus afgerond - log "Uit" en stop systeem
      logToGoogleSheet("Uit");
      cyclus_actief = false;
      systeem_uit = true;
      alleRelaisUit();
      verwarmen_start_tijd = 0;
      koelen_start_tijd = 0;
      return;
    }
    
    // Schakel over naar verwarmen voor volgende cyclus
    verwarmen_actief = true;
    verwarmenAan();
    verwarmen_start_tijd = millis();
    koelen_start_tijd = 0;
  }
}

// Cyclus logica
// Veilige implementatie met watchdog feeding tijdens kritieke operaties
void cyclusLogica() {
  // Feed watchdog aan het begin
  yield();
  
  // Reset koelingsfase_actief als cyclus actief is
  if (cyclus_actief) {
    if (koelingsfase_actief) {
      koelingsfase_actief = false;
      veiligheidskoeling_start_tijd = 0;
      veiligheidskoeling_naloop_start_tijd = 0;
    }
  }
  
  // Veiligheidskoeling fase - alleen als cyclus NIET actief is EN systeem uit is
  if (!cyclus_actief && koelingsfase_actief && systeem_uit) {
    handleSafetyCooling();
    yield(); // Feed watchdog na veiligheidskoeling
    return;
  }
  
  // Normale cyclus logica - alleen als cyclus actief is en systeem niet uit
  if (!cyclus_actief || systeem_uit || isnan(g_currentTempC)) {
    return;
  }
  
  // Extra veiligheidscheck
  if (koelingsfase_actief) {
    koelingsfase_actief = false;
    veiligheidskoeling_start_tijd = 0;
    veiligheidskoeling_naloop_start_tijd = 0;
  }
  
  // BELANGRIJK: Feed watchdog voor fase handling
  yield();
  
  if (verwarmen_actief) {
    handleHeatingPhase(); // Kan lang duren bij T_top bereik
  } else {
    handleCoolingPhase();
  }
  
  // BELANGRIJK: Feed watchdog na fase handling
  yield();
}

// Display en touchscreen configuratie

// Touchscreen pins
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;

// DRAW_BUF_SIZE definitie (moet na LVGL include, dus hier)
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))  // Terug naar /10 nu we PSRAM gebruiken
// draw_buf is al gedeclareerd hierboven

// If logging is enabled, it will inform the user about what is happening in the library
void log_print(lv_log_level_t level, const char * buf) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}

// Get the Touchscreen data
void touchscreen_read(lv_indev_t * indev, lv_indev_data_t * data) {
  // Checks if Touchscreen was touched, and prints X, Y and Pressure (Z)
  if(touchscreen.tirqTouched() && touchscreen.touched()) {
    // Get Touchscreen points
    TS_Point p = touchscreen.getPoint();

    // Advanced Touchscreen calibration, LEARN MORE » https://RandomNerdTutorials.com/touchscreen-calibration/
    float alpha_x, beta_x, alpha_y, beta_y, delta_x, delta_y;

    // REPLACE WITH YOUR OWN CALIBRATION VALUES » https://RandomNerdTutorials.com/touchscreen-calibration/
    alpha_x = 0.000;
    beta_x = 0.089;
    delta_x = -26.000;
    alpha_y = 0.067;
    beta_y = -0.001;
    delta_y = -19.000;

    x = alpha_y * p.x + beta_y * p.y + delta_y;
    // clamp x between 0 and SCREEN_WIDTH - 1
    x = max(0, x);
    x = min(SCREEN_WIDTH - 1, x);

    y = alpha_x * p.x + beta_x * p.y + delta_x;
    // clamp y between 0 and SCREEN_HEIGHT - 1
    y = max(0, y);
    y = min(SCREEN_HEIGHT - 1, y);

    // Basic Touchscreen calibration points with map function to the correct width and height
    //x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    //y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);

    z = p.z;

    data->state = LV_INDEV_STATE_PRESSED;

    // Set the coordinates
    data->point.x = x;
    data->point.y = y;

  }
  else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// Temperature display is now handled directly in updateGUI() function with color coding

// Forward declarations
bool allocate_buffers(void);
void init_graph_data(void);
void lv_create_graph_screen(void);
void graph_button_event(lv_event_t * e);
void back_button_event(lv_event_t * e);
void log_graph_data(void);
// update_graph_data() verwijderd - niet meer nodig met eenvoudige circulaire array
void update_graph_display(void);
void update_graph_y_axis_labels(void);
void fill_chart_completely(void);

// Globale GUI objecten
lv_obj_t * text_label_temp;
lv_obj_t * text_label_temp_value;
lv_obj_t * text_label_cyclus;
lv_obj_t * text_label_status;
lv_obj_t * text_label_t_top;
lv_obj_t * text_label_t_bottom;
lv_obj_t * text_label_verwarmen_tijd;
lv_obj_t * text_label_koelen_tijd;
lv_obj_t * text_label_cyclus_max;
lv_obj_t * text_label_version;
lv_obj_t * btn_start;
lv_obj_t * btn_stop;
lv_obj_t * btn_graph;
lv_obj_t * btn_t_top_plus;
lv_obj_t * btn_t_top_minus;
lv_obj_t * btn_t_bottom_plus;
lv_obj_t * btn_t_bottom_minus;
lv_obj_t * btn_temp_plus;
lv_obj_t * btn_temp_minus;
lv_obj_t * btn_cyclus_plus;
lv_obj_t * btn_cyclus_minus;
lv_obj_t * screen_main;
lv_obj_t * screen_graph;
lv_obj_t * chart;
lv_chart_series_t * chart_series_rising;  // Rode lijn voor stijgende temperatuur
lv_chart_series_t * chart_series_falling; // Blauwe lijn voor dalende temperatuur
lv_obj_t * y_axis_labels[6];  // Array voor Y-as labels (6 labels)

// Helper functie voor event handlers: sla instelling op en log wijziging
static void saveAndLogSetting(const char* log_msg, bool update_graph = false) {
  saveSettings();
  if (update_graph && lv_scr_act() == screen_graph) {
    update_graph_y_axis_labels();
  }
  logToGoogleSheet(log_msg);
}

// Zet alle knoppen grijs tijdens initialisatie
void setAllButtonsGray() {
  uint32_t gray_color = 0x808080; // Grijs
  
  if (btn_temp_minus != nullptr) lv_obj_set_style_bg_color(btn_temp_minus, lv_color_hex(gray_color), LV_PART_MAIN);
  if (btn_temp_plus != nullptr) lv_obj_set_style_bg_color(btn_temp_plus, lv_color_hex(gray_color), LV_PART_MAIN);
  if (btn_t_top_minus != nullptr) lv_obj_set_style_bg_color(btn_t_top_minus, lv_color_hex(gray_color), LV_PART_MAIN);
  if (btn_t_top_plus != nullptr) lv_obj_set_style_bg_color(btn_t_top_plus, lv_color_hex(gray_color), LV_PART_MAIN);
  if (btn_t_bottom_minus != nullptr) lv_obj_set_style_bg_color(btn_t_bottom_minus, lv_color_hex(gray_color), LV_PART_MAIN);
  if (btn_t_bottom_plus != nullptr) lv_obj_set_style_bg_color(btn_t_bottom_plus, lv_color_hex(gray_color), LV_PART_MAIN);
  if (btn_start != nullptr) lv_obj_set_style_bg_color(btn_start, lv_color_hex(gray_color), LV_PART_MAIN);
  if (btn_graph != nullptr) lv_obj_set_style_bg_color(btn_graph, lv_color_hex(gray_color), LV_PART_MAIN);
  // btn_stop blijft grijs, die hoeft niet aangepast
  
  // Force direct update voor betere responsiviteit
  lv_task_handler();
  delay(10); // Korte delay om update te laten renderen
  lv_task_handler();
}

// Zet alle knoppen terug naar normale kleuren na succesvolle initialisatie
void setAllButtonsNormal() {
  if (btn_temp_minus != nullptr) lv_obj_set_style_bg_color(btn_temp_minus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blauw
  if (btn_temp_plus != nullptr) lv_obj_set_style_bg_color(btn_temp_plus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blauw
  if (btn_t_top_minus != nullptr) lv_obj_set_style_bg_color(btn_t_top_minus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blauw
  if (btn_t_top_plus != nullptr) lv_obj_set_style_bg_color(btn_t_top_plus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blauw
  if (btn_t_bottom_minus != nullptr) lv_obj_set_style_bg_color(btn_t_bottom_minus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blauw
  if (btn_t_bottom_plus != nullptr) lv_obj_set_style_bg_color(btn_t_bottom_plus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blauw
  if (btn_start != nullptr) lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x00AA00), LV_PART_MAIN); // Groen
  if (btn_graph != nullptr) lv_obj_set_style_bg_color(btn_graph, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blauw
  // btn_stop blijft grijs (normale status)
  
  // Force direct update voor betere responsiviteit
  lv_task_handler();
  delay(10); // Korte delay om update te laten renderen
  lv_task_handler();
}

void lv_create_main_gui(void) {
  // Maak main screen object
  screen_main = lv_scr_act();
  
  // Regel 1: Thermokoppel (links), rechts Status: [status]
  text_label_temp = lv_label_create(lv_scr_act());
  lv_label_set_text(text_label_temp, "Thermokoppel");
  lv_obj_set_style_text_align(text_label_temp, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(text_label_temp, LV_ALIGN_TOP_LEFT, 10, 10);
  
  // Status label (rechts boven)
  text_label_status = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_status, "Status: Uit");
  lv_obj_set_style_text_align(text_label_status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(text_label_status, LV_ALIGN_TOP_RIGHT, -5, 10);
  lv_obj_set_width(text_label_status, 230); // Breedte verhoogd voor langere tekst zoals "Status: Veiligheidskoeling"
  lv_label_set_long_mode(text_label_status, LV_LABEL_LONG_CLIP); // Tekst knippen i.p.v. wrappen - voorkomt regelafbreking

  // Regel 2: Grote temperatuur waarde
  text_label_temp_value = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_temp_value, "--.--°C");
  lv_obj_set_style_text_align(text_label_temp_value, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(text_label_temp_value, LV_ALIGN_TOP_LEFT, 10, 27); // 5px omhoog (was 32, nu 27)
  static lv_style_t style_temp;
  lv_style_init(&style_temp);
  lv_style_set_text_font(&style_temp, &lv_font_montserrat_36);
  lv_obj_add_style(text_label_temp_value, &style_temp, 0);

  // Regel 3: Cycli [-] [+] knoppen (uitgelijnd met Top/Dal knoppen), rechts Cycli: xxx/yyy
  // Cycli label (links, uitgelijnd met Top/Dal labels)
  // Note: We gebruiken text_label_cyclus voor de tekst rechts, maar we kunnen een apart label toevoegen voor links als nodig
  // Voor nu: cycli knoppen direct na label positie
  
  // Cycli [-] knop (uitgelijnd met Top/Dal knoppen op x=125)
  btn_temp_minus = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_temp_minus, 25, 25);
  lv_obj_align(btn_temp_minus, LV_ALIGN_TOP_LEFT, 125, 68); // 10px omhoog voor gelijke afstand (was 78, nu 68)
  lv_obj_set_style_bg_color(btn_temp_minus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blue
  lv_obj_t * btn_temp_minus_label = lv_label_create(btn_temp_minus);
  lv_label_set_text(btn_temp_minus_label, "-");
  lv_obj_center(btn_temp_minus_label);
  lv_obj_add_event_cb(btn_temp_minus, temp_minus_event, LV_EVENT_ALL, NULL);
  
  // Cycli [+] knop (uitgelijnd met Top/Dal knoppen op x=160)
  btn_temp_plus = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_temp_plus, 25, 25);
  lv_obj_align(btn_temp_plus, LV_ALIGN_TOP_LEFT, 160, 68); // 10px omhoog voor gelijke afstand (was 78, nu 68)
  lv_obj_set_style_bg_color(btn_temp_plus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blue
  lv_obj_t * btn_temp_plus_label = lv_label_create(btn_temp_plus);
  lv_label_set_text(btn_temp_plus_label, "+");
  lv_obj_center(btn_temp_plus_label);
  lv_obj_add_event_cb(btn_temp_plus, temp_plus_event, LV_EVENT_ALL, NULL);
  
  // Cyclus teller (rechts, regel 3) - formaat: "Cycli: xxx/yyy" of "Cycli: xxx/inf"
  text_label_cyclus = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_cyclus, "Cycli: 1/inf");
  lv_obj_set_style_text_align(text_label_cyclus, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(text_label_cyclus, LV_ALIGN_TOP_RIGHT, -5, 70); // 8px omhoog voor gelijke afstand (was 78, nu 70)
  lv_obj_set_width(text_label_cyclus, 120);

  // Regel 4: Top (links) met [-] [+] knoppen, rechts opwarmen timer
  text_label_t_top = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_t_top, "Top: 80.0°C");
  lv_obj_set_style_text_align(text_label_t_top, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(text_label_t_top, LV_ALIGN_TOP_LEFT, 10, 100); // 24px naar beneden (was 76, nu 100)
  lv_obj_set_width(text_label_t_top, 120);

  // Top [-] knop
  btn_t_top_minus = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_t_top_minus, 25, 25);
  lv_obj_align(btn_t_top_minus, LV_ALIGN_TOP_LEFT, 125, 98); // 24px naar beneden (was 74, nu 98)
  lv_obj_set_style_bg_color(btn_t_top_minus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blue
  lv_obj_t * btn_t_top_minus_label = lv_label_create(btn_t_top_minus);
  lv_label_set_text(btn_t_top_minus_label, "-");
  lv_obj_center(btn_t_top_minus_label);
  lv_obj_add_event_cb(btn_t_top_minus, t_top_minus_event, LV_EVENT_ALL, NULL);

  // Top [+] knop
  btn_t_top_plus = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_t_top_plus, 25, 25);
  lv_obj_align(btn_t_top_plus, LV_ALIGN_TOP_LEFT, 160, 98); // 24px naar beneden (was 74, nu 98)
  lv_obj_set_style_bg_color(btn_t_top_plus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blue
  lv_obj_t * btn_t_top_plus_label = lv_label_create(btn_t_top_plus);
  lv_label_set_text(btn_t_top_plus_label, "+");
  lv_obj_center(btn_t_top_plus_label);
  lv_obj_add_event_cb(btn_t_top_plus, t_top_plus_event, LV_EVENT_ALL, NULL);
  
  // Opwarmen timer (rechts)
  text_label_verwarmen_tijd = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_verwarmen_tijd, "opwarmen: 0:00");
  lv_obj_set_style_text_align(text_label_verwarmen_tijd, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(text_label_verwarmen_tijd, LV_ALIGN_TOP_RIGHT, -5, 100); // 24px naar beneden (was 76, nu 100)
  lv_obj_set_width(text_label_verwarmen_tijd, 140); // Verhoogd van 120 naar 140 voor tijden boven 10:00

  // Regel 5: Dal (links) met [-] [+] knoppen, rechts afkoelen timer
  text_label_t_bottom = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_t_bottom, "Dal: 25.0°C");
  lv_obj_set_style_text_align(text_label_t_bottom, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(text_label_t_bottom, LV_ALIGN_TOP_LEFT, 10, 130); // 24px naar beneden (was 106, nu 130)
  lv_obj_set_width(text_label_t_bottom, 120);

  // Dal [-] knop
  btn_t_bottom_minus = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_t_bottom_minus, 25, 25);
  lv_obj_align(btn_t_bottom_minus, LV_ALIGN_TOP_LEFT, 125, 128); // 24px naar beneden (was 104, nu 128)
  lv_obj_set_style_bg_color(btn_t_bottom_minus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blue
  lv_obj_t * btn_t_bottom_minus_label = lv_label_create(btn_t_bottom_minus);
  lv_label_set_text(btn_t_bottom_minus_label, "-");
  lv_obj_center(btn_t_bottom_minus_label);
  lv_obj_add_event_cb(btn_t_bottom_minus, t_bottom_minus_event, LV_EVENT_ALL, NULL);

  // Dal [+] knop
  btn_t_bottom_plus = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn_t_bottom_plus, 25, 25);
  lv_obj_align(btn_t_bottom_plus, LV_ALIGN_TOP_LEFT, 160, 128); // 24px naar beneden (was 104, nu 128)
  lv_obj_set_style_bg_color(btn_t_bottom_plus, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blue
  lv_obj_t * btn_t_bottom_plus_label = lv_label_create(btn_t_bottom_plus);
  lv_label_set_text(btn_t_bottom_plus_label, "+");
  lv_obj_center(btn_t_bottom_plus_label);
  lv_obj_add_event_cb(btn_t_bottom_plus, t_bottom_plus_event, LV_EVENT_ALL, NULL);
  
  // Afkoelen timer (rechts)
  text_label_koelen_tijd = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_koelen_tijd, "afkoelen: 0:00");
  lv_obj_set_style_text_align(text_label_koelen_tijd, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(text_label_koelen_tijd, LV_ALIGN_TOP_RIGHT, -5, 130); // 24px naar beneden (was 106, nu 130)
  lv_obj_set_width(text_label_koelen_tijd, 140); // Verhoogd van 120 naar 140 voor tijden boven 10:00

  // Status regels tussen Dal: en START knop
  // Init status (tijdens initialisatie) - links uitgelijnd
  init_status_label = lv_label_create(lv_screen_active());
  lv_label_set_text(init_status_label, "WiFi initialiseren");
  lv_obj_set_style_text_align(init_status_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(init_status_label, LV_ALIGN_TOP_LEFT, 10, 167);
  lv_obj_set_style_text_color(init_status_label, lv_color_hex(0x000000), LV_PART_MAIN); // Zwart
  lv_obj_set_style_text_font(init_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
  
  // WiFi status (na initialisatie) - links uitgelijnd, dicht bij Google Sheets status
  wifi_status_label = lv_label_create(lv_screen_active());
  lv_label_set_text(wifi_status_label, ""); // Start leeg
  lv_obj_set_style_text_align(wifi_status_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(wifi_status_label, LV_ALIGN_BOTTOM_LEFT, 10, -65); // 15 pixels boven Google Sheets status (-50)
  lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0x888888), LV_PART_MAIN); // Standaard grijs
  lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
  
  // Google Sheets status (na initialisatie) - links uitgelijnd, zelfde hoogte als versienummering
  gs_status_label = lv_label_create(lv_screen_active());
  lv_label_set_text(gs_status_label, ""); // Start leeg
  lv_obj_set_style_text_align(gs_status_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(gs_status_label, LV_ALIGN_BOTTOM_LEFT, 10, -50); // Zelfde hoogte als versienummering (rechts)
  lv_obj_set_style_text_color(gs_status_label, lv_color_hex(0x888888), LV_PART_MAIN); // Standaard grijs
  lv_obj_set_style_text_font(gs_status_label, &lv_font_montserrat_14, LV_PART_MAIN);

  // Create Start button (initieel groen - systeem niet actief)
  btn_start = lv_btn_create(lv_scr_act());
  lv_obj_set_size(btn_start, 80, 40);
  lv_obj_align(btn_start, LV_ALIGN_BOTTOM_LEFT, 10, -5);
  lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x00AA00), LV_PART_MAIN); // Green (systeem niet actief)
  lv_obj_t * btn_start_label = lv_label_create(btn_start);
  lv_label_set_text(btn_start_label, "START");
  lv_obj_center(btn_start_label);
  lv_obj_add_event_cb(btn_start, start_button_event, LV_EVENT_ALL, NULL);

  // Create Graph button (gecentreerd)
  btn_graph = lv_btn_create(lv_scr_act());
  lv_obj_set_size(btn_graph, 100, 40); // Iets breder voor "GRAFIEK"
  lv_obj_align(btn_graph, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(btn_graph, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blue
  lv_obj_t * btn_graph_label = lv_label_create(btn_graph);
  lv_label_set_text(btn_graph_label, "GRAFIEK");
  lv_obj_center(btn_graph_label);
  lv_obj_add_event_cb(btn_graph, graph_button_event, LV_EVENT_ALL, NULL);

  // Create Stop button (initieel grijs - systeem niet actief)
  btn_stop = lv_btn_create(lv_scr_act());
  lv_obj_set_size(btn_stop, 80, 40);
  lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
  lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x808080), LV_PART_MAIN); // Grey (systeem niet actief)
  lv_obj_t * btn_stop_label = lv_label_create(btn_stop);
  lv_label_set_text(btn_stop_label, "STOP");
  lv_obj_center(btn_stop_label);
  lv_obj_add_event_cb(btn_stop, stop_button_event, LV_EVENT_ALL, NULL);

  // Temperature will be updated directly in updateGUI() function every 0.3 seconds
  
  // Versienummer label - klein net boven STOP knop
  text_label_version = lv_label_create(lv_scr_act());
  char version_str[16];
  snprintf(version_str, sizeof(version_str), "v%d.%02d", FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR);
  lv_label_set_text(text_label_version, version_str);
  lv_obj_set_style_text_color(text_label_version, lv_color_hex(0x888888), LV_PART_MAIN); // Grijs
  lv_obj_set_style_text_opa(text_label_version, LV_OPA_70, LV_PART_MAIN); // Iets transparant voor subtiele weergave
  lv_obj_set_style_text_font(text_label_version, &lv_font_montserrat_14, LV_PART_MAIN); // Kleine font
  lv_obj_align(text_label_version, LV_ALIGN_BOTTOM_RIGHT, -10, -55); // Net boven STOP knop (knop op -10, -10, label 45px erboven)
  
  // Maak grafiek scherm
  lv_create_graph_screen();
}

// ---- Grafiek scherm creatie ----
void lv_create_graph_screen(void) {
  // Maak nieuw scherm voor grafiek (NULL = nieuw scherm object)
  screen_graph = lv_obj_create(NULL);
  lv_obj_clear_flag(screen_graph, LV_OBJ_FLAG_SCROLLABLE); // Disable scrolling
  
  // Titel label
  lv_obj_t * title_label = lv_label_create(screen_graph);
  lv_label_set_text(title_label, "Temperatuur (graden C - minuten)");
  lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 2);
  
  // Chart maken - maximaal gebruik van beschikbare ruimte (320x200 scherm)
  chart = lv_chart_create(screen_graph);
  lv_obj_set_size(chart, 280, 155); // Veel groter: 280px breed, 155px hoog (maximale hoogte)
  lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 35, 18); // 35px van links (ruimte voor Y-waarden), 18px van boven
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, GRAPH_POINTS);
  // Chart range wordt dynamisch ingesteld op basis van T_top (20-T_top)
  // Wordt bijgewerkt in update_graph_y_axis_labels()
  lv_chart_set_axis_min_value(chart, LV_CHART_AXIS_PRIMARY_Y, 20);
  lv_chart_set_axis_max_value(chart, LV_CHART_AXIS_PRIMARY_Y, 120); // Initieel 20-120°C, wordt bijgewerkt
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(chart, 6, 10); // 6 horizontal, 10 vertical division lines
  
  // Y-as numerieke labels - dynamisch op basis van T_top (6 labels: T_top tot 20°C)
  // Chart Y start = 18, hoogte = 155, met 6 divisie lijnen verdeelt LVGL de chart in 7 gelijke delen
  // Labels worden bijgewerkt in update_graph_y_axis_labels() wanneer T_top verandert
  for (int i = 0; i <= 5; i++) {
    y_axis_labels[i] = lv_label_create(screen_graph);
    lv_obj_set_style_text_align(y_axis_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
    // Y positie: chart start (18) + (i * 155/5) voor labels op elke divisie
    // Dit verdeelt het bereik in 5 intervallen: 155/5 = 31px per interval
    // -5 voor verticale centering van het label op de divisie lijn
    // Extra aanpassing: top blijft, rest steeds 4px hoger (dus i*4 pixels extra omhoog)
    // Alle labels 10px naar beneden (onderlinge afstand blijft gelijk)
    // Aanvullende -2 voor betere uitlijning met divisielijnen
    int y_pos = 18 + (i * 155 / 5) - 5 - (i * 4) + 10 - 2;
    lv_obj_align(y_axis_labels[i], LV_ALIGN_TOP_LEFT, 10, y_pos);
  }
  // Initialiseer labels met initiële waarden
  update_graph_y_axis_labels();
  
  // Series toevoegen: rood voor stijgende, blauw voor dalende temperatuur
  chart_series_rising = lv_chart_add_series(chart, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y);  // Rood
  chart_series_falling = lv_chart_add_series(chart, lv_color_hex(0x0066CC), LV_CHART_AXIS_PRIMARY_Y); // Blauw
  
  // X-as minutenlabels onder de grafiek - uitgelijnd onder elke verticale divisielijn (10 labels: 9 8 7 6 5 4 3 2 1 0)
  // Chart start X = 35, breedte = 280px, 10 verticale divisielijnen verdeelt de chart in 11 gelijke delen
  // Elke divisielijn staat op: 35 + (i * 280 / 11) vanaf links (i = 0 tot 10)
  // Labels moeten onderaan de grafiek staan: Y = 18 (chart start) + 155 (hoogte) + offset
  int chart_x_start = 35;
  int chart_width = 280;
  int chart_y_start = 18;
  int chart_height = 155;
  int num_vertical_divs = 10; // 10 verticale divisielijnen
  int num_labels = 10; // 10 labels van 9 tot 0
  
  for (int i = 0; i < num_labels; i++) {
    lv_obj_t * x_value_label = lv_label_create(screen_graph);
    int minute_value = 9 - i; // 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
    char label_text[8];
    snprintf(label_text, sizeof(label_text), "%d", minute_value);
    lv_label_set_text(x_value_label, label_text);
    lv_obj_set_style_text_align(x_value_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // Basis X positie berekening
    int base_x_pos = chart_x_start + (i * chart_width / (num_vertical_divs + 1)) + (i * 4) + 6;
    
    // Individuele aanpassingen per label:
    // 9: 0px (was +2, nu 2px naar links t.o.v. origineel)
    // 8, 7: zo houden (geen aanpassing)
    // 6, 5, 4, 3: -2px naar links
    // 2: -4px naar links (was -2, nu nog 2px verder)
    // 1: -2px naar links
    // 0: -6px naar links
    int offset = 0;
    if (minute_value == 9) {
      offset = 0;   // Was +2, nu 0 (2px naar links)
    } else if (minute_value == 8 || minute_value == 7) {
      offset = 0;   // Zo houden
    } else if (minute_value == 2) {
      offset = -4;  // Was -2, nu -4 (nog 2px verder naar links)
    } else if (minute_value >= 3 && minute_value <= 6) {
      offset = -2;  // 2px naar links
    } else if (minute_value == 1) {
      offset = -2;  // 2px naar links
    } else if (minute_value == 0) {
      offset = -6;  // 6px naar links
    }
    
    int x_pos = base_x_pos + offset;
    int y_pos = chart_y_start + chart_height + 3; // 3px onder de grafiek
    
    lv_obj_align(x_value_label, LV_ALIGN_TOP_LEFT, x_pos, y_pos);
  }
  
  // Terug knop
  lv_obj_t * btn_back = lv_btn_create(screen_graph);
  lv_obj_set_size(btn_back, 100, 40);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x0066CC), LV_PART_MAIN); // Blauw in plaats van grijs
  lv_obj_t * btn_back_label = lv_label_create(btn_back);
  lv_label_set_text(btn_back_label, "TERUG");
  lv_obj_center(btn_back_label);
  lv_obj_add_event_cb(btn_back, back_button_event, LV_EVENT_ALL, NULL);
}

// ---- GUI Update functies ----
void updateGUI() {
  // Update temperature display met mediaan (elke 285ms, synchroon met sampling)
  unsigned long now = millis();
  if (now - g_lastDisplayMs >= TEMP_DISPLAY_UPDATE_MS) {
    g_lastDisplayMs = now;
    
    // Gebruik mediaan temperatuur (g_avgTempC bevat de mediaan van laatste 7 metingen)
    if (!isnan(g_avgTempC)) {
      // Gebruik char array i.p.v. String om heap fragmentatie te voorkomen
      char temp_text[16];
      snprintf(temp_text, sizeof(temp_text), "%.1f°C", g_avgTempC);
      lv_label_set_text(text_label_temp_value, temp_text);
      
      // Color coding: groen onder TEMP_SAFE_THRESHOLD (veilig), rood vanaf TEMP_SAFE_THRESHOLD (niet veilig aanraken)
      if (g_avgTempC < TEMP_SAFE_THRESHOLD) {
        // Onder 37°C: groen (veilig aanraken)
        lv_obj_set_style_text_color(text_label_temp_value, lv_color_hex(0x00AA00), 0);
      } else {
        // 37°C en hoger: rood (niet veilig aanraken)
        lv_obj_set_style_text_color(text_label_temp_value, lv_color_hex(0xCC0000), 0);
      }
    } else if (!isnan(g_lastValidTempC)) {
      // Geen nieuwe metingen, maar toon laatste geldige waarde (grijs voor indicatie)
      char temp_text[16];
      snprintf(temp_text, sizeof(temp_text), "%.1f°C", g_lastValidTempC);
      lv_label_set_text(text_label_temp_value, temp_text);
      lv_obj_set_style_text_color(text_label_temp_value, lv_color_hex(0x808080), 0); // Grijs voor oude waarde
    } else {
      // Geen metingen en geen laatste geldige waarde - toon --.--
      lv_label_set_text(text_label_temp_value, "--.--°C");
      lv_obj_set_style_text_color(text_label_temp_value, lv_color_hex(0x808080), 0);
    }
  }
  
  // Update cyclus teller - formaat: "Cycli: xxx/yyy" of "Cycli: xxx/inf"
  // VERPLAATST NAAR CycleController module - gebruik getters
  char cyclus_text[32];
  int current_cyclus = cycleController.getCycleCount();
  int max_cyclus = cyclus_max; // Nog via globale variabele (kan later naar CycleController)
  if (max_cyclus == 0) {
    snprintf(cyclus_text, sizeof(cyclus_text), "Cycli: %d/inf", current_cyclus);
  } else {
    snprintf(cyclus_text, sizeof(cyclus_text), "Cycli: %d/%d", current_cyclus, max_cyclus);
  }
  lv_label_set_text(text_label_cyclus, cyclus_text);
  
  // Update status - gebruik char array i.p.v. String
  // VERPLAATST NAAR CycleController module - gebruik getters
  const char* status_suffix = "";
  if (cycleController.isSafetyCooling()) {
    status_suffix = "Veiligheidskoeling";
  } else if (cycleController.isSystemOff()) {
    status_suffix = "Uit";
  } else if (cycleController.isActive()) {
    status_suffix = cycleController.isHeating() ? "Verwarmen" : "Koelen";
  } else {
    status_suffix = "Gereed";
  }
  char status_text[64];
  snprintf(status_text, sizeof(status_text), "Status: %s", status_suffix);
  lv_label_set_text(text_label_status, status_text);
  
  // Update temperatuur instellingen - gebruik char array i.p.v. String
  char t_top_text[32];
  snprintf(t_top_text, sizeof(t_top_text), "Top: %.1f°C", T_top);
  lv_label_set_text(text_label_t_top, t_top_text);
  
  char t_bottom_text[32];
  snprintf(t_bottom_text, sizeof(t_bottom_text), "Dal: %.1f°C", T_bottom);
  lv_label_set_text(text_label_t_bottom, t_bottom_text);
  
  // Update timer weergaven - gebruik char array i.p.v. String
  // VERPLAATST NAAR CycleController module - gebruik getters
  if (cycleController.isActive() && cycleController.isHeating()) {
    // We zijn aan het verwarmen
    unsigned long verwarmen_verstreken = cycleController.getHeatingElapsed();
    char tijd_str[16];
    formatTijdChar(verwarmen_verstreken, tijd_str, sizeof(tijd_str));
    char verwarmen_text[32];
    snprintf(verwarmen_text, sizeof(verwarmen_text), "Opwarmen: %s", tijd_str);
    lv_label_set_text(text_label_verwarmen_tijd, verwarmen_text);
    // Zet koelen timer op 0:00 tijdens verwarmen
    lv_label_set_text(text_label_koelen_tijd, "Afkoelen: 0:00");
  } else if (cycleController.isActive() && !cycleController.isHeating()) {
    // We zijn aan het koelen
    unsigned long koelen_verstreken = cycleController.getCoolingElapsed();
    char tijd_str[16];
    formatTijdChar(koelen_verstreken, tijd_str, sizeof(tijd_str));
    char koelen_text[32];
    snprintf(koelen_text, sizeof(koelen_text), "Afkoelen: %s", tijd_str);
    lv_label_set_text(text_label_koelen_tijd, koelen_text);
    // Zet verwarmen timer op 0:00 tijdens koelen
    lv_label_set_text(text_label_verwarmen_tijd, "Opwarmen: 0:00");
  } else {
    // Geen actieve fase of timer niet gestart
    lv_label_set_text(text_label_verwarmen_tijd, "Opwarmen: 0:00");
    lv_label_set_text(text_label_koelen_tijd, "Afkoelen: 0:00");
  }
  
  // Update knop kleuren op basis van systeemstatus
  // VERPLAATST NAAR CycleController module - gebruik getters
  if (cycleController.isSafetyCooling()) {
    // Veiligheidskoeling: beide knoppen grijs (geen bediening mogelijk)
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x808080), LV_PART_MAIN); // Grey
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x808080), LV_PART_MAIN);  // Grey
  } else if (cycleController.isActive() && !cycleController.isSystemOff()) {
    // Systeem actief: START grijs, STOP rood
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x808080), LV_PART_MAIN); // Grey
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0xAA0000), LV_PART_MAIN);  // Red
  } else {
    // Systeem niet actief: START groen, STOP grijs
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x00AA00), LV_PART_MAIN); // Green
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x808080), LV_PART_MAIN);  // Grey
  }
}

// ---- Event handlers ----
void start_button_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    uiController.onStartButton();
  }
}

void stop_button_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    uiController.onStopButton();
  }
}

// ---- Grafiek scherm event handlers ----
// Functie om alle punten in de grafiek te wissen
void clear_chart() {
  if (chart == nullptr || chart_series_rising == nullptr || chart_series_falling == nullptr) {
    return;
  }
  
  // Wis alle punten door ze op LV_CHART_POINT_NONE te zetten
  // Loop door alle punten en zet beide series op LV_CHART_POINT_NONE
  for (int i = 0; i < GRAPH_POINTS; i++) {
    lv_chart_set_next_value(chart, chart_series_rising, LV_CHART_POINT_NONE);
    lv_chart_set_next_value(chart, chart_series_falling, LV_CHART_POINT_NONE);
  }
}

// RADICAAL EENVOUDIGE AANPAK: Sorteer alle punten op tijd en toon laatste 120
void fill_chart_completely() {
  // Null pointer checks
  if (chart == NULL || chart_series_rising == NULL || chart_series_falling == NULL) {
    return;
  }
  
  if (graph_temps == nullptr || graph_times == nullptr) {
    return;
  }
  
  if (!graph_data_ready || graph_count == 0) {
    return; // Nog geen data
  }
  
  // Wis eerst de chart
  clear_chart();
  
  // EENVOUDIGE CIRCULAIRE ARRAY LOGICA:
  // graph_write_index wijst naar de volgende positie om te schrijven
  // Als graph_count < GRAPH_POINTS: punten staan op indices 0 tot graph_count-1
  // Als graph_count == GRAPH_POINTS: oudste punt staat op graph_write_index, nieuwste op (graph_write_index - 1 + GRAPH_POINTS) % GRAPH_POINTS
  // Voor weergave: begin bij oudste punt en loop graph_count punten verder
  
  int points_to_show = (graph_count < GRAPH_POINTS) ? graph_count : GRAPH_POINTS;
  
  // Bepaal start index: als buffer nog niet vol is, begin bij 0, anders bij graph_write_index
  int start_index = (graph_count < GRAPH_POINTS) ? 0 : graph_write_index;
  
  // Loop door alle punten in chronologische volgorde (oudste eerst)
  for (int j = 0; j < points_to_show; j++) {
    // Bereken index: begin bij start_index (oudste) en loop verder
    int i = (start_index + j) % GRAPH_POINTS;
    
    if (i < 0 || i >= GRAPH_POINTS) continue;
    
    // Check of dit punt geldig is
    if (graph_times[i] == 0 || isnan(graph_temps[i]) || 
        graph_temps[i] < -50.0 || graph_temps[i] > TEMP_MAX) {
      // Ongeldig punt: skip
      continue;
    }
    
    float min_temp = 20.0;
    float max_temp = T_top;
    int chart_value = (int)(graph_temps[i] + 0.5);
    chart_value = constrain(chart_value, (int)min_temp, (int)max_temp);
    add_chart_point_with_trend(i, graph_temps[i], chart_value);
    
    if (j % 10 == 0) {
      yield(); // Feed watchdog
      lv_task_handler(); // Laat LVGL touch events verwerken tijdens grafiek opbouw
    }
  }
}

void graph_button_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    uiController.onGraphButton();
  }
}

void back_button_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    uiController.onBackButton();
  }
}

// ---- Grafiek data functies ----
void log_graph_data() {
  // Null pointer check voor arrays
  if (graph_temps == nullptr || graph_times == nullptr) {
    return;
  }
  
  // Log zolang er nog activiteit is: tijdens cyclus OF tijdens veiligheidskoeling
  // BELANGRIJK: Grafiek moet continu blijven van START tot systeem UIT
  // Dit betekent: loggen tijdens normale cyclus EN tijdens veiligheidskoeling na STOP
  // Stop pas met loggen wanneer systeem volledig uit is (niet tijdens veiligheidskoeling)
  if (!cyclus_actief && !koelingsfase_actief) {
    return; // Geen actieve cyclus of veiligheidskoeling - geen logging
  }
  
  // Log temperatuur data elke 5 seconden
  unsigned long now = millis();
  
  // BELANGRIJK: Gebruik mediaan temperatuur voor grafiek (net zoals statusovergangen)
  // Mediaan is robuuster tegen uitschieters dan enkele meting
  float temp_for_graph = getMedianTemp();
  
  // Accepteer alle waarden behalve NAN (ook 0.0 is geldig)
  bool valid_temp = !isnan(temp_for_graph) && temp_for_graph >= -50.0 && temp_for_graph <= TEMP_MAX;
  
  // BELANGRIJK: graph_last_log_time wordt alleen gereset bij START, niet bij cyclus overgangen
  // Dit zorgt ervoor dat de grafiek continu blijft tijdens alle cycli
  if (valid_temp && (now - graph_last_log_time >= TEMP_GRAPH_LOG_INTERVAL_MS)) {
    // EENVOUDIGE CIRCULAIRE ARRAY: Schrijf naar graph_write_index
    graph_temps[graph_write_index] = temp_for_graph;
    graph_times[graph_write_index] = now;
    
    // Update write index (wrapt automatisch rond)
    graph_write_index = (graph_write_index + 1) % GRAPH_POINTS;
    
    // Update count (max 120)
    if (graph_count < GRAPH_POINTS) {
      graph_count++;
    }
    
    // Markeer data als ready zodra eerste punt is geschreven
    graph_data_ready = true;
    
    // Update timer alleen na succesvolle opslag
      graph_last_log_time = now;
    
    // REAL-TIME UPDATE: Als grafiek scherm actief is, update direct
    // Dit zorgt voor minimale vertraging tussen data logging en grafiek weergave
    if (lv_scr_act() == screen_graph) {
      update_graph_display();
    }
  } else if (!valid_temp && (now - graph_last_log_time >= TEMP_GRAPH_LOG_INTERVAL_MS)) {
    // Temperatuur is ongeldig, maar tijd is verstreken
    // BELANGRIJK: Update graph_last_log_time om te voorkomen dat de functie blijft proberen
    // Dit voorkomt dat de grafiek "vastloopt" als de temperatuur langere tijd ongeldig is
    graph_last_log_time = now;
  }
}

// update_graph_data() verwijderd - niet meer nodig met eenvoudige circulaire array

// Update Y-as labels op basis van T_top (dynamisch: 20°C tot T_top)
void update_graph_y_axis_labels() {
  if (chart == nullptr) return;
  
  // Bepaal bereik: van 20°C tot T_top
  float min_temp = 20.0;
  float max_temp = T_top;
  float range = max_temp - min_temp; // Bereik tussen 20 en T_top
  
  // Update chart range
  lv_chart_set_axis_min_value(chart, LV_CHART_AXIS_PRIMARY_Y, (int)min_temp);
  lv_chart_set_axis_max_value(chart, LV_CHART_AXIS_PRIMARY_Y, (int)max_temp);
  
  // Update alle 6 labels (van boven naar beneden: T_top tot 20°C)
  // Gebruik char array i.p.v. String om heap fragmentatie te voorkomen
  char label_text[16];
  for (int i = 0; i <= 5; i++) {
    if (y_axis_labels[i] == nullptr) continue;
    
    // Bereken temperatuurwaarde voor deze divisielijn (van T_top naar 20°C)
    float temp_value = max_temp - (i * range / 5.0); // Verdeel in 5 intervallen
    int temp_int = (int)(temp_value + 0.5); // Rond af naar dichtstbijzijnde geheel getal
    snprintf(label_text, sizeof(label_text), "%d", temp_int);
    lv_label_set_text(y_axis_labels[i], label_text);
  }
}

// Helper functie om temperatuurtrend te bepalen en punt toe te voegen
void add_chart_point_with_trend(int index, float temp_value, int chart_value) {
  // Null pointer check voor arrays
  if (graph_temps == nullptr || graph_times == nullptr) {
    return;
  }
  
  // Array bounds check
  if (index < 0 || index >= GRAPH_POINTS) {
    return;
  }
  
  // Bepaal trend: vergelijk met vorige waarde
  bool rising = false;
  bool has_previous = false;
  
  if (index > 0) {
    // Normaal geval: vergelijk met vorige index
    int prev_index = index - 1;
    if (prev_index >= 0 && prev_index < GRAPH_POINTS) {
    if (!isnan(graph_temps[prev_index]) && graph_times[prev_index] > 0) {
      rising = temp_value > graph_temps[prev_index];
      has_previous = true;
      }
    }
  } else if (graph_count >= GRAPH_POINTS) {
    // Bij wrap-around (buffer vol): vergelijk met laatste punt in buffer
    // Laatste punt staat op (graph_write_index - 1 + GRAPH_POINTS) % GRAPH_POINTS
    int prev_index = (graph_write_index - 1 + GRAPH_POINTS) % GRAPH_POINTS;
    if (prev_index >= 0 && prev_index < GRAPH_POINTS) {
    if (!isnan(graph_temps[prev_index]) && graph_times[prev_index] > 0) {
      rising = temp_value > graph_temps[prev_index];
      has_previous = true;
      }
    }
  }
  
  // Als er geen vorige waarde is (eerste punt), gebruik blauw als default
  if (!has_previous) {
    rising = false; // Eerste punt = blauw (of we kunnen dit ook rood maken)
  }
  
  // Null check voor chart objecten
  if (chart == nullptr || chart_series_rising == nullptr || chart_series_falling == nullptr) {
    return;
  }
  
  // Voeg punt toe aan juiste series, zet andere op LV_CHART_POINT_NONE
  if (rising) {
    // Stijgende temperatuur -> rood
    lv_chart_set_next_value(chart, chart_series_rising, chart_value);
    lv_chart_set_next_value(chart, chart_series_falling, LV_CHART_POINT_NONE);
  } else {
    // Dalende temperatuur (of gelijk, of eerste punt) -> blauw
    lv_chart_set_next_value(chart, chart_series_rising, LV_CHART_POINT_NONE);
    lv_chart_set_next_value(chart, chart_series_falling, chart_value);
  }
}

// ECHTE SLIDING WINDOW: Voeg alleen nieuwe punten toe, LVGL verwijdert automatisch oude punten
// EENVOUDIGE SLIDING WINDOW: Voeg alleen nieuwe punten toe, LVGL verwijdert automatisch oude punten
void update_graph_display() {
  // Null pointer checks
  if (chart == NULL || chart_series_rising == NULL || chart_series_falling == NULL) {
    return;
  }
  
  if (graph_temps == nullptr || graph_times == nullptr) {
    return;
  }
  
  if (!graph_data_ready || graph_count == 0) {
    return; // Nog geen data
  }
  
  // Feed watchdog
  static unsigned long last_watchdog_feed = 0;
  if (millis() - last_watchdog_feed > 100) {
    yield();
    last_watchdog_feed = millis();
  }
  
  // Eerste keer of na scherm wissel: bouw grafiek op met alle beschikbare punten
  static unsigned long last_displayed_time = 0;
  
  // BELANGRIJK: Reset last_displayed_time bij force rebuild om nieuwe punten te kunnen tonen
  if (graph_force_rebuild) {
    last_displayed_time = 0; // Reset zodat fill_chart_completely alle punten toont
  }
  
  if (graph_force_rebuild || last_displayed_time == 0) {
    fill_chart_completely();
    // Bepaal tijd van nieuwste punt voor volgende update
    // BELANGRIJK: Loop door alle punten om de hoogste tijd te vinden
    // Dit werkt correct bij zowel weinig als veel punten
    unsigned long max_time = 0;
    int points_to_check = (graph_count < GRAPH_POINTS) ? graph_count : GRAPH_POINTS;
    int start_index = (graph_count < GRAPH_POINTS) ? 0 : graph_write_index;
    for (int j = 0; j < points_to_check; j++) {
      int i = (start_index + j) % GRAPH_POINTS;
      if (i >= 0 && i < GRAPH_POINTS && graph_times[i] > max_time) {
        max_time = graph_times[i];
      }
    }
    if (max_time > 0) {
      last_displayed_time = max_time;
  } else {
      // Als er geen punten zijn, zet last_displayed_time naar 0 zodat nieuwe punten worden getoond
      last_displayed_time = 0;
    }
    graph_force_rebuild = false; // Reset flag
    return;
  }
  
  // Continue update: voeg alle nieuwe punten toe sinds laatste update
  // EENVOUDIGE LOGICA: Loop door alle punten en voeg toe als tijd > last_displayed_time
  // Begin bij oudste punt en loop graph_count punten verder
  
  int points_to_check = (graph_count < GRAPH_POINTS) ? graph_count : GRAPH_POINTS;
  int start_index = (graph_count < GRAPH_POINTS) ? 0 : graph_write_index;
  int points_added = 0;
  unsigned long newest_time = last_displayed_time;
  
  // Loop door alle punten in chronologische volgorde (oudste eerst)
  for (int j = 0; j < points_to_check; j++) {
    // Bereken index: begin bij start_index (oudste) en loop verder
    int i = (start_index + j) % GRAPH_POINTS;
    
    if (i < 0 || i >= GRAPH_POINTS) break;
    
    // Check of dit punt nieuwer is dan laatst weergegeven
    if (graph_times[i] == 0 || graph_times[i] <= last_displayed_time) {
      continue; // Skip oude punten
    }
    
    // Check of dit punt geldig is
    if (isnan(graph_temps[i]) || graph_temps[i] < -50.0 || graph_temps[i] > TEMP_MAX) {
      // Ongeldig punt: voeg leeg punt toe maar update tijd wel
      lv_chart_set_next_value(chart, chart_series_rising, LV_CHART_POINT_NONE);
      lv_chart_set_next_value(chart, chart_series_falling, LV_CHART_POINT_NONE);
      if (graph_times[i] > newest_time) {
        newest_time = graph_times[i];
      }
      points_added++;
      continue;
    }
    
        float min_temp = 20.0;
        float max_temp = T_top;
        int chart_value = (int)(graph_temps[i] + 0.5);
        chart_value = constrain(chart_value, (int)min_temp, (int)max_temp);
        add_chart_point_with_trend(i, graph_temps[i], chart_value);
        
    if (graph_times[i] > newest_time) {
      newest_time = graph_times[i];
    }
      points_added++;
    
    if (points_added % 5 == 0) yield();
    }
    
  // Update laatste weergegeven tijd
  if (points_added > 0 && newest_time > last_displayed_time) {
    last_displayed_time = newest_time;
  }
}

// ---- Temperatuur instel event handlers ----
void t_top_plus_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    uiController.onTtopPlus();
  }
}

void t_top_minus_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    uiController.onTtopMinus();
  }
}

void t_bottom_plus_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    uiController.onTbottomPlus();
  }
}

void t_bottom_minus_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    uiController.onTbottomMinus();
  }
}

// ---- Temperatuur instel event handlers (worden gebruikt voor cycli instelling) ----
void temp_plus_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    cyclus_max++;
    if (cyclus_max > 999) cyclus_max = 999; // Max limiet
    char log_msg[32];
    if (cyclus_max == 0) {
      snprintf(log_msg, sizeof(log_msg), "Cycli+inf");
    } else {
      snprintf(log_msg, sizeof(log_msg), "Cycli+%d", cyclus_max);
    }
    saveAndLogSetting(log_msg);
  }
}

void temp_minus_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    cyclus_max--;
    if (cyclus_max < 0) cyclus_max = 0; // 0 = oneindig
    char log_msg[32];
    if (cyclus_max == 0) {
      snprintf(log_msg, sizeof(log_msg), "Cycli-inf");
    } else {
      snprintf(log_msg, sizeof(log_msg), "Cycli-%d", cyclus_max);
    }
    saveAndLogSetting(log_msg);
  }
}

void setup() {
  SPI.begin();
  thermocouple.begin(); // Oude instantie - wordt vervangen door tempSensor
  tempSensor.begin();
  
  // BELANGRIJK: Stel software SPI delay in voor betere communicatie stabiliteit
  // Dit voorkomt timing problemen bij hoge CPU belasting
  thermocouple.setSWSPIdelay(MAX6675_SW_SPI_DELAY_US);
  
  // BELANGRIJK: Gebruik char array i.p.v. String om heap fragmentatie te voorkomen
  char lvgl_version[64];
  snprintf(lvgl_version, sizeof(lvgl_version), "LVGL Library Version: %d.%d.%d", 
           lv_version_major(), lv_version_minor(), lv_version_patch());
  Serial.begin(115200);
  
  // Laad opgeslagen instellingen uit non-volatile memory
  // Initialiseer SettingsStore
  settingsStore.begin();
  loadSettings();
  
  // Laad cyclus_teller uit Preferences (voor persistentie bij reboot)
  int saved_cyclus_teller = settingsStore.loadCycleCount();
  
  // Initialiseer CycleController (na loadSettings zodat settings beschikbaar zijn)
  cycleController.begin(&tempSensor, &logger, RELAIS_KOELEN, RELAIS_VERWARMING);
  cycleController.setTargetTop(T_top);
  cycleController.setTargetBottom(T_bottom);
  cycleController.setMaxCycles(cyclus_max);
  cycleController.setCycleCount(saved_cyclus_teller); // Herstel cyclus_teller na reboot
  
  // Stel callback in voor logging (CycleController gebruikt logTransition() intern)
  cycleController.setTransitionCallback([](const char* status, float temp, unsigned long timestamp) {
    // CycleController handelt logging zelf af via logTransition()
    // Deze callback kan gebruikt worden voor extra acties indien nodig
  });
  
  // Stel callback in voor cyclus_teller opslag (voor persistentie bij reboot)
  cycleController.setCycleCountSaveCallback([](int cycleCount) {
    settingsStore.saveCycleCount(cycleCount);
  });

  // Initialize GPIO pins for relais
  pinMode(RELAIS_KOELEN, OUTPUT);
  pinMode(RELAIS_VERWARMING, OUTPUT);
  
  // Start with all relais off but system ready
  systeemGereed();

  // BELANGRIJK: MAX6675 warm-up tijd - wacht tot sensor gestabiliseerd is
  delay(MAX6675_POWERUP_DELAY_MS);
  delay(MAX6675_WARMUP_TIME_MS);
  
  // BELANGRIJK: Discard eerste paar metingen voor stabiliteit
  // Eerste metingen na power-up kunnen onnauwkeurig zijn
  for (int i = 0; i < 3; i++) {
    (void)tempSensor.read(); // Warm-up reads - gebruik private read() via friend of direct via public API
    delay(MAX6675_CONVERSION_TIME_MS); // Wacht conversietijd tussen reads
  }
  
  // Reset conversietijd timer na warm-up
  g_lastMax6675ReadTime = 0;
  
  // Initialiseer UIController (alloceert buffers en initialiseert grafiek data)
  if (!uiController.begin(FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR)) {
    Serial.println("KRITIEKE FOUT: Kan UIController niet initialiseren!");
    while(1) delay(1000); // Blijf hangen
  }
  
// Start LVGL
  lv_init();
  // Register print function for debugging
  lv_log_register_print_cb(log_print);

  // Start the SPI for the touchscreen and init the touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  // Set the Touchscreen rotation in landscape mode
  // Note: in some displays, the touchscreen might be upside down, so you might need to set the rotation to 0: touchscreen.setRotation(0);
  touchscreen.setRotation(2);

  // Create a display object
  lv_display_t * disp;
  // Initialize the TFT display using the TFT_eSPI library
  // Bereken juiste buffer grootte op basis van LV_COLOR_DEPTH (nu beschikbaar na LVGL init)
  size_t actual_draw_buf_size = SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8);
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, uiController.getDrawBuf(), actual_draw_buf_size);
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
  
  // Initialize an LVGL input device object (Touchscreen)
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  // Set the callback function to read Touchscreen input
  lv_indev_set_read_cb(indev, touchscreen_read);
  // Opmerking: lv_indev_set_read_period() is niet beschikbaar in deze LVGL versie
  // Touch response wordt verbeterd door vaker lv_task_handler() aan te roepen in loop()

  // Maak schermen (UIController heeft al buffers gealloceerd en grafiek data geïnitialiseerd)
  uiController.createMainScreen();
  uiController.createGraphScreen();
  
  // Stel callbacks in voor UIController
  uiController.setStartCallback([]() {
    logToGoogleSheet("START");
    cycleController.start();
  });
  
  uiController.setStopCallback([]() {
    logToGoogleSheet("STOP");
    cycleController.stop();
  });
  
  uiController.setGraphResetCallback([]() {
    // Reset grafiek data bij START
    float* graph_temps = uiController.getGraphTemps();
    unsigned long* graph_times = uiController.getGraphTimes();
    if (graph_temps != nullptr && graph_times != nullptr) {
      for (int i = 0; i < 120; i++) {
        graph_temps[i] = NAN;
        graph_times[i] = 0;
      }
      uiController.setGraphWriteIndex(0);
      uiController.setGraphCount(0);
      uiController.setGraphDataReady(false);
      uiController.setGraphLastLogTime(0);
      uiController.setGraphForceRebuild(false);
    }
  });
  
  uiController.setSettingChangeCallback([](const char* setting, float value) {
    if (strcmp(setting, "T_top") == 0) {
      T_top += value;
      if (T_top >= TEMP_MAX) T_top = TEMP_MAX;
      if (T_top < T_bottom + TEMP_MIN_DIFF) T_top = T_bottom + TEMP_MIN_DIFF;
      char log_msg[32];
      snprintf(log_msg, sizeof(log_msg), value > 0 ? "Top+%.1f" : "Top-%.1f", T_top);
      saveAndLogSetting(log_msg, true);
      cycleController.setTargetTop(T_top);
    } else if (strcmp(setting, "T_bottom") == 0) {
      T_bottom += value;
      if (T_bottom > T_top - TEMP_MIN_DIFF) T_bottom = T_top - TEMP_MIN_DIFF;
      if (T_bottom < 0.0) T_bottom = 0.0;
      char log_msg[32];
      snprintf(log_msg, sizeof(log_msg), value > 0 ? "Dal+%.1f" : "Dal-%.1f", T_bottom);
      saveAndLogSetting(log_msg);
      cycleController.setTargetBottom(T_bottom);
    } else if (strcmp(setting, "temp_offset") == 0) {
      temp_offset += value;
      if (temp_offset < -10.0) temp_offset = -10.0;
      if (temp_offset > 10.0) temp_offset = 10.0;
      char log_msg[32];
      snprintf(log_msg, sizeof(log_msg), value > 0 ? "Offset+%.1f" : "Offset-%.1f", temp_offset);
      saveAndLogSetting(log_msg);
      tempSensor.setOffset(temp_offset);
    }
  });
  
  // Stel status callbacks in
  uiController.setIsActiveCallback([]() { return cycleController.isActive(); });
  uiController.setIsHeatingCallback([]() { return cycleController.isHeating(); });
  uiController.setIsSystemOffCallback([]() { return cycleController.isSystemOff(); });
  uiController.setIsSafetyCoolingCallback([]() { return cycleController.isSafetyCooling(); });
  uiController.setGetCycleCountCallback([]() { return cycleController.getCycleCount(); });
  uiController.setGetHeatingElapsedCallback([]() { return cycleController.getHeatingElapsed(); });
  uiController.setGetCoolingElapsedCallback([]() { return cycleController.getCoolingElapsed(); });
  uiController.setGetMedianTempCallback([]() { return getMedianTemp(); });
  uiController.setGetLastValidTempCallback([]() { return g_lastValidTempC; });
  uiController.setTtopCallback([]() { return T_top; });
  uiController.setTbottomCallback([]() { return T_bottom; });
  uiController.setCyclusMaxCallback([]() { return cyclus_max; });
  
  // Zet alle knoppen grijs tijdens initialisatie
  uiController.setButtonsGray();
  
  // Toon initialisatie status
  uiController.showInitStatus("WiFi initialiseren", 0x000000); // Zwart
  
  // ---- WiFi en Google Sheets initialisatie ----
  // STAP 1: Probeer eerst WiFi Station verbinding (nodig voor Google Sheets logging)
  WiFi.mode(WIFI_STA); // Start in Station mode
  
  // WiFiManager voor WiFi Station configuratie
  WiFiManager wm;
  wm.setConfigPortalTimeout(180); // Timeout na 3 minuten
  
  // Callback voor wanneer config portal (AP) wordt gestart
  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    IPAddress apIP = WiFi.softAPIP();
    char apStatus[64];
    snprintf(apStatus, sizeof(apStatus), "WiFi: Config AP [%s]", apIP.toString().c_str());
    showWifiStatus(apStatus, false); // Grijs
    lv_task_handler();
  });
  
  // Probeer verbinding te maken met opgeslagen Station credentials
  showWifiStatus("WiFi: verbinden...", false); // Grijs
  lv_task_handler(); // Update display
  
  bool res = wm.autoConnect("ESP32-TC");
  
  if (!res) {
    // Geen Station verbinding - Google Sheets logging werkt niet
    uiController.showWifiStatus("WiFi Station: geen verbinding", true); // Rood (fout)
    // Blijf doorgaan zonder WiFi - logging werkt dan niet
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    // Verberg init status zodra WiFi verbonden is
    uiController.hideInitStatus();
    
    // BELANGRIJK: Gebruik char array i.p.v. String om heap fragmentatie te voorkomen
    // Toon SSID en Station IP
    char wifiInfo[128];
    snprintf(wifiInfo, sizeof(wifiInfo), "WiFi: %s (%s)", 
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    uiController.showWifiStatus(wifiInfo, false); // Grijs (succes)
    
    // Configureer NTP tijd met lokale tijdzone offset (+1 uur = 3600 seconden)
    uiController.showGSStatus("Google Sheets: tijd synchroniseren...", false); // Grijs
    systemClock.begin(3600); // GMT offset: +1 uur (3600 seconden), geen daylight saving offset
    systemClock.sync(); // Synchroniseer NTP tijd
    
    // Laad Google credentials uit Preferences (als beschikbaar)
    GoogleCredentials creds = settingsStore.loadGoogleCredentials();
    const char* clientEmail = (strlen(creds.clientEmail) > 0) ? creds.clientEmail : CLIENT_EMAIL;
    const char* projectId = (strlen(creds.projectId) > 0) ? creds.projectId : PROJECT_ID;
    const char* privateKey = (strlen(creds.privateKey) > 0) ? creds.privateKey : PRIVATE_KEY;
    const char* spreadsheetId = (strlen(creds.spreadsheetId) > 0) ? creds.spreadsheetId : SPREADSHEET_ID;
    
    // Laad NTFY instellingen
    char ntfyTopic[64] = "";
    NtfyNotificationSettings ntfySettings;
    settingsStore.loadNtfySettings(ntfyTopic, sizeof(ntfyTopic), ntfySettings);
    
    // Initialiseer NTFY notifier
    if (strlen(ntfyTopic) > 0) {
        ntfyNotifier.begin(ntfyTopic);
        ntfyNotifier.setSettings(ntfySettings);
        // Koppel NTFY aan logger
        logger.setNtfyNotifier(&ntfyNotifier);
    }
    
    // Initialiseer Logger module (handelt Google Sheets client af)
    uiController.showGSStatus("Google Sheets: authenticeren...", false); // Grijs
    if (logger.begin(clientEmail, projectId, privateKey, spreadsheetId, &systemClock)) {
    // Wacht op token authenticatie (max 30 seconden)
      // BELANGRIJK: Logger task roept sheetClient.ready() aan om token authenticatie te verwerken
    int auth_tries = 0;
      while (!logger.isTokenReady() && auth_tries < 60) {
      delay(500);
      yield(); // BELANGRIJK: Feed watchdog in while loop
      lv_task_handler(); // Update display tijdens wachten
      auth_tries++;
      if (auth_tries % 10 == 0) {
        uiController.showGSStatus("Google Sheets: wachten...", false); // Grijs
      }
    }
    
      if (logger.isTokenReady()) {
        uiController.showGSStatus("Google Sheets: GEREED", false); // Grijs (succes)
        g_googleAuthTokenReady = true; // Update globale variabele voor backward compatibility
        delay(1000); // Korte pauze
      } else {
        uiController.showGSStatus("Google Sheets: MISLUKT", true); // Rood (fout)
        delay(1000);
      }
    } else {
      uiController.showGSStatus("Google Sheets: MISLUKT", true); // Rood (fout)
      delay(1000);
    }
    
    // STAP 2: Start nu AP voor web interface (alleen als WiFi Station verbonden is)
    // Zet WiFi in AP+STA mode (zodat we tegelijk verbonden kunnen zijn met extern netwerk EN een eigen AP hebben)
    WiFi.mode(WIFI_AP_STA);
    
    // Start Access Point voor web interface
    const char* apSSID = "ESP32-TC";
    const char* apPassword = ""; // Geen wachtwoord voor eenvoudige toegang
    IPAddress apIP(192, 168, 4, 1);
    IPAddress apGateway(192, 168, 4, 1);
    IPAddress apSubnet(255, 255, 255, 0);
    
    WiFi.softAPConfig(apIP, apGateway, apSubnet);
    WiFi.softAP(apSSID, apPassword);
    
    // Update WiFi status met SSID en Station IP
    snprintf(wifiInfo, sizeof(wifiInfo), "WiFi: %s (%s)", 
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    uiController.showWifiStatus(wifiInfo, false); // Grijs
    
    // Update AP status
    uiController.showAPStatus("ESP32-TC", WiFi.softAPIP().toString().c_str());
    
    // Start webserver voor web interface
    webServer.setSettingsStore(&settingsStore);
    webServer.setCycleController(&cycleController);
    webServer.setTempSensor(&tempSensor);
    webServer.setUIController(&uiController);
    
    // Stel callbacks in voor acties
    webServer.setStartCallback([]() {
      cycleController.start();
      logToGoogleSheet("START");
    });
    
    webServer.setStopCallback([]() {
      logToGoogleSheet("STOP");
      cycleController.stop();
    });
    
    // Settings change callback
    webServer.setSettingsChangeCallback([](float tTop, float tBottom, float tempOffset, int cycleMax,
                                            const char* clientEmail, const char* projectId,
                                            const char* privateKey, const char* spreadsheetId) {
      T_top = tTop;
      T_bottom = tBottom;
      temp_offset = tempOffset;
      cyclus_max = cycleMax;

      // Valideer en corrigeer indien nodig
      if (T_top >= TEMP_MAX) T_top = TEMP_MAX;
      if (T_top < T_bottom + TEMP_MIN_DIFF) T_top = T_bottom + TEMP_MIN_DIFF;
      if (T_bottom > T_top - TEMP_MIN_DIFF) T_bottom = T_top - TEMP_MIN_DIFF;
      if (T_bottom < 0.0) T_bottom = 0.0;
      if (temp_offset < -10.0) temp_offset = -10.0;
      if (temp_offset > 10.0) temp_offset = 10.0;
      if (cyclus_max < 0) cyclus_max = 0;

      // Sla instellingen op
      Settings settings;
      settings.tTop = T_top;
      settings.tBottom = T_bottom;
      settings.tempOffset = temp_offset;
      settings.cycleMax = cyclus_max;
      settingsStore.save(settings);
      
      // Sla Google credentials apart op (om RAM te besparen)
      if (clientEmail && strlen(clientEmail) > 0) {
        GoogleCredentials creds;
        strncpy(creds.clientEmail, clientEmail, sizeof(creds.clientEmail) - 1);
        creds.clientEmail[sizeof(creds.clientEmail) - 1] = '\0';
        
        if (projectId && strlen(projectId) > 0) {
          strncpy(creds.projectId, projectId, sizeof(creds.projectId) - 1);
          creds.projectId[sizeof(creds.projectId) - 1] = '\0';
        }
        if (privateKey && strlen(privateKey) > 0) {
          strncpy(creds.privateKey, privateKey, sizeof(creds.privateKey) - 1);
          creds.privateKey[sizeof(creds.privateKey) - 1] = '\0';
        }
        if (spreadsheetId && strlen(spreadsheetId) > 0) {
          strncpy(creds.spreadsheetId, spreadsheetId, sizeof(creds.spreadsheetId) - 1);
          creds.spreadsheetId[sizeof(creds.spreadsheetId) - 1] = '\0';
        }
        settingsStore.saveGoogleCredentials(creds);
        
        // Herinitialiseer Logger als Google credentials zijn gewijzigd
        if (WiFi.status() == WL_CONNECTED) {
          uiController.showGSStatus("Google Sheets: herinitialiseren...", false);
          logger.begin(clientEmail, projectId, privateKey, spreadsheetId, &systemClock);
        }
      }

      // Pas instellingen toe
      tempSensor.setOffset(temp_offset);
      cycleController.setTargetTop(T_top);
      cycleController.setTargetBottom(T_bottom);
      cycleController.setMaxCycles(cyclus_max);

      // Log wijziging
      char log_msg[64];
      snprintf(log_msg, sizeof(log_msg), "Web: Top=%.1f, Dal=%.1f, Offset=%.1f, Max=%d",
               T_top, T_bottom, temp_offset, cyclus_max);
      logToGoogleSheet(log_msg);
    });
    
    // Stel status callbacks in
    webServer.setGetCurrentTempCallback([]() { return g_currentTempC; });
    webServer.setGetMedianTempCallback([]() { return getMedianTemp(); });
    webServer.setIsActiveCallback([]() { return cycleController.isActive(); });
    webServer.setIsHeatingCallback([]() { return cycleController.isHeating(); });
    webServer.setGetCycleCountCallback([]() { return cycleController.getCycleCount(); });
    webServer.setGetTtopCallback([]() { return T_top; });
    webServer.setGetTbottomCallback([]() { return T_bottom; });
    webServer.setGetCycleMaxCallback([]() { return cyclus_max; });
    webServer.setGetTempOffsetCallback([]() { return temp_offset; });
    
    // Google credentials getters (laad uit Preferences)
    webServer.setGetClientEmailCallback([]() -> const char* {
      static GoogleCredentials creds;
      creds = settingsStore.loadGoogleCredentials();
      return (const char*)creds.clientEmail;
    });
    webServer.setGetProjectIdCallback([]() -> const char* {
      static GoogleCredentials creds;
      creds = settingsStore.loadGoogleCredentials();
      return (const char*)creds.projectId;
    });
    webServer.setGetPrivateKeyCallback([]() -> const char* {
      static GoogleCredentials creds;
      creds = settingsStore.loadGoogleCredentials();
      return (const char*)creds.privateKey;
    });
    webServer.setGetSpreadsheetIdCallback([]() -> const char* {
      static GoogleCredentials creds;
      creds = settingsStore.loadGoogleCredentials();
      return (const char*)creds.spreadsheetId;
    });
    
    // NTFY callbacks
    webServer.setGetNtfyTopicCallback([]() -> const char* {
      return ntfyNotifier.getTopic();
    });
    webServer.setGetNtfySettingsCallback([]() -> NtfyNotificationSettings {
      return ntfyNotifier.getSettings();
    });
    webServer.setSaveNtfySettingsCallback([](const char* topic, const NtfyNotificationSettings& settings) {
      if (topic != nullptr && strlen(topic) > 0) {
        ntfyNotifier.setTopic(topic);
      }
      ntfyNotifier.setSettings(settings);
      settingsStore.saveNtfySettings(topic, settings);
    });
    
    // Start webserver
    webServer.begin();
    
    // Toon webserver IP in serial monitor
    Serial.print("Webserver gestart op AP: http://");
    Serial.println(WiFi.softAPIP());
    Serial.print("WiFi Station verbonden: ");
    Serial.println(WiFi.localIP());
  } else {
    // Geen Station verbinding - Google Sheets logging werkt niet
    uiController.showGSStatus("Google Sheets: WiFi Station nodig", false); // Grijs
    delay(1000);
  }
  
  // Verberg initialisatie status (status regels blijven zichtbaar)
  uiController.hideInitStatus();
  
  // Zet alle knoppen terug naar normale kleuren (initialisatie compleet)
  uiController.setButtonsNormal();
  
  // Logger module is al geïnitialiseerd in WiFi sectie hierboven
  // (Geen globale variabelen meer nodig - Logger module handelt alles intern af)
}

void loop() {
  // BELANGRIJK: Feed watchdog timer om crashes te voorkomen
  // Dit is de eerste regel om watchdog altijd te voeden
  yield();
  
  // Handle webserver requests (AP is alleen actief als WiFi Station verbonden is)
  webServer.handleClient();
  
  // BELANGRIJK: LVGL task handler - roep dit VAKER aan voor betere responsiviteit
  // Meerdere calls voor snellere touch response (vooral belangrijk voor terug-knop)
  lv_task_handler();
  yield(); // Feed watchdog na LVGL operaties
  lv_task_handler(); // Extra call voor snellere response
  yield();
  
  // Google Sheets token refresh wordt nu gedaan in logging task op Core 1
  
  // Behandel logging success feedback (vanuit Core 1 via Logger module)
  if (logger.hasLogSuccess()) {
    uiController.showGSSuccessCheckmark();  // Toon groen "LOGGING" tekst
    logger.clearLogSuccess();  // Reset flag
    // Update globale variabelen voor backward compatibility
    g_logSuccessFlag = false;
    g_logSuccessTime = logger.getLogSuccessTime(); // Update tijdstempel
  }
  
  // Reset LOGGING status na 1 seconde (via UIController)
  uiController.updateGSStatusReset();
  
  // Temperatuur meting (elke 0.3 seconde)
  // VERPLAATST NAAR TempSensor module
  tempSensor.sample();
  // Update globale variabelen voor backward compatibility
  g_currentTempC = tempSensor.getCurrent();
  g_avgTempC = tempSensor.getMedian();
  g_lastValidTempC = tempSensor.getLastValid();
  yield(); // Feed watchdog na temperatuur meting
  lv_task_handler(); // Laat LVGL touch events verwerken
  
  // Update cyclus logica
  // VERPLAATST NAAR CycleController module
  cycleController.update();
  yield(); // Feed watchdog na cyclus logica
  lv_task_handler(); // Laat LVGL touch events verwerken (belangrijk voor responsiviteit)
  
  // Log grafiek data
  uiController.logGraphData();
  yield(); // Feed watchdog na grafiek logging
  lv_task_handler(); // Laat LVGL touch events verwerken
  
  // Update GUI labels (alleen op main scherm)
  static unsigned long last_gui_update = 0;
  if (lv_scr_act() == uiController.getMainScreen()) {
    if (millis() - last_gui_update >= GUI_UPDATE_INTERVAL_MS) {
      uiController.update();
      last_gui_update = millis();
    }
  } else if (lv_scr_act() == uiController.getGraphScreen()) {
    // Update grafiek wanneer op grafiek scherm (elke 0.5 seconde als backup)
    // REAL-TIME: Directe updates gebeuren al in logGraphData() bij nieuwe data
    unsigned long now = millis();
    if (now - g_lastGraphUpdateMs >= GRAPH_UPDATE_INTERVAL_MS) {
      g_lastGraphUpdateMs = now;
      uiController.updateGraph();
    }
  }
  
  // LVGL tick - update tijd voor LVGL intern
  lv_tick_inc(5);
  
  // BELANGRIJK: Vervang delay() door yield() + meerdere lv_task_handler() calls
  // Dit maakt touch input veel responsiever omdat LVGL vaker draait
  // In plaats van 5ms blokkeren, doen we 5x 1ms met LVGL updates ertussen
  for (int i = 0; i < 5; i++) {
    yield(); // Feed watchdog
    lv_task_handler(); // Laat LVGL touch events verwerken
    delay(1); // Korte delay (1ms) om CPU niet te overbelasten
  }
  
  // Extra lv_task_handler() call voor snellere touch response
  lv_task_handler();
  yield();
}