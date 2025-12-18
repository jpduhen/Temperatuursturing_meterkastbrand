# Temperatuur Cyclus Controller

Arduino/ESP32 project voor geautomatiseerde temperatuurcycli met MAX6675 thermocouple sensor, LVGL touchscreen interface, en Google Sheets logging.

## Overzicht

Dit project is ontwikkeld voor het onderzoek naar meterkastbranden (kooiklem maximaaltest). Het systeem voert geautomatiseerde temperatuurcycli uit: opwarmen tot een bovenwaarde (T_top), afkoelen tot een onderwaarde (T_bottom), en dit herhalen voor een configureerbaar aantal cycli.

## Hardware

- **ESP32 Development Board** (ESP32_DEV)
- **CYD 2.8 inch Display** met LVGL graphics en touch bediening
- **MAX6675 Thermocouple Sensor** (CS=22, SO=35, SCK=27)
- **Solid State Relais (SSR)** voor verwarming (GPIO 23) en koeling (GPIO 5)

## Functionaliteit

### Basis Functionaliteit
- **Cyclus Logica**: Automatisch opwarmen tot T_top, afkoelen tot T_bottom
- **Temperatuur Instellingen**: Configureerbaar via +/- knoppen (0-350°C, min 5°C verschil)
- **Cycli Insteller**: Configureerbaar aantal cycli (0 = oneindig)
- **Dynamische Start/Stop Knoppen**: Kleur op basis van systeemstatus
- **Intelligente Start Modus**: Start op basis van huidige temperatuur
- **Veiligheidskoeling**: Koelt automatisch tot <35°C voordat systeem uitgaat bij STOP

### Display Features
- **Temperatuur Update**: Elke 285ms met mediaan van laatste 7 metingen
- **Kleurcodering**: Groen <37°C (veilig), rood ≥37°C (niet veilig aanraken)
- **Opwarmen/Afkoelen Timers**: Real-time weergave in m:ss formaat
- **Status Tekst**: Rechts uitgelijnd met actuele systeemstatus
- **Versienummer**: Weergave in rechter onderhoek

### Grafiek Functionaliteit
- **Temperatuur/Tijd Grafiek**: 280x155px, 120 datapunten, 6 divisielijnen
- **Automatische Logging**: Elke 5 seconden tijdens actieve cyclus
- **Kleurcodering**: Rood voor stijgende temperatuur, blauw voor dalende
- **Dynamische Y-as Labels**: Automatisch aangepast op basis van T_top (20°C tot T_top)
- **X-as Minuten Labels**: 9-0 minuten weergave
- **Real-time Updates**: Minimale vertraging bij nieuwe meetpunten

### Google Sheets Logging
- **FreeRTOS Task**: Logging op Core 1 (lagere prioriteit) zodat Core 0 beschikbaar blijft voor real-time taken
- **Logging Events**: START, verwarmen→koelen, koelen→verwarmen, STOP, veiligheidskoeling afgerond
- **Tabblad**: "DataLog-K" met 9 kolommen
- **Timestamp Formaat**: [yy-mm-dd hh:mm:ss] met WiFi en NTP tijd synchronisatie
- **Cyclus Tijd**: Totaaltijd van volledige cyclus (opwarmen + afkoelen) wordt gelogd bij "Afkoelen tot Opwarmen"

### NTFY Push Notificaties
- **Automatische Notificaties**: Alle logging events worden automatisch als push notificaties verstuurd
- **Configureerbare Types**: Per melding type in/uitschakelbaar (START, STOP, transitions, safety, errors, warnings)
- **NTFY.sh Service**: Gebruikt gratis NTFY.sh service voor push notificaties
- **Web Interface**: Topic en instellingen configureerbaar via web interface
- **Persistente Opslag**: Topic en instellingen worden opgeslagen in Preferences

### Beveiligingsfuncties
1. **Opwarmtijd Beveiliging**: Als opwarmen langer duurt dan 2x de gemiddelde opwarmtijd, wordt veiligheidskoeling geactiveerd en het systeem schakelt uit
2. **Veiligheidskoeling Naloop**: 2 minuten naloop na veiligheidskoeling om voldoende afkoeling te garanderen
3. **Temperatuur Stagnatie Detectie**: Als temperatuur langer dan 2 minuten binnen een 3°C bandbreedte blijft, wordt veiligheidskoeling geactiveerd (detecteert losse thermocouple)

### Instellingen Opslag
- **Non-volatile Memory**: T_top, T_bottom, cyclus_max, temp_offset worden opgeslagen in Preferences
- **Google Sheets Credentials**: Client email, project ID, private key, spreadsheet ID worden opgeslagen in Preferences
- **NTFY Instellingen**: Topic naam en melding type instellingen worden opgeslagen in Preferences
- **Automatisch Laden**: Instellingen worden automatisch geladen bij opstarten
- **Automatisch Opslaan**: Instellingen worden automatisch opgeslagen bij wijziging

## Software Architectuur

Het project gebruikt een modulaire architectuur met de volgende modules:

### Modules

1. **SystemClock** (`src/SystemClock/`)
   - NTP tijd synchronisatie
   - Timestamp conversie (millis() → Unix tijd)
   - Tijdzone support

2. **SettingsStore** (`src/SettingsStore/`)
   - Non-volatile opslag via Preferences
   - Load/save functionaliteit voor instellingen
   - Callback support voor logging

3. **TempSensor** (`src/TempSensor/`)
   - MAX6675 sensor communicatie
   - Mediaan filter (laatste 7 metingen)
   - Open-circuit detectie
   - Temperatuur offset correctie
   - Majority voting voor kritieke metingen

4. **Logger** (`src/Logger/`)
   - FreeRTOS logging task op Core 1
   - Google Sheets API integratie
   - Rate limiting en retry mechanisme
   - Queue management

5. **CycleController** (`src/CycleController/`)
   - Opwarmen/afkoelen state machine
   - Beveiligingsfuncties (opwarmtijd, stagnatie detectie)
   - Cyclus teller
   - Relais besturing
   - Transition callbacks voor logging

6. **UIController** (`src/UIController/`)
   - LVGL scherm creatie en beheer
   - Event handlers voor knoppen
   - Grafiek data logging en updates
   - GUI updates (temperatuur, status, timers, knoppen)
   - Status weergave (WiFi, Google Sheets)

7. **NtfyNotifier** (`src/NtfyNotifier/`)
   - Push notificaties via NTFY.sh
   - Automatische notificaties bij logging events
   - Configureerbare melding types (START, STOP, transitions, safety, etc.)
   - Web interface integratie voor instellingen
   - Persistente opslag van topic en instellingen

8. **WebServer** (`src/WebServer/`)
   - HTTP webserver voor configuratie
   - Real-time status API (JSON)
   - Instellingen beheer (HTML formulier)
   - START/STOP controle
   - Google Sheets credentials beheer
   - NTFY notificatie configuratie

## Installatie

### Vereisten

1. **Arduino IDE** met ESP32 board support
2. **Libraries** (via Library Manager):
   - LVGL (versie 9.4.0)
   - TFT_eSPI (versie 2.5.43)
   - XPT2046_Touchscreen (versie 1.4)
   - WiFiManager (versie 2.0.17)
   - ESP-Google-Sheet-Client (versie 1.4.13)
   - ESP_SSLClient (versie 3.0.6)
   - MAX6675 (versie 0.3.3)

3. **Google Cloud Service Account**:
   - Maak een service account aan in Google Cloud Console
   - Download de JSON credentials
   - Plaats het bestand in de project root (wordt automatisch genegeerd door .gitignore)

### Configuratie

1. **Google Sheets Setup**:
   - Maak een Google Spreadsheet
   - Deel het met het service account e-mailadres
   - Kopieer de Spreadsheet ID
   - Update `SPREADSHEET_ID` in de code

2. **WiFi Configuratie**:
   - Bij eerste opstarten: connecteer met WiFi AP "ESP32-TC"
   - Configureer WiFi credentials via web interface
   - Na configuratie: automatische verbinding bij volgende opstarten
   - Web interface beschikbaar op AP IP adres (standaard: 192.168.4.1)

3. **NTFY Notificaties Setup** (Optioneel):
   - Installeer NTFY app op telefoon/tablet
   - Kies een unieke topic naam (bijv. "mijn-temperatuur-monitor-2024")
   - Abonneer op het topic in de NTFY app
   - Configureer topic en instellingen via web interface

4. **Hardware Aansluitingen**:
   - MAX6675: CS=22, SO=35, SCK=27
   - SSR Verwarming: GPIO 23
   - SSR Koeling: GPIO 5
   - Display: SPI via TFT_eSPI
   - Touchscreen: XPT2046 via SPI

## Gebruik

### Basis Operatie

1. **Opstarten**: Systeem start automatisch en synchroniseert tijd via NTP
2. **Instellingen Aanpassen**: Gebruik +/- knoppen om T_top, T_bottom, of cyclus_max aan te passen
3. **Cyclus Starten**: Druk op START knop (groen wanneer systeem gereed is)
4. **Cyclus Stoppen**: Druk op STOP knop (rood wanneer systeem actief is)
5. **Grafiek Bekijken**: Druk op GRAFIEK knop om temperatuur/tijd grafiek te bekijken

### Status Indicatoren

- **Temperatuur**: Groen <37°C (veilig), rood ≥37°C (niet veilig)
- **Status**: Gereed, Verwarmen, Koelen, Veiligheidskoeling, Uit
- **Cycli**: Huidige cyclus / Maximaal aantal cycli (of "inf" voor oneindig)
- **Timers**: Real-time opwarmen/afkoelen tijd in m:ss formaat

### Grafiek Scherm

- **Y-as**: Dynamisch bereik van 20°C tot T_top (6 labels)
- **X-as**: Minuten labels (9-0)
- **Kleurcodering**: Rood voor stijgende temperatuur, blauw voor dalende
- **Real-time Updates**: Nieuwe punten worden direct toegevoegd tijdens actieve cyclus

## Technische Details

### Temperatuur Meting
- **Sampling Interval**: 285ms
- **Mediaan Filter**: Laatste 7 metingen
- **Display Update**: Elke 285ms (synchroon met sampling)
- **Grafiek Logging**: Elke 5 seconden
- **Kritieke Metingen**: Majority voting met 3 samples

### FreeRTOS Tasks
- **Core 0**: Main loop, UI updates, temperatuur meting, cyclus logica
- **Core 1**: Logger task (Google Sheets logging)

### Memory Management
- **Char Arrays**: Gebruikt i.p.v. String om heap fragmentatie te voorkomen
- **Static Buffers**: Grafiek data (120 punten), display buffer
- **Dynamic Allocation**: Alleen tijdens initialisatie

## Versie Geschiedenis

Zie [CHANGELOG.md](CHANGELOG.md) voor gedetailleerde versie geschiedenis.

**Huidige Versie**: 4.03

## Licentie

Dit project is ontwikkeld voor onderzoeksdoeleinden.

## Auteur

Jan Pieter Duhen

## Ondersteuning

Voor vragen of problemen, raadpleeg de code documentatie of de CHANGELOG.md voor bekende issues en oplossingen.
