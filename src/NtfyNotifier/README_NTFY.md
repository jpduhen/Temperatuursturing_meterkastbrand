# NTFY Notificatie Module - Gebruiksaanwijzing

## Overzicht

De NTFY Notificatie module (`NtfyNotifier`) is een herbruikbare module voor het versturen van push notificaties via NTFY.sh. Deze module kan eenvoudig geïntegreerd worden in elk ESP32 project.

## Wat is NTFY.sh?

NTFY.sh is een gratis, open-source push notification service. Het stelt je in staat om notificaties te ontvangen op je telefoon, tablet of computer zonder dat je een eigen server hoeft te draaien.

## Installatie

### 1. NTFY App Installeren

1. **Android/iOS**: Installeer de officiële NTFY app uit de Play Store of App Store
2. **Desktop**: Download de desktop app van [ntfy.sh/apps](https://ntfy.sh/apps)

### 2. Module Toevoegen aan Project

Kopieer de `NtfyNotifier` map naar de `src/` directory van je project:

```
src/
  └── NtfyNotifier/
      ├── NtfyNotifier.h
      ├── NtfyNotifier.cpp
      └── README_NTFY.md
```

## Gebruik

### Basis Gebruik

```cpp
#include "src/NtfyNotifier/NtfyNotifier.h"

// Maak een instantie
NtfyNotifier notifier;

void setup() {
    // Initialiseer WiFi eerst
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    
    // Initialiseer NTFY met een topic
    if (notifier.begin("mijn-project-notificaties")) {
        Serial.println("NTFY geïnitialiseerd");
    }
}

void loop() {
    // Verstuur een notificatie
    notifier.send("Titel", "Dit is een test notificatie");
    delay(60000); // Wacht 1 minuut
}
```

### Geavanceerd Gebruik

#### Melding Types

De module ondersteunt verschillende melding types:

```cpp
// Algemene informatie
notifier.sendInfo("Info", "Dit is een informatieve melding");

// Systeem gestart
notifier.sendStart("Systeem", "Temperatuur controller gestart");

// Systeem gestopt
notifier.sendStop("Systeem", "Temperatuur controller gestopt");

// Fase overgang
notifier.sendTransition("Cyclus", "Verwarmen → Koelen");

// Veiligheidsmelding
notifier.sendSafety("Waarschuwing", "Temperatuur te hoog!");

// Foutmelding
notifier.sendError("Fout", "Sensor communicatie gefaald");

// Waarschuwing
notifier.sendWarning("Waarschuwing", "Batterij laag");
```

#### Melding Instellingen

Je kunt specifieke melding types aan/uit zetten:

```cpp
NtfyNotificationSettings settings;
settings.enabled = true;           // Algemene NTFY functionaliteit
settings.logInfo = true;           // LOG_INFO meldingen
settings.logStart = true;          // LOG_START meldingen
settings.logStop = true;           // LOG_STOP meldingen
settings.logTransition = true;     // LOG_TRANSITION meldingen
settings.logSafety = true;         // LOG_SAFETY meldingen
settings.logError = true;           // LOG_ERROR meldingen
settings.logWarning = true;        // LOG_WARNING meldingen

notifier.setSettings(settings);
```

#### Handmatig Type Opgeven

```cpp
// Verstuur met specifiek type
notifier.send(
    "Titel", 
    "Bericht", 
    NtfyNotificationType::LOG_ERROR,
    "rotating_light"  // Optionele custom color tag
);
```

### Integratie met Logger

De module kan geïntegreerd worden met een Logger module zodat alle logs automatisch ook als NTFY notificaties worden verstuurd:

```cpp
#include "src/Logger/Logger.h"
#include "src/NtfyNotifier/NtfyNotifier.h"

Logger logger;
NtfyNotifier notifier;

void setup() {
    // Initialiseer logger
    logger.begin(clientEmail, projectId, privateKey, spreadsheetId, &systemClock);
    
    // Koppel NTFY notifier aan logger
    logger.setNtfyNotifier(&notifier);
    
    // Initialiseer NTFY
    notifier.begin("mijn-temperatuur-monitor");
}
```

## Web Interface Integratie

De module kan geïntegreerd worden met een web interface om instellingen te configureren. Zie `WebServer.cpp` voor een voorbeeld implementatie.

### Callbacks voor Web Interface

```cpp
// Getter voor NTFY topic
const char* getNtfyTopic() {
    return notifier.getTopic();
}

// Getter voor NTFY instellingen
NtfyNotificationSettings getNtfySettings() {
    return notifier.getSettings();
}

// Setter voor NTFY instellingen
void saveNtfySettings(const char* topic, const NtfyNotificationSettings& settings) {
    notifier.setTopic(topic);
    notifier.setSettings(settings);
    
    // Opslaan in persistent storage (bijv. Preferences)
    settingsStore.saveNtfySettings(topic, settings);
}
```

## NTFY Topic Setup

### 1. Kies een Topic Naam

Een NTFY topic is een unieke naam voor je notificaties. Kies een unieke naam die alleen jij kent:

- Goed: `mijn-temperatuur-monitor-2024`
- Goed: `esp32-kamer-temp-abc123`
- Slecht: `test` (te algemeen, anderen kunnen dit ook gebruiken)

### 2. Abonneer op het Topic

1. Open de NTFY app op je telefoon/tablet
2. Klik op "Subscribe to topic"
3. Voer je topic naam in (bijv. `mijn-temperatuur-monitor-2024`)
4. Klik op "Subscribe"

Nu ontvang je alle notificaties die naar dit topic worden verstuurd!

### 3. Topic Beveiligen (Optioneel)

Voor extra beveiliging kun je een wachtwoord instellen:

1. Ga naar [ntfy.sh](https://ntfy.sh) en maak een account
2. Maak een topic aan met een wachtwoord
3. In de NTFY app: Voeg het wachtwoord toe bij het abonneren

**Let op**: De standaard NTFY.sh service is publiek - iedereen met je topic naam kan je notificaties zien. Gebruik een unieke topic naam of beveilig het met een wachtwoord.

## API Referentie

### Klassen

#### `NtfyNotifier`

Hoofdklasse voor NTFY notificaties.

##### Methoden

- `bool begin(const char* topic)` - Initialiseer met topic naam
- `void setTopic(const char* topic)` - Wijzig topic naam
- `const char* getTopic() const` - Haal huidige topic op
- `void setSettings(const NtfyNotificationSettings& settings)` - Stel melding instellingen in
- `NtfyNotificationSettings getSettings() const` - Haal melding instellingen op
- `bool isNotificationEnabled(NtfyNotificationType type) const` - Check of melding type is ingeschakeld
- `bool send(const char* title, const char* message, NtfyNotificationType type, const char* colorTag)` - Verstuur notificatie
- `bool sendInfo(...)` - Verstuur info notificatie
- `bool sendStart(...)` - Verstuur start notificatie
- `bool sendStop(...)` - Verstuur stop notificatie
- `bool sendTransition(...)` - Verstuur transition notificatie
- `bool sendSafety(...)` - Verstuur safety notificatie
- `bool sendError(...)` - Verstuur error notificatie
- `bool sendWarning(...)` - Verstuur warning notificatie
- `bool isEnabled() const` - Check of NTFY is ingeschakeld

#### `NtfyNotificationSettings`

Struct voor melding instellingen.

##### Velden

- `bool enabled` - Algemene NTFY functionaliteit aan/uit
- `bool logInfo` - LOG_INFO meldingen
- `bool logStart` - LOG_START meldingen
- `bool logStop` - LOG_STOP meldingen
- `bool logTransition` - LOG_TRANSITION meldingen
- `bool logSafety` - LOG_SAFETY meldingen
- `bool logError` - LOG_ERROR meldingen
- `bool logWarning` - LOG_WARNING meldingen

#### `NtfyNotificationType`

Enum voor melding types.

##### Waarden

- `LOG_INFO` - Algemene log informatie
- `LOG_START` - Systeem gestart
- `LOG_STOP` - Systeem gestopt
- `LOG_TRANSITION` - Overgang tussen fasen
- `LOG_SAFETY` - Veiligheidsmeldingen
- `LOG_ERROR` - Foutmeldingen
- `LOG_WARNING` - Waarschuwingen

## Troubleshooting

### Notificaties worden niet ontvangen

1. **Check WiFi verbinding**: Zorg dat de ESP32 verbonden is met WiFi
2. **Check topic naam**: Controleer of de topic naam correct is en dat je erop geabonneerd bent in de NTFY app
3. **Check instellingen**: Controleer of NTFY is ingeschakeld en het juiste melding type is ingeschakeld
4. **Check Serial output**: Bekijk de Serial monitor voor foutmeldingen

### "WiFi niet verbonden" fout

Zorg dat WiFi is geïnitialiseerd voordat je NTFY gebruikt:

```cpp
WiFi.begin("SSID", "password");
while (WiFi.status() != WL_CONNECTED) {
    delay(500);
}
// Nu pas NTFY initialiseren
notifier.begin("topic");
```

### "Topic niet geconfigureerd" fout

Zorg dat je een topic naam hebt ingesteld:

```cpp
notifier.begin("mijn-topic");  // Of
notifier.setTopic("mijn-topic");
```

### Notificaties komen vertraagd aan

- NTFY.sh gebruikt gratis servers die soms vertraging kunnen hebben
- Voor betere performance kun je je eigen NTFY server draaien
- Controleer je internetverbinding

## Voorbeeld Projecten

### Temperatuur Monitor

Zie `3_5_Display_Temperature_MAX6675_2SSR` voor een volledig voorbeeld waarbij NTFY is geïntegreerd met:
- Logger module (automatische notificaties bij logs)
- Web interface (instellingen configureren)
- SettingsStore (persistente opslag)

## Licentie

Deze module is onderdeel van het temperatuur monitor project en volgt dezelfde licentie.
