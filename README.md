# Temperatuursturing Meterkastbrand

ESP32-gebaseerde temperatuurcyclus controller voor meterkastbrand onderzoek met MAX6675 thermocouple sensor.

## Hardware

- **Display:** CYD 2.8 inch (240x320, LVGL graphics, touchscreen)
- **Sensor:** MAX6675 K-type thermocouple (software SPI)
- **Relais:** 2x Solid State Relais (SSR) voor koelen/verwarmen
- **Platform:** ESP32

## Functionaliteit

- **Cyclus logica:** Automatisch verwarmen tot T_top, koelen tot T_bottom
- **Beveiligingen:**
  - Opwarmtijd > 2x gemiddelde → veiligheidskoeling
  - Temperatuur stagnatie > 2 minuten → veiligheidskoeling
  - Veiligheidskoeling met 2 minuten naloop
- **Grafiek:** Real-time temperatuur/tijd grafiek (120 punten, 10 minuten)
- **Logging:** Google Sheets logging via FreeRTOS task
- **Settings:** Non-volatile storage (Preferences)

## Versie

**Huidige versie:** 3.85

Zie [CHANGELOG.md](CHANGELOG.md) voor versiegeschiedenis.

## Documentatie

- [CODE_INDEX.md](CODE_INDEX.md) - Codebase overzicht en architectuur
- [REFACTORING_PLAN.md](REFACTORING_PLAN.md) - Plan voor modulaire refactoring
- [MAX6675_ANALYSE.md](MAX6675_ANALYSE.md) - MAX6675 sensor analyse
- [MAX6675_LIBRARY_REVIEW.md](MAX6675_LIBRARY_REVIEW.md) - Library review

## Instellingen

Instellingen worden opgeslagen in non-volatile memory (ESP32 Preferences):
- `T_top` - Bovenste temperatuur (default: 80°C)
- `T_bottom` - Onderste temperatuur (default: 25°C)
- `cyclus_max` - Maximaal aantal cycli (0 = oneindig)
- `temp_offset` - Kalibratie offset voor MAX6675

## Google Sheets Logging

Logging naar Google Sheets via service account authenticatie:
- **Tabblad:** "DataLog-K"
- **Kolommen:** Timestamp, Meettemperatuur, Status, Huidige cyclus, Totaal cycli, T_top, T_dal, Fase tijd, Cyclus tijd

## Licentie

Zie licentie bestand voor details.

