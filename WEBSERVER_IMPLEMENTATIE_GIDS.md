# WebServer Module - Implementatie Gids

## Overzicht

De `ConfigWebServer` module is een herbruikbare webserver implementatie voor ESP32 projecten die een webinterface biedt voor:
- Status monitoring (real-time updates via JSON API)
- Instellingen beheer (via HTML formulier)
- Systeem controle (START/STOP acties)
- Google Sheets credentials beheer (optioneel)

## Architectuur

De webserver gebruikt een **callback-gebaseerde architectuur** om losgekoppeld te blijven van specifieke projectcode. Dit maakt de module volledig herbruikbaar.

### Belangrijke Principes:
1. **Geen directe dependencies**: De webserver gebruikt forward declarations en callbacks
2. **Flexibele data toegang**: Via getter callbacks kan elke data structuur worden gebruikt
3. **Actie callbacks**: START/STOP en settings wijzigingen worden via callbacks afgehandeld
4. **Optionele features**: Google Sheets credentials zijn optioneel

## Vereisten

### Hardware
- ESP32 microcontroller
- WiFi functionaliteit (Station mode of AP+STA mode)

### Software Dependencies
- `WebServer.h` (ESP32 Arduino Core library)
- `Arduino.h` (standaard Arduino library)

### Optionele Dependencies
- `SettingsStore` module (voor instellingen opslag)
- Andere project-specifieke modules (via callbacks)

## Installatie

### Stap 1: Kopieer de Module

Kopieer de volgende bestanden naar je project:
```
src/WebServer/
├── WebServer.h
└── WebServer.cpp
```

### Stap 2: Pas Includes Aan

De webserver module gebruikt forward declarations voor externe classes. Als je andere modules gebruikt, pas dan de includes aan in `WebServer.cpp`:

```cpp
// In WebServer.cpp - verwijder of pas aan naar jouw modules
#include "../SettingsStore/SettingsStore.h"  // Optioneel
#include "../CycleController/CycleController.h"  // Optioneel
#include "../TempSensor/TempSensor.h"  // Optioneel
#include "../UIController/UIController.h"  // Optioneel
```

**Let op**: Als je deze modules niet gebruikt, verwijder dan de includes en de bijbehorende setter methoden.

## Basis Implementatie

### Stap 1: Include en Declare

```cpp
#include "src/WebServer/WebServer.h"

ConfigWebServer webServer(80);  // Port 80 (standaard HTTP)
```

### Stap 2: Initialiseer in setup()

```cpp
void setup() {
    // ... andere initialisatie code ...
    
    // WiFi setup (vereist voor webserver)
    WiFi.mode(WIFI_AP_STA);  // Of WIFI_STA of WIFI_AP
    WiFi.softAP("ESP32-Config", "password123");  // AP mode
    // Of: WiFi.begin("SSID", "password");  // Station mode
    
    // Wacht tot WiFi verbonden is
    while (WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() == 0) {
        delay(100);
    }
    
    // Setup callbacks (zie volgende sectie)
    setupWebServerCallbacks();
    
    // Start webserver
    webServer.begin();
    
    Serial.print("Webserver gestart op: http://");
    Serial.println(WiFi.softAPIP());  // Of WiFi.localIP() voor Station mode
}
```

### Stap 3: Roep handleClient() aan in loop()

```cpp
void loop() {
    // ... andere code ...
    
    webServer.handleClient();  // Verwerk HTTP requests
    
    // ... andere code ...
}
```

## Callbacks Configureren

### Verplichte Callbacks (voor basis functionaliteit)

#### Status Callbacks (voor `/status` API)

```cpp
void setupWebServerCallbacks() {
    // Status getters
    webServer.setGetCurrentTempCallback([]() { 
        return huidigeTemperatuur;  // Jouw temperatuur variabele
    });
    
    webServer.setIsActiveCallback([]() { 
        return systeemActief;  // Jouw status variabele
    });
    
    // Optionele status callbacks
    webServer.setIsHeatingCallback([]() { 
        return verwarmenActief; 
    });
    
    webServer.setGetCycleCountCallback([]() { 
        return cyclusTeller; 
    });
}
```

#### Settings Callbacks (voor instellingen formulier)

```cpp
void setupWebServerCallbacks() {
    // Settings getters
    webServer.setGetTtopCallback([]() { 
        return T_top;  // Jouw instelling
    });
    
    webServer.setGetTbottomCallback([]() { 
        return T_bottom; 
    });
    
    webServer.setGetCycleMaxCallback([]() { 
        return cyclus_max; 
    });
    
    webServer.setGetTempOffsetCallback([]() { 
        return temp_offset; 
    });
}
```

#### Actie Callbacks (voor START/STOP en settings wijzigingen)

```cpp
void setupWebServerCallbacks() {
    // START actie
    webServer.setStartCallback([]() {
        // Jouw START logica
        startSysteem();
    });
    
    // STOP actie
    webServer.setStopCallback([]() {
        // Jouw STOP logica
        stopSysteem();
    });
    
    // Settings wijziging
    webServer.setSettingsChangeCallback([](float tTop, float tBottom, 
                                           float tempOffset, int cycleMax,
                                           const char* clientEmail, 
                                           const char* projectId,
                                           const char* privateKey, 
                                           const char* spreadsheetId) {
        // Sla nieuwe instellingen op
        T_top = tTop;
        T_bottom = tBottom;
        temp_offset = tempOffset;
        cyclus_max = cycleMax;
        
        // Optioneel: Google Sheets credentials
        if (strlen(clientEmail) > 0) {
            // Sla credentials op (bijv. in Preferences)
        }
    });
}
```

### Optionele Callbacks

#### Google Sheets Credentials (alleen nodig als je Google Sheets logging gebruikt)

```cpp
void setupWebServerCallbacks() {
    webServer.setGetClientEmailCallback([]() -> const char* {
        static char email[128];
        // Laad email uit jouw storage
        strcpy(email, opgeslagenEmail);
        return email;
    });
    
    webServer.setGetProjectIdCallback([]() -> const char* {
        static char projectId[64];
        strcpy(projectId, opgeslagenProjectId);
        return projectId;
    });
    
    webServer.setGetPrivateKeyCallback([]() -> const char* {
        static char key[2048];
        strcpy(key, opgeslagenPrivateKey);
        return key;
    });
    
    webServer.setGetSpreadsheetIdCallback([]() -> const char* {
        static char sheetId[128];
        strcpy(sheetId, opgeslagenSpreadsheetId);
        return sheetId;
    });
}
```

## API Endpoints

De webserver biedt de volgende endpoints:

### GET `/`
- **Beschrijving**: Hoofdpagina met webinterface
- **Response**: HTML pagina met formulier en status display
- **Gebruik**: Open in browser voor configuratie

### GET `/settings`
- **Beschrijving**: Huidige instellingen als JSON
- **Response**: `{"tTop":80.0,"tBottom":25.0,"tempOffset":0.0,"cycleMax":0,...}`
- **Gebruik**: Voor AJAX calls om huidige instellingen op te halen

### GET `/status`
- **Beschrijving**: Real-time systeemstatus als JSON
- **Response**: `{"currentTemp":45.2,"medianTemp":45.0,"isActive":true,"isHeating":true,...}`
- **Gebruik**: Voor real-time status updates (wordt automatisch elke 2 seconden opgehaald)

### POST `/start`
- **Beschrijving**: Start het systeem
- **Body**: Leeg
- **Response**: `{"status":"success"}` of `{"status":"error","message":"..."}`
- **Gebruik**: Wordt aangeroepen door START knop

### POST `/stop`
- **Beschrijving**: Stop het systeem
- **Body**: Leeg
- **Response**: `{"status":"success"}` of `{"status":"error","message":"..."}`
- **Gebruik**: Wordt aangeroepen door STOP knop

### POST `/save`
- **Beschrijving**: Sla nieuwe instellingen op
- **Body**: `tTop=80.0&tBottom=25.0&tempOffset=0.0&cycleMax=0&...`
- **Response**: `{"status":"success"}` of `{"status":"error","message":"..."}`
- **Gebruik**: Wordt aangeroepen bij formulier submit

## Aanpassen voor Jouw Project

### HTML Interface Aanpassen

De HTML interface wordt gegenereerd in `generateHTML()` in `WebServer.cpp`. Pas deze functie aan voor jouw specifieke instellingen:

```cpp
String ConfigWebServer::generateHTML() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>Jouw Project Configuratie</title>";
    // ... pas aan naar jouw behoeften ...
    return html;
}
```

### Nieuwe Instellingen Toevoegen

1. **Voeg getter callback toe in `WebServer.h`**:
```cpp
typedef float (*GetNieuweInstellingCallback)();
void setGetNieuweInstellingCallback(GetNieuweInstellingCallback cb);
```

2. **Implementeer in `WebServer.cpp`**:
```cpp
GetNieuweInstellingCallback getNieuweInstellingCallback;

void ConfigWebServer::setGetNieuweInstellingCallback(GetNieuweInstellingCallback cb) {
    getNieuweInstellingCallback = cb;
}
```

3. **Voeg toe aan HTML formulier** in `generateHTML()`:
```html
<div class="form-group">
    <label for="nieuweInstelling">Nieuwe Instelling:</label>
    <input type="number" id="nieuweInstelling" name="nieuweInstelling" step="0.1" required>
</div>
```

4. **Verwerk in `handleSaveSettings()`**:
```cpp
if (server.hasArg("nieuweInstelling")) {
    float nieuweWaarde = server.arg("nieuweInstelling").toFloat();
    // Roep settings callback aan met nieuwe waarde
}
```

5. **Voeg toe aan `SettingsChangeCallback` typedef**:
```cpp
typedef void (*SettingsChangeCallback)(float tTop, float tBottom, 
                                       float nieuweInstelling,  // NIEUW
                                       ...);
```

### Nieuwe Status Velden Toevoegen

1. **Voeg getter callback toe** (zoals hierboven)
2. **Voeg toe aan `generateStatusJSON()`**:
```cpp
if (getNieuweStatusCallback) {
    response += ",\"nieuweStatus\":" + String(getNieuweStatusCallback());
}
```

3. **Update JavaScript in HTML** om nieuwe status te tonen

## Minimale Implementatie Voorbeeld

Hier is een minimale implementatie zonder Google Sheets:

```cpp
#include "src/WebServer/WebServer.h"

ConfigWebServer webServer(80);

float temperatuur = 0.0;
bool systeemActief = false;
float T_top = 80.0;
float T_bottom = 25.0;

void setup() {
    Serial.begin(115200);
    
    // WiFi setup
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-Config", "password123");
    
    // Setup callbacks
    webServer.setGetCurrentTempCallback([]() { return temperatuur; });
    webServer.setIsActiveCallback([]() { return systeemActief; });
    webServer.setGetTtopCallback([]() { return T_top; });
    webServer.setGetTbottomCallback([]() { return T_bottom; });
    
    webServer.setStartCallback([]() {
        systeemActief = true;
        Serial.println("Systeem gestart");
    });
    
    webServer.setStopCallback([]() {
        systeemActief = false;
        Serial.println("Systeem gestopt");
    });
    
    webServer.setSettingsChangeCallback([](float tTop, float tBottom, 
                                           float tempOffset, int cycleMax,
                                           const char* clientEmail, 
                                           const char* projectId,
                                           const char* privateKey, 
                                           const char* spreadsheetId) {
        T_top = tTop;
        T_bottom = tBottom;
        Serial.printf("Instellingen opgeslagen: T_top=%.1f, T_bottom=%.1f\n", T_top, T_bottom);
    });
    
    webServer.begin();
    Serial.print("Webserver: http://");
    Serial.println(WiFi.softAPIP());
}

void loop() {
    // Simuleer temperatuur update
    temperatuur = 20.0 + random(0, 100) / 10.0;
    
    webServer.handleClient();
    delay(100);
}
```

## Troubleshooting

### Webserver start niet
- **Check WiFi**: Zorg dat WiFi is geïnitialiseerd voordat `webServer.begin()` wordt aangeroepen
- **Check Serial output**: Bekijk Serial monitor voor foutmeldingen
- **Check IP adres**: Gebruik `WiFi.softAPIP()` of `WiFi.localIP()` om IP te controleren

### Callbacks werken niet
- **Check null pointers**: Zorg dat alle callbacks zijn ingesteld voordat webserver start
- **Check return types**: Zorg dat callback return types overeenkomen met typedef
- **Check lambda syntax**: Gebruik `[]() { return waarde; }` voor getters

### Settings worden niet opgeslagen
- **Check callback**: Zorg dat `setSettingsChangeCallback()` is ingesteld
- **Check formulier**: Controleer of formulier velden correcte `name` attributen hebben
- **Check validatie**: Bekijk `handleSaveSettings()` voor validatie logica

### Status updates werken niet
- **Check JavaScript**: Open browser console (F12) voor JavaScript errors
- **Check JSON format**: Controleer of `generateStatusJSON()` valide JSON produceert
- **Check polling**: Status wordt elke 2 seconden opgehaald (zie JavaScript in HTML)

## Aanbevelingen

1. **Security**: Voeg authenticatie toe voor productie gebruik (bijv. HTTP Basic Auth)
2. **HTTPS**: Overweeg HTTPS voor gevoelige data (vereist certificaat)
3. **Caching**: Voeg cache headers toe voor statische resources
4. **Error Handling**: Verbeter error handling in callbacks
5. **Logging**: Voeg logging toe voor debugging

## Licentie

Deze module is ontwikkeld voor specifiek gebruik maar kan vrijelijk worden aangepast voor andere projecten.

