/*  versie 3.85
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
#include <Preferences.h>  // Voor opslag van instellingen in non-volatile memory
#include <MAX6675.h>
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
#define FIRMWARE_VERSION_MAJOR 3
#define FIRMWARE_VERSION_MINOR 85

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

// Kalibratie offset voor MAX6675 (wordt geladen uit Preferences)
float temp_offset = 0.0; // Kalibratie offset in °C

// Timer voor conversietijd respectering
static unsigned long g_lastMax6675ReadTime = 0;

// Interne functie voor één enkele MAX6675 read (zonder retry)
// Respecteert conversietijd en controleert status bits
static float readTempC_single() {
  // Respecteer conversietijd van MAX6675 (220ms typisch)
  // Voorkomt onstabiele metingen door te snelle opeenvolgende reads
  unsigned long now = millis();
  if (g_lastMax6675ReadTime > 0 && (now - g_lastMax6675ReadTime) < MAX6675_CONVERSION_TIME_MS) {
    return NAN; // Te snel na vorige read - conversie nog niet klaar
  }
  
  uint8_t state = thermocouple.read();
  g_lastMax6675ReadTime = now;
  
  if (state == 0) { // STATUS_OK
    // Controleer op open circuit (bit 2 in status)
    // De library zet bit 2 als thermocouple open circuit is
    uint8_t status = thermocouple.getStatus();
    if (status & 0x04) {
      // Thermocouple open circuit gedetecteerd
      // Return NAN om aan te geven dat meting ongeldig is
      return NAN;
    }
    
    float temp = thermocouple.getCelsius();
    
    // Data validatie - controleer op onrealistische waarden
    // K-type thermocouple bereik: -200°C tot 1200°C
    if (temp < -200.0 || temp > 1200.0) {
      return NAN; // Onrealistische waarde - waarschijnlijk corrupte data
    }
    
    // Pas kalibratie offset toe (wordt automatisch toegepast door library via setOffset)
    return temp;
  }
  
  // STATUS_ERROR of andere fout
  return NAN;
}

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

// MAX6675 temperatuurmeting met retry mechanisme voor betere robuustheid
static float readTempC() {
  float temp = NAN;
  
  // Probeer MAX6675_READ_RETRIES keer bij tijdelijke fouten
  for (int i = 0; i < MAX6675_READ_RETRIES; i++) {
    temp = readTempC_single();
    
    // Als we een geldige meting hebben, return direct
    if (!isnan(temp)) {
      return temp;
    }
    
    // Bij fout: wacht kort en probeer opnieuw (behalve laatste poging)
    if (i < (MAX6675_READ_RETRIES - 1)) {
      delay(MAX6675_RETRY_DELAY_MS);
    }
  }
  
  // Alle retries gefaald - return NAN
  return NAN;
}

// MAX6675 temperatuurmeting met majority voting voor KRITIEKE metingen
// Gebruik deze functie voor kritieke beslissingen zoals faseovergangen (T_top/T_bottom checks)
// Duurt ~90ms (3 samples * 30ms) - gebruik alleen waar nodig
static float readTempC_critical() {
  float samples[MAX6675_CRITICAL_SAMPLES];
  int valid_count = 0;
  
  // Doe meerdere reads met delay tussen samples
  for (int i = 0; i < MAX6675_CRITICAL_SAMPLES; i++) {
    float temp = readTempC(); // Gebruik normale read met retry
    
    if (!isnan(temp)) {
      samples[valid_count++] = temp;
    }
    
    // Wacht tussen samples (behalve laatste) - gebruik yield() + lv_task_handler() voor responsiviteit
    if (i < (MAX6675_CRITICAL_SAMPLES - 1)) {
      unsigned long delay_start = millis();
      while (millis() - delay_start < MAX6675_CRITICAL_SAMPLE_DELAY_MS) {
        yield(); // Feed watchdog
        lv_task_handler(); // Laat LVGL touch events verwerken
        delay(1); // Korte delay om CPU niet te overbelasten
      }
    }
  }
  
  // Als we minder dan 2 geldige samples hebben, return NAN (niet betrouwbaar)
  // Verlaagd van 3 naar 2 voor snellere respons (met 3 samples is 2 voldoende)
  if (valid_count < 2) {
    return NAN;
  }
  
  // Bereken mediaan van geldige samples
  return calculateMedianFromArray(samples, valid_count);
}

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

// Preferences object voor opslag van instellingen
Preferences preferences;
#define PREF_NAMESPACE "tempctrl"  // Namespace voor Preferences
#define PREF_KEY_T_TOP "t_top"
#define PREF_KEY_T_BOTTOM "t_bottom"
#define PREF_KEY_CYCLUS_MAX "cyclus_max"
#define PREF_KEY_TEMP_OFFSET "temp_offset"  // Kalibratie offset voor MAX6675
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
unsigned long ntp_sync_time_ms = 0; // millis() waarde toen NTP gesynchroniseerd werd
time_t ntp_sync_unix_time = 0; // Unix tijd toen NTP gesynchroniseerd werd

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

// Struct voor logging data (alleen POD types voor queue)
typedef struct {
  char status[LOG_STATUS_MAX_LEN];
  float temp;
  int cyclus_teller;
  int cyclus_max;
  float T_top;
  float T_bottom;
  char fase_tijd[LOG_FASE_TIJD_MAX_LEN];
  char cyclus_tijd[LOG_CYCLUS_TIJD_MAX_LEN]; // Totaaltijd van volledige cyclus (opwarmen + afkoelen)
  unsigned long timestamp_ms; // Timestamp in milliseconden (voor correcte timestamp bij fase wijziging)
} LogRequest;

QueueHandle_t logQueue = nullptr;
TaskHandle_t loggingTaskHandle = nullptr;

// Interne logging functie (wordt aangeroepen door logging task op Core 1)
void logToGoogleSheet_internal(const LogRequest* req);

// Preferences functies voor opslag van instellingen
void loadSettings() {
  preferences.begin(PREF_NAMESPACE, false); // false = read-write mode
  
  // Laad T_top (default: 80.0)
  T_top = preferences.getFloat(PREF_KEY_T_TOP, 80.0);
  // BELANGRIJK: Valideer T_top tegen TEMP_MAX (kan verhoogd zijn in nieuwe firmware)
  // Gebruik >= i.p.v. > om floating point problemen te voorkomen
  if (T_top >= TEMP_MAX) {
    T_top = TEMP_MAX;
    // Sla gecorrigeerde waarde op
    preferences.putFloat(PREF_KEY_T_TOP, T_top);
  }
  
  // Laad T_bottom (default: 25.0)
  T_bottom = preferences.getFloat(PREF_KEY_T_BOTTOM, 25.0);
  
  // Laad cyclus_max (default: 0 = oneindig)
  cyclus_max = preferences.getInt(PREF_KEY_CYCLUS_MAX, 0);
  
  // Laad temperatuur kalibratie offset (default: 0.0)
  temp_offset = preferences.getFloat(PREF_KEY_TEMP_OFFSET, 0.0);
  // Pas offset toe op MAX6675 library
  thermocouple.setOffset(temp_offset);
  
  preferences.end();
}

void saveSettings() {
  preferences.begin(PREF_NAMESPACE, false); // false = read-write mode
  
  // Sla T_top op
  preferences.putFloat(PREF_KEY_T_TOP, T_top);
  
  // Sla T_bottom op
  preferences.putFloat(PREF_KEY_T_BOTTOM, T_bottom);
  
  // Sla cyclus_max op
  preferences.putInt(PREF_KEY_CYCLUS_MAX, cyclus_max);
  
  // Sla temperatuur kalibratie offset op
  preferences.putFloat(PREF_KEY_TEMP_OFFSET, temp_offset);
  
  preferences.end();
}

// Token status callback functie
static void gs_tokenStatusCallback(TokenInfo info) {
  if (info.status == token_status_ready) {
    g_googleAuthTokenReady = true;
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

// Haal mediaan temperatuur op
// Vereenvoudigd: g_avgTempC wordt altijd bijgewerkt in sampleMax6675() met correcte chronologische volgorde
static float getMedianTemp() {
  // Gebruik de al berekende mediaan (wordt elke 285ms bijgewerkt in sampleMax6675())
  if (!isnan(g_avgTempC)) {
    return g_avgTempC;
  }
  
  // Fallback: gebruik laatste meting als mediaan nog niet beschikbaar is
  return g_currentTempC;
}

// Sample MAX6675 temperatuur (aanroepen vanuit loop())
// Nieuwe aanpak: elke 285ms een nieuwe waarde toevoegen aan circulaire array
// Mediaan wordt berekend en gebruikt voor display en logging
static void sampleMax6675()
{
  unsigned long now = millis();
  if (now - g_lastSampleMs < TEMP_SAMPLE_INTERVAL_MS) return;
  g_lastSampleMs = now;
  
  float t = readTempC();
  if (!isnan(t) && t > -200 && t < 1200) { // sanity window for K-type + MAX6675
    // Voeg nieuwe waarde toe aan circulaire array (oudste valt automatisch af)
    g_tempSamples[g_tempSampleIndex] = t;
    g_tempSampleIndex = (g_tempSampleIndex + 1) % TEMP_MEDIAN_SAMPLES;
    
    // Update aantal geldige metingen (max 7)
    if (g_tempSampleCount < TEMP_MEDIAN_SAMPLES) {
      g_tempSampleCount++;
    }
    
    // Kopieer waarden uit circulaire array in chronologische volgorde (oudste eerst)
    // Dit is nodig omdat de circulaire array wrapt en de volgorde niet chronologisch is
    float chrono_samples[TEMP_MEDIAN_SAMPLES];
    int count_to_use = (g_tempSampleCount >= TEMP_MEDIAN_SAMPLES) ? TEMP_MEDIAN_SAMPLES : g_tempSampleCount;
    
    // g_tempSampleIndex wijst naar de volgende positie (waar de volgende meting komt)
    // We moeten de laatste count_to_use metingen pakken in chronologische volgorde
    for (int i = 0; i < count_to_use; i++) {
      // Start vanaf de oudste meting en werk vooruit
      // Bij circulaire buffer: als index 3 is en count 7, dan zijn de metingen op posities:
      // (3-7+7)%7=3, (3-6+7)%7=4, (3-5+7)%7=5, (3-4+7)%7=6, (3-3+7)%7=0, (3-2+7)%7=1, (3-1+7)%7=2
      int idx = (g_tempSampleIndex - count_to_use + i + TEMP_MEDIAN_SAMPLES) % TEMP_MEDIAN_SAMPLES;
      chrono_samples[i] = g_tempSamples[idx];
    }
    
    // Bereken mediaan van de waarden in chronologische volgorde
    if (count_to_use >= TEMP_MEDIAN_SAMPLES) {
      // Array is vol - bereken mediaan van alle 7 waarden
      g_avgTempC = calculateMedianFromArray(chrono_samples, TEMP_MEDIAN_SAMPLES);
    } else if (count_to_use >= 2) {
      // Minimaal 2 waarden nodig voor mediaan
      g_avgTempC = calculateMedianFromArray(chrono_samples, count_to_use);
    } else {
      // Nog niet genoeg waarden - gebruik laatste meting
      g_avgTempC = t;
    }
    
    // Update huidige temperatuur en laatste geldige waarde
    g_currentTempC = g_avgTempC; // Gebruik mediaan als huidige waarde
    g_lastValidTempC = g_avgTempC; // Bewaar voor display bij fouten
  }
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
void getTimestampChar(char* buffer, size_t buffer_size) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    strncpy(buffer, "00-00-00 00:00:00", buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return;
  }
  
  snprintf(buffer, buffer_size, "%02d-%02d-%02d %02d:%02d:%02d",
           timeinfo.tm_year % 100,  // yy
           timeinfo.tm_mon + 1,      // mm
           timeinfo.tm_mday,         // dd
           timeinfo.tm_hour,         // hh
           timeinfo.tm_min,          // mm
           timeinfo.tm_sec);         // ss
}

// Helper functie voor timestamp op basis van millis() waarde
// Gebruik char array versie om heap fragmentatie te voorkomen
void getTimestampFromMillisChar(unsigned long timestamp_ms, char* buffer, size_t buffer_size) {
  if (ntp_sync_time_ms == 0 || ntp_sync_unix_time == 0) {
    // NTP nog niet gesynchroniseerd, gebruik huidige tijd
    getTimestampChar(buffer, buffer_size);
    return;
  }
  
  // Bereken verschil tussen opgeslagen tijd en sync tijd
  long diff_ms = (long)timestamp_ms - (long)ntp_sync_time_ms;
  
  // Converteer naar Unix tijd (seconden)
  time_t target_time = ntp_sync_unix_time + (diff_ms / 1000);
  
  // Converteer naar struct tm
  struct tm timeinfo;
  if (!localtime_r(&target_time, &timeinfo)) {
    getTimestampChar(buffer, buffer_size); // Fallback naar huidige tijd
    return;
  }
  
  snprintf(buffer, buffer_size, "%02d-%02d-%02d %02d:%02d:%02d",
           timeinfo.tm_year % 100,  // yy
           timeinfo.tm_mon + 1,      // mm
           timeinfo.tm_mday,         // dd
           timeinfo.tm_hour,         // hh
           timeinfo.tm_min,          // mm
           timeinfo.tm_sec);         // ss
}

// FreeRTOS Task voor Google Sheets logging op Core 1
// BELANGRIJK: Draait op Core 1 (samen met loop()) zodat Core 0 beschikbaar blijft voor real-time taken
void loggingTask(void* parameter) {
  LogRequest req;
  static unsigned long last_log_time = 0;
  const unsigned long MIN_LOG_INTERVAL_MS = 2000; // Minimaal 2 seconden tussen logs (voorkomt SSL overbelasting)
  
  while (true) {
    // Wacht op logging request in queue (korte timeout voor snelle respons)
    // Gebruik korte timeout (10ms) in plaats van 100ms voor snellere logging
    if (xQueueReceive(logQueue, &req, pdMS_TO_TICKS(10)) == pdTRUE) {
      // BELANGRIJK: Rate limiting om SSL overbelasting te voorkomen
      // Wacht minimaal 2 seconden tussen logging requests
      unsigned long now = millis();
      unsigned long time_since_last_log = (now >= last_log_time) ? (now - last_log_time) : (ULONG_MAX - last_log_time + now);
      
      if (time_since_last_log < MIN_LOG_INTERVAL_MS) {
        // Te snel na vorige log - wacht en probeer opnieuw
        // Stuur request terug naar queue (vooraan) om later opnieuw te proberen
        xQueueSendToFront(logQueue, &req, 0);
        vTaskDelay(pdMS_TO_TICKS(MIN_LOG_INTERVAL_MS - time_since_last_log));
        continue;
      }
      
      // Voer logging uit
      logToGoogleSheet_internal(&req);
      last_log_time = millis();
    }
    
    // Onderhoud Google Sheets token periodiek
    // BELANGRIJK: Gebruik alleen non-blocking aanroep - sheet.ready() is al non-blocking
    // Geen while loop om IDLE task niet te blokkeren (voorkomt watchdog timeout)
    if (WiFi.status() == WL_CONNECTED && g_googleAuthTokenReady) {
      static unsigned long last_token_refresh = 0;
      unsigned long now = millis();
      if (now - last_token_refresh >= 10000) { // Refresh elke 10 seconden
        // Eén non-blocking aanroep - sheet.ready() handelt token refresh intern af
        sheet.ready();
        last_token_refresh = millis();
      }
    }
    
    // Korte delay om CPU niet te overbelasten, maar kort genoeg voor snelle respons
    // Verlaagd van 50ms naar 5ms voor snellere logging
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// Publieke logging functie - stuurt request naar queue (niet-blokkerend)
void logToGoogleSheet(const char* status) {
  // Controleer of queue bestaat
  if (logQueue == nullptr) {
    return; // Queue niet geïnitialiseerd, skip logging
  }
  
  // Null pointer check
  if (status == nullptr) {
    return;
  }
  
  // Voorkom queue overflow - verwijder meerdere oude entries als queue vol is
  UBaseType_t queue_count = uxQueueMessagesWaiting(logQueue);
  if (queue_count >= LOG_QUEUE_SIZE - 1) {
    // Verwijder meerdere oude entries om ruimte te maken
    int removed = 0;
    LogRequest dummy;
    while (removed < LOG_QUEUE_CLEANUP_COUNT && xQueueReceive(logQueue, &dummy, 0) == pdTRUE) {
      removed++;
    }
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
  
  // Stuur naar queue (non-blocking)
  // BELANGRIJK: Gebruik timeout om te voorkomen dat requests verloren gaan
  // Als queue vol is, wacht kort (10ms) om ruimte te maken
  if (logQueue != nullptr) {
    if (xQueueSend(logQueue, &req, pdMS_TO_TICKS(10)) != pdTRUE) {
      // Queue vol of timeout - probeer oude entries te verwijderen en opnieuw te sturen
      LogRequest dummy;
      int removed = 0;
      while (removed < 3 && xQueueReceive(logQueue, &dummy, 0) == pdTRUE) {
        removed++;
      }
      // Probeer opnieuw te sturen
      xQueueSend(logQueue, &req, 0);
    }
  }
}

// Interne logging functie (uitgevoerd op Core 1 door logging task)
void logToGoogleSheet_internal(const LogRequest* req) {
  // BELANGRIJK: Null pointer check
  if (req == nullptr) {
    return;
  }
  
  // Controleer WiFi verbinding
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  // Controleer Google Sheets authenticatie
  if (!g_googleAuthTokenReady) {
    return;
  }
  
  // Maak timestamp - gebruik de opgeslagen timestamp_ms als die beschikbaar is
  // Dit zorgt ervoor dat de timestamp de starttijd van de fase is (niet de eindtijd)
  // Gebruik char array i.p.v. String om heap fragmentatie te voorkomen
  char timestamp[20];
  if (req->timestamp_ms > 0) {
    // Gebruik de opgeslagen starttijd om de juiste timestamp te berekenen
    getTimestampFromMillisChar(req->timestamp_ms, timestamp, sizeof(timestamp));
  } else {
    // Fallback naar huidige tijd
    getTimestampChar(timestamp, sizeof(timestamp));
  }
  
  // Bepaal totaal cycli string - gebruik char array i.p.v. String
  char totaal_cycli[16];
  if (req->cyclus_max == 0) {
    strncpy(totaal_cycli, "inf", sizeof(totaal_cycli) - 1);
    totaal_cycli[sizeof(totaal_cycli) - 1] = '\0';
  } else {
    snprintf(totaal_cycli, sizeof(totaal_cycli), "%d", req->cyclus_max);
  }
  
  // BELANGRIJK: Feed watchdog voordat we FirebaseJson objecten maken
  // Dit voorkomt watchdog timeout tijdens heap allocaties
  vTaskDelay(pdMS_TO_TICKS(1));
  
  // BELANGRIJK: Declareer success variabele BUITEN de FirebaseJson scope
  // zodat deze beschikbaar is na de scope
  bool success = false;
  unsigned long log_start = millis();
  int attempt = 0;
  const int MAX_ATTEMPTS = 3; // Eerste poging + 2 retries (verlaagd om SSL overbelasting te voorkomen)
  
  // Bereid FirebaseJson voor - gebruik scope om automatisch destructor aan te roepen
  // BELANGRIJK: FirebaseJson objecten kunnen heap fragmentatie veroorzaken
  // Scope zorgt ervoor dat destructors worden aangeroepen en geheugen wordt vrijgegeven
  {
    FirebaseJson response;
    FirebaseJson valueRange;
    
    // BELANGRIJK: Feed watchdog na object creatie
    vTaskDelay(pdMS_TO_TICKS(1));
    
    valueRange.add("majorDimension", "ROWS");
    
    // BELANGRIJK: Feed watchdog tijdens data toevoegen
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // Voeg de data toe (9 kolommen: Timestamp, Meettemperatuur, Status, Huidige cyclus, Totaal cycli, T_top, T_dal, Fase tijd, Cyclus tijd)
    valueRange.set("values/[0]/[0]", timestamp);           // Timestamp (char array)
    valueRange.set("values/[0]/[1]", req->temp);          // Meettemperatuur
    valueRange.set("values/[0]/[2]", req->status);        // Status
    valueRange.set("values/[0]/[3]", req->cyclus_teller); // Huidige cyclus
    valueRange.set("values/[0]/[4]", totaal_cycli);      // Totaal cycli
    valueRange.set("values/[0]/[5]", req->T_top);         // T_top
    valueRange.set("values/[0]/[6]", req->T_bottom);       // T_dal
    valueRange.set("values/[0]/[7]", req->fase_tijd);     // Fase tijd
    valueRange.set("values/[0]/[8]", req->cyclus_tijd);    // Cyclus tijd (totaaltijd opwarmen + afkoelen)
    
    // BELANGRIJK: Feed watchdog na data toevoegen, voor API call
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // Zorg dat token up-to-date is - één non-blocking aanroep
    // BELANGRIJK: Geen while loop om IDLE task niet te blokkeren (voorkomt watchdog timeout)
    // Token wordt periodiek gecontroleerd in loggingTask elke 10 seconden
    // Als token niet klaar is, zal logging falen en later opnieuw worden geprobeerd
    sheet.ready();
    
    // Log naar Google Sheets - werkblad "DataLog-K" - met timeout en retry
    // BELANGRIJK: Verbeterde error handling voor SSL fouten
    
    // BELANGRIJK: Geef SSL layer tijd om te herstellen tussen pogingen
    // Te veel snelle retries kunnen SSL verbinding overbelasten
    while ((millis() - log_start < 15000) && attempt < MAX_ATTEMPTS) { // Max 15 seconden timeout
      // BELANGRIJK: sheet.values.append() kan lang duren - geef andere tasks tijd
      // Dit voorkomt watchdog timeouts en stack overflows
      vTaskDelay(pdMS_TO_TICKS(10)); // Korte delay voor andere tasks (verhoogd van 1ms)
      
      // Probeer logging
      success = sheet.values.append(&response, SPREADSHEET_ID, "DataLog-K!A1", &valueRange);
      if (success) {
        break;
      }
      
      attempt++;
      
      // Delay alleen als we nog een retry gaan doen (niet na laatste poging)
      if (attempt < MAX_ATTEMPTS && (millis() - log_start < 15000)) {
        // BELANGRIJK: Langere delays tussen retries om SSL layer tijd te geven te herstellen
        // Exponential backoff: 500ms, 1000ms (was: 100ms, 200ms, 300ms)
        int delay_ms = 500 * attempt;
        // Verdeel delay in chunks om watchdog te voeden
        int chunks = delay_ms / 100; // 100ms chunks
        for (int i = 0; i < chunks; i++) {
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        // Rest delay
        if (delay_ms % 100 > 0) {
          vTaskDelay(pdMS_TO_TICKS(delay_ms % 100));
        }
      }
    }
    
    // BELANGRIJK: Feed watchdog na FirebaseJson operaties, voor destructors
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // FirebaseJson objecten worden hier automatisch vrijgegeven (destructor)
    // BELANGRIJK: Destructors kunnen heap operaties doen - feed watchdog
  } // FirebaseJson objecten worden hier vrijgegeven
  
  // BELANGRIJK: Feed watchdog na FirebaseJson scope (na destructors)
  vTaskDelay(pdMS_TO_TICKS(1));
  
  if (success) {
    // Zet flag voor visuele feedback (wordt verwerkt in main loop op Core 1)
    g_logSuccessFlag = true;
    g_logSuccessTime = millis();
  } else {
    // Logging mislukt - zet error flag (optioneel voor debugging)
    // BELANGRIJK: Zelfs bij falen doorgaan, anders stopt alle logging
    // De volgende logging request kan wel slagen
    
    // Geef SSL layer extra tijd om te herstellen na fout
    // Dit voorkomt dat opeenvolgende requests de SSL verbinding overbelasten
    vTaskDelay(pdMS_TO_TICKS(500)); // Extra delay na fout
  }
}

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
  float temp = readTempC_critical();
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
  // Gebruik char array i.p.v. String om heap fragmentatie te voorkomen
  char cyclus_text[32];
  if (cyclus_max == 0) {
    snprintf(cyclus_text, sizeof(cyclus_text), "Cycli: %d/inf", cyclus_teller);
  } else {
    snprintf(cyclus_text, sizeof(cyclus_text), "Cycli: %d/%d", cyclus_teller, cyclus_max);
  }
  lv_label_set_text(text_label_cyclus, cyclus_text);
  
  // Update status - gebruik char array i.p.v. String
  const char* status_suffix = "";
  if (koelingsfase_actief) {
    status_suffix = "Veiligheidskoeling";
  } else if (systeem_uit) {
    status_suffix = "Uit";
  } else if (cyclus_actief) {
    status_suffix = verwarmen_actief ? "Verwarmen" : "Koelen";
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
  // BELANGRIJK: Controleer eerst verwarmen_actief flag, dan pas timer
  // Dit voorkomt dat de verkeerde timer wordt getoond tijdens fase overgangen
  if (cyclus_actief && verwarmen_actief && verwarmen_start_tijd > 0) {
    // We zijn aan het verwarmen EN de timer loopt
    unsigned long verwarmen_verstreken = millis() - verwarmen_start_tijd;
    char tijd_str[16];
    formatTijdChar(verwarmen_verstreken, tijd_str, sizeof(tijd_str));
    char verwarmen_text[32];
    snprintf(verwarmen_text, sizeof(verwarmen_text), "Opwarmen: %s", tijd_str);
    lv_label_set_text(text_label_verwarmen_tijd, verwarmen_text);
    // Zet koelen timer op 0:00 tijdens verwarmen
    lv_label_set_text(text_label_koelen_tijd, "Afkoelen: 0:00");
  } else if (cyclus_actief && !verwarmen_actief && koelen_start_tijd > 0) {
    // We zijn aan het koelen EN de timer loopt
    unsigned long koelen_verstreken = millis() - koelen_start_tijd;
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
  if (koelingsfase_actief) {
    // Veiligheidskoeling: beide knoppen grijs (geen bediening mogelijk)
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x808080), LV_PART_MAIN); // Grey
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x808080), LV_PART_MAIN);  // Grey
  } else if (cyclus_actief && !systeem_uit) {
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
    // Reset systeem status - altijd mogelijk om te starten
    systeem_uit = false;
    koelingsfase_actief = false; // Reset veiligheidskoeling fase bij START
    veiligheidskoeling_start_tijd = 0;
    veiligheidskoeling_naloop_start_tijd = 0;
    
    if (!cyclus_actief) {
      cyclus_actief = true;
      cyclus_teller = 1;  // Reset cyclus teller (start bij 1)
      systeemAan();
      
      // Log START eerst
      logToGoogleSheet("START");
      
      // BELANGRIJK: Reset grafiek data ALLEEN bij START (niet bij cyclus overgangen)
      // Grafiek moet doorlopen over alle cycli heen tot systeem UIT
      if (graph_temps != nullptr && graph_times != nullptr) {
        for (int i = 0; i < GRAPH_POINTS; i++) {
          graph_temps[i] = NAN;  // Reset data
          graph_times[i] = 0;
        }
        graph_write_index = 0;
        graph_count = 0;
        graph_data_ready = false;
        graph_last_log_time = 0; // Reset timer zodat eerste meting meteen kan starten
        graph_force_rebuild = false;
      }
      
      // Reset timers en fase duur opslag
      verwarmen_start_tijd = 0;
      koelen_start_tijd = 0;
      last_opwarmen_duur = 0;
      last_koelen_duur = 0;
      
      // Reset gemiddelde opwarmtijd tracking voor beveiliging
      gemiddelde_opwarmen_duur = 0;
      opwarmen_telling = 0;
      
      // Reset temperatuur stagnatie tracking voor beveiliging
      laatste_temp_voor_stagnatie = NAN;
      stagnatie_start_tijd = 0;
      
      // Start logica:
      // - Als meettemp < T_top: opwarmen (verwarming aan)
      // - Als meettemp >= T_top: koelen (pomp aan)
      if (isnan(g_currentTempC)) {
        // Geen geldige temperatuur - start met verwarmen als default
        verwarmen_actief = true;
        verwarmenAan();  // Start met verwarmen
        verwarmen_start_tijd = millis(); // Start opwarmen timer
        // Log niet bij START - wacht tot eerste faseovergang
      } else if (g_currentTempC < T_top) {
        // Meettemp < T_top: opwarmen (verwarming aan)
        verwarmen_actief = true;
        verwarmenAan();  // Start met verwarmen
        verwarmen_start_tijd = millis(); // Start opwarmen timer
        // Log niet bij START - wacht tot eerste faseovergang (opwarmen→afkoelen)
      } else {
        // Meettemp >= T_top: koelen (pomp aan)
        verwarmen_actief = false;
        koelenAan();     // Start met koelen
        koelen_start_tijd = millis(); // Start afkoelen timer
        // Log niet bij START - wacht tot eerste faseovergang (afkoelen→opwarmen)
      }
    }
  }
}

void stop_button_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    // Log STOP knop bediening eerst
    logToGoogleSheet("STOP");
    
    // STOP logica:
    // - Als meettemp <= 35°C: meteen UIT
    // - Als meettemp > 35°C: veiligheidskoelen (pomp aan) tot < 35°C
    if (isnan(g_currentTempC)) {
      // Geen geldige temperatuur - direct uit als veilig
      alleRelaisUit();
      cyclus_actief = false;
      systeem_uit = true;
      verwarmen_start_tijd = 0;
      koelen_start_tijd = 0;
      veiligheidskoeling_start_tijd = 0;
      veiligheidskoeling_naloop_start_tijd = 0;
      logToGoogleSheet("Uit"); // Log STOP: direct uit
    } else if (g_currentTempC <= TEMP_SAFETY_COOLING) {
      // Meettemp <= TEMP_SAFETY_COOLING: meteen UIT
      alleRelaisUit();
      cyclus_actief = false;
      systeem_uit = true;
      // Reset timers
      verwarmen_start_tijd = 0;
      koelen_start_tijd = 0;
      veiligheidskoeling_start_tijd = 0;
      veiligheidskoeling_naloop_start_tijd = 0;
      logToGoogleSheet("Uit"); // Log STOP: direct uit
    } else {
      // Meettemp > 35°C: veiligheidskoelen (pomp aan) tot < 35°C
      koelingsfase_actief = true;
      cyclus_actief = false; // Stop normale cyclus
      systeem_uit = true;    // Markeer als uitgeschakeld
      koelenAan();           // Start veiligheidskoeling (pomp aan)
      veiligheidskoeling_start_tijd = millis(); // Start timer voor veiligheidskoeling
      veiligheidskoeling_naloop_start_tijd = 0; // Reset naloop timer (wordt gestart wanneer temp < 35°C)
      logToGoogleSheet("Veiligheidskoeling"); // Log STOP met veiligheidskoeling
    }
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
    // EENVOUDIGE CIRCULAIRE ARRAY: Bij openen grafiek toon altijd laatste 120 punten
    // Force rebuild bij volgende update_graph_display() call
    graph_force_rebuild = true;
    
    // Update Y-as labels op basis van huidige T_top
    update_graph_y_axis_labels();
    
    // Schakel naar grafiek scherm
    lv_scr_load(screen_graph);
    
    // BELANGRIJK: Update grafiek direct na scherm wissel
    // Dit zorgt ervoor dat bestaande punten direct zichtbaar zijn
    update_graph_display();
  }
}

void back_button_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    // Schakel terug naar main scherm
    lv_scr_load(screen_main);
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
    T_top += 1.0;
    // BELANGRIJK: Gebruik >= i.p.v. > om floating point problemen te voorkomen
    if (T_top >= TEMP_MAX) {
      T_top = TEMP_MAX;
    }
    char log_msg[32];
    snprintf(log_msg, sizeof(log_msg), "Top+%.1f", T_top);
    saveAndLogSetting(log_msg, true);
  }
}

void t_top_minus_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    T_top -= 1.0;
    if (T_top < T_bottom + TEMP_MIN_DIFF) T_top = T_bottom + TEMP_MIN_DIFF;
    char log_msg[32];
    snprintf(log_msg, sizeof(log_msg), "Top-%.1f", T_top);
    saveAndLogSetting(log_msg, true);
  }
}

void t_bottom_plus_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    T_bottom += 1.0;
    if (T_bottom > T_top - TEMP_MIN_DIFF) T_bottom = T_top - TEMP_MIN_DIFF;
    char log_msg[32];
    snprintf(log_msg, sizeof(log_msg), "Dal+%.1f", T_bottom);
    saveAndLogSetting(log_msg);
  }
}

void t_bottom_minus_event(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_PRESSED) {
    T_bottom -= 1.0;
    if (T_bottom < 0.0) T_bottom = 0.0; // Min temperatuur
    char log_msg[32];
    snprintf(log_msg, sizeof(log_msg), "Dal-%.1f", T_bottom);
    saveAndLogSetting(log_msg);
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
  thermocouple.begin();
  
  // BELANGRIJK: Stel software SPI delay in voor betere communicatie stabiliteit
  // Dit voorkomt timing problemen bij hoge CPU belasting
  thermocouple.setSWSPIdelay(MAX6675_SW_SPI_DELAY_US);
  
  // BELANGRIJK: Gebruik char array i.p.v. String om heap fragmentatie te voorkomen
  char lvgl_version[64];
  snprintf(lvgl_version, sizeof(lvgl_version), "LVGL Library Version: %d.%d.%d", 
           lv_version_major(), lv_version_minor(), lv_version_patch());
  Serial.begin(115200);
  
  // Laad opgeslagen instellingen uit non-volatile memory
  loadSettings();

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
    (void)readTempC();
    delay(MAX6675_CONVERSION_TIME_MS); // Wacht conversietijd tussen reads
  }
  
  // Reset conversietijd timer na warm-up
  g_lastMax6675ReadTime = 0;
  
  // Alloceer buffers in DRAM
  if (!allocate_buffers()) {
    Serial.println("KRITIEKE FOUT: Kan buffers niet alloceren!");
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
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, actual_draw_buf_size);
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
  
  // Initialize an LVGL input device object (Touchscreen)
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  // Set the callback function to read Touchscreen input
  lv_indev_set_read_cb(indev, touchscreen_read);
  // Opmerking: lv_indev_set_read_period() is niet beschikbaar in deze LVGL versie
  // Touch response wordt verbeterd door vaker lv_task_handler() aan te roepen in loop()

  // Initialiseer grafiek data
  init_graph_data();

  // Maak hoofdscherm eerst (zodat status labels beschikbaar zijn)
  lv_create_main_gui();
  
  // Zet alle knoppen grijs tijdens initialisatie
  setAllButtonsGray();
  
  // Toon initialisatie status
  showInitStatus("WiFi initialiseren", 0x000000); // Zwart
  
  // ---- WiFi en Google Sheets initialisatie ----
  // WiFiManager voor WiFi configuratie
  WiFiManager wm;
  wm.setConfigPortalTimeout(180); // Timeout na 3 minuten
  
  // Callback voor wanneer config portal (AP) wordt gestart
  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    IPAddress apIP = WiFi.softAPIP();
    
    // Update init status tekst met AP IP
    // BELANGRIJK: Gebruik char array i.p.v. String om heap fragmentatie te voorkomen
    char apText[64];
    snprintf(apText, sizeof(apText), "Stel WiFi in via AP op %s", apIP.toString().c_str());
    showInitStatus(apText, 0x000000); // Zwart
    
    // Force LVGL update zodat tekst direct zichtbaar is
    lv_task_handler();
    
    // Update WiFi status
    char apStatus[64];
    snprintf(apStatus, sizeof(apStatus), "WiFi: AP actief [%s]", apIP.toString().c_str());
    showWifiStatus(apStatus, false); // Grijs
    
    // Nog een keer LVGL update voor WiFi status
    lv_task_handler();
  });
  
  // Probeer verbinding te maken met opgeslagen credentials
  showWifiStatus("WiFi: verbinden...", false); // Grijs
  lv_task_handler(); // Update display
  
  bool res = wm.autoConnect("ESP32-TemperatuurCyclus");
  
  // Controleer of AP actief is (als autoConnect mislukt, kan AP nog actief zijn)
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    IPAddress apIP = WiFi.softAPIP();
    if (apIP != IPAddress(0, 0, 0, 0)) {
      // BELANGRIJK: Gebruik char array i.p.v. String om heap fragmentatie te voorkomen
      char apText[64];
      snprintf(apText, sizeof(apText), "Stel WiFi in via AP op %s", apIP.toString().c_str());
      showInitStatus(apText, 0x000000); // Zwart
      lv_task_handler(); // Force display update
    }
  }
  
  if (!res) {
    showWifiStatus("WiFi: MISLUKT - herstart", true); // Rood (fout)
    // Blijf doorgaan zonder WiFi - logging werkt dan niet
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    // BELANGRIJK: Gebruik char array i.p.v. String om heap fragmentatie te voorkomen
    char wifiInfo[128];
    snprintf(wifiInfo, sizeof(wifiInfo), "WiFi: %s (%s)", 
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    showWifiStatus(wifiInfo, false); // Grijs (succes)
    
    // Configureer NTP tijd met lokale tijdzone offset (+1 uur = 3600 seconden)
    showGSStatus("Google Sheets: tijd synchroniseren...", false); // Grijs
    configTime(3600, 0, "pool.ntp.org"); // GMT offset: +1 uur (3600 seconden), geen daylight saving offset
    struct tm timeinfo;
    int ntp_tries = 0;
    while (!getLocalTime(&timeinfo) && ntp_tries < 10) {
      delay(1000);
      yield(); // BELANGRIJK: Feed watchdog in while loop
      lv_task_handler(); // Update display tijdens wachten
      ntp_tries++;
    }
    if (getLocalTime(&timeinfo)) {
      // Sla NTP sync tijd op voor timestamp berekening
      ntp_sync_time_ms = millis();
      ntp_sync_unix_time = mktime(&timeinfo);
    }
    
    // Initialiseer Google Sheets client
    showGSStatus("Google Sheets: authenticeren...", false); // Grijs
    sheet.setTokenCallback(gs_tokenStatusCallback);
    sheet.setPrerefreshSeconds(10 * 60); // Refresh token elke 10 minuten
    sheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);
    
    // Wacht op token authenticatie (max 30 seconden)
    int auth_tries = 0;
    while (!g_googleAuthTokenReady && auth_tries < 60) {
      sheet.ready(); // Verwerk token requests
      delay(500);
      yield(); // BELANGRIJK: Feed watchdog in while loop
      lv_task_handler(); // Update display tijdens wachten
      auth_tries++;
      if (auth_tries % 10 == 0) {
        showGSStatus("Google Sheets: wachten...", false); // Grijs
      }
    }
    
    if (g_googleAuthTokenReady) {
      showGSStatus("Google Sheets: GEREED", false); // Grijs (succes)
      delay(1000); // Korte pauze
    } else {
      showGSStatus("Google Sheets: MISLUKT", true); // Rood (fout)
      delay(1000);
    }
  } else {
    showGSStatus("Google Sheets: WiFi nodig", false); // Grijs
    delay(1000);
  }
  
  // Verberg initialisatie status (status regels blijven zichtbaar)
  hideInitStatus();
  
  // Zet alle knoppen terug naar normale kleuren (initialisatie compleet)
  setAllButtonsNormal();
  
  // Initialiseer FreeRTOS queue voor logging
  logQueue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogRequest));
  if (logQueue == nullptr) {
    Serial.println("❌ KRITIEK: Kan logging queue niet aanmaken!");
    while(1) delay(1000); // Blijf hangen
  }
  
  // Start logging task op Core 1 (samen met loop() - lagere prioriteit voor betere knopbediening)
  // Verhoogde stack grootte voor FirebaseJson operaties (was 8192, nu 16384)
  // BELANGRIJK: Logging op Core 1 zodat Core 0 meer beschikbaar is voor real-time taken
  xTaskCreatePinnedToCore(
    loggingTask,           // Task functie
    "LoggingTask",         // Task naam
    16384,                 // Stack grootte (bytes) - verhoogd voor FirebaseJson
    NULL,                  // Parameters
    1,                     // Prioriteit verlaagd (was 2) zodat knopbediening prioriteit heeft
    &loggingTaskHandle,    // Task handle
    1                      // Core 1 (samen met loop() - Core 0 blijft beschikbaar voor real-time taken)
  );
}

void loop() {
  // BELANGRIJK: Feed watchdog timer om crashes te voorkomen
  // Dit is de eerste regel om watchdog altijd te voeden
  yield();
  
  // BELANGRIJK: LVGL task handler - roep dit VAKER aan voor betere responsiviteit
  // Meerdere calls voor snellere touch response (vooral belangrijk voor terug-knop)
  lv_task_handler();
  yield(); // Feed watchdog na LVGL operaties
  lv_task_handler(); // Extra call voor snellere response
  yield();
  
  // Google Sheets token refresh wordt nu gedaan in logging task op Core 1
  
  // Behandel logging success feedback (vanuit Core 1)
  if (g_logSuccessFlag) {
    showGSSuccessCheckmark();  // Toon groen vinkje
    g_logSuccessFlag = false;  // Reset flag
  }
  
  // Verwijder vinkje na 1 seconde
  if (g_logSuccessTime > 0 && (millis() - g_logSuccessTime >= 1000)) {
    if (gs_status_label != nullptr && strlen(g_lastGSStatusText) > 0) {
      // Reset naar originele tekst (zonder vinkje)
      lv_label_set_text(gs_status_label, g_lastGSStatusText);
      lv_obj_set_style_text_color(gs_status_label, lv_color_hex(0x888888), LV_PART_MAIN); // Terug naar grijs
    }
    g_logSuccessTime = 0;  // Reset timer
  }
  
  // Temperatuur meting (elke 0.3 seconde)
  sampleMax6675();
  yield(); // Feed watchdog na temperatuur meting
  lv_task_handler(); // Laat LVGL touch events verwerken
  
  // Update cyclus logica
  // BELANGRIJK: Dit kan lang duren bij statusovergangen - feed watchdog
  cyclusLogica();
  yield(); // Feed watchdog na cyclus logica
  lv_task_handler(); // Laat LVGL touch events verwerken (belangrijk voor responsiviteit)
  
  // Log grafiek data
  log_graph_data();
  yield(); // Feed watchdog na grafiek logging
  lv_task_handler(); // Laat LVGL touch events verwerken
  
  // Update GUI labels (alleen op main scherm)
  static unsigned long last_gui_update = 0;
  if (lv_scr_act() == screen_main) {
    if (millis() - last_gui_update >= GUI_UPDATE_INTERVAL_MS) {
      updateGUI();
      last_gui_update = millis();
    }
  } else if (lv_scr_act() == screen_graph) {
    // Update grafiek wanneer op grafiek scherm (elke 0.5 seconde als backup)
    // REAL-TIME: Directe updates gebeuren al in log_graph_data() bij nieuwe data
    unsigned long now = millis();
    if (now - g_lastGraphUpdateMs >= GRAPH_UPDATE_INTERVAL_MS) {
      g_lastGraphUpdateMs = now;
      update_graph_display();
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