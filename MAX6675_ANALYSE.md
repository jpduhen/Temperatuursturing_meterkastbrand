# MAX6675 Stabiliteit en Nauwkeurigheid Analyse

## Huidige Implementatie

### Bibliotheek
- **Library**: Rob Tillaart MAX6675 v0.3.3
- **SPI Mode**: Software SPI (niet hardware SPI)
- **Pins**: CS=22, SO(MISO)=35, SCK=27

### Huidige Filtering
1. **Mediaan filter**: 7 samples (~2 seconden bij 0.3s interval)
2. **Gemiddelde berekening**: Voor display updates (elke 2 seconden)
3. **Sanity check**: -200°C tot 1200°C
4. **Status controle**: Alleen STATUS_OK wordt geaccepteerd

### Sampling Frequentie
- **Interval**: 300ms (TEMP_SAMPLE_INTERVAL_MS)
- **Display update**: 2000ms (TEMP_DISPLAY_UPDATE_MS)
- **Grafiek logging**: 5000ms (TEMP_GRAPH_LOG_INTERVAL_MS)

---

## MAX6675 Datasheet Specificaties

### Nauwkeurigheid
- **Resolutie**: 0.25°C
- **Nauwkeurigheid**: ±2°C bij kamertemperatuur (0-1024°C bereik)
- **Lineaire fout**: ±0.75% van de gemeten waarde
- **Koud-junctie compensatie**: Automatisch (intern)

### Timing Requirements
- **Conversietijd (tCONV)**: 220ms typisch, 300ms maximum
- **CS setup tijd**: Minimaal 100ns
- **Clock frequentie**: Max 4.3MHz (maar library gebruikt 1MHz)
- **Tijd tussen conversies**: Minimaal conversietijd + setup tijd

### Status Bits
- **Bit 2 (D2)**: Thermocouple open circuit detectie (1 = open, 0 = OK)
- **Bit 15 (D15)**: Sign bit (0 = positief, 1 = negatief)
- **Bits 3-14**: Temperatuur data (12 bits, 0.25°C resolutie)

---

## Geïdentificeerde Problemen

### 1. **Geen Conversietijd Respectering**
- **Probleem**: De MAX6675 heeft ~220ms nodig voor een conversie, maar er is geen expliciete wachttijd tussen opeenvolgende reads
- **Impact**: Mogelijk onstabiele metingen als er te snel achter elkaar wordt gelezen
- **Huidige situatie**: 300ms interval zou moeten werken, maar er is geen garantie dat de conversie klaar is

### 2. **Geen Kalibratie Offset**
- **Probleem**: De library ondersteunt `setOffset()`, maar dit wordt niet gebruikt in de code
- **Impact**: Systematische fouten kunnen niet worden gecorrigeerd
- **Oplossing**: Kalibratie offset toevoegen via Preferences

### 3. **Beperkte Status Bit Controle**
- **Probleem**: Alleen STATUS_OK wordt gecontroleerd, maar specifieke fouten (open circuit) worden niet gelogd
- **Impact**: Open circuit fouten worden niet gedetecteerd of gerapporteerd
- **Oplossing**: Uitgebreide status controle en logging

### 4. **Software SPI Timing**
- **Probleem**: Library gebruikt standaard `_swSPIdelay = 0`, wat timing problemen kan veroorzaken
- **Impact**: Mogelijke communicatiefouten bij hoge CPU belasting
- **Oplossing**: Expliciete delay instellen voor betere stabiliteit

### 5. **Geen Warm-up Tijd**
- **Probleem**: Na power-up wordt er direct gelezen zonder wachttijd voor stabilisatie
- **Impact**: Eerste metingen kunnen onnauwkeurig zijn
- **Huidige situatie**: 500ms delay na power-up, maar dit is mogelijk niet genoeg

### 6. **Geen Noise Filtering op Raw Data**
- **Probleem**: Mediaan filter werkt op al gelezen data, maar er is geen filtering op de SPI communicatie zelf
- **Impact**: SPI communicatiefouten kunnen door de filter heen komen
- **Oplossing**: Meerdere reads per sample en majority voting

---

## Aanbevolen Verbeteringen

### 1. **Conversietijd Respectering**
```cpp
// Wacht minimaal conversietijd tussen reads
#define MAX6675_CONVERSION_TIME_MS 250  // 220ms typisch + marge
static unsigned long g_lastReadTime = 0;

static float readTempC() {
  unsigned long now = millis();
  if (now - g_lastReadTime < MAX6675_CONVERSION_TIME_MS) {
    return NAN; // Te snel na vorige read
  }
  // ... rest van code
  g_lastReadTime = now;
}
```

### 2. **Kalibratie Offset via Preferences**
```cpp
// Voeg toe aan Preferences
#define PREF_KEY_TEMP_OFFSET "temp_offset"
float temp_offset = 0.0; // Kalibratie offset in °C

// In loadSettings():
temp_offset = preferences.getFloat(PREF_KEY_TEMP_OFFSET, 0.0);
thermocouple.setOffset(temp_offset);
```

### 3. **Uitgebreide Status Controle**
```cpp
static float readTempC() {
  uint8_t state = thermocouple.read();
  uint8_t status = thermocouple.getStatus();
  
  if (state == STATUS_OK) {
    float temp = thermocouple.getCelsius();
    // Controleer op open circuit (bit 2)
    if (status & 0x04) {
      // Thermocouple open circuit - log maar return NAN
      return NAN;
    }
    return temp;
  }
  return NAN;
}
```

### 4. **Software SPI Delay Instellen**
```cpp
// In setup(), na thermocouple.begin():
thermocouple.setSWSPIdelay(1); // 1 microseconde delay voor stabiliteit
```

### 5. **Meerdere Reads per Sample (Majority Voting)**
```cpp
// Lees 3x en gebruik mediaan voor extra stabiliteit
static float readTempC_robust() {
  float readings[3];
  int valid_count = 0;
  
  for (int i = 0; i < 3; i++) {
    float t = readTempC();
    if (!isnan(t)) {
      readings[valid_count++] = t;
    }
    delay(10); // Korte delay tussen reads
  }
  
  if (valid_count == 0) return NAN;
  if (valid_count == 1) return readings[0];
  
  // Sorteer en gebruik mediaan
  // ... sorting code ...
  return readings[valid_count/2];
}
```

### 6. **Warm-up Tijd Verlengen**
```cpp
#define MAX6675_WARMUP_TIME_MS 1000  // 1 seconde warm-up tijd
// In setup():
delay(MAX6675_WARMUP_TIME_MS);
// Discard eerste paar metingen
for (int i = 0; i < 3; i++) {
  (void)readTempC();
  delay(250);
}
```

---

## Prioriteit van Verbeteringen

### **Hoog (Direct Implementeren)**
1. ✅ Conversietijd respectering (voorkomt onstabiele metingen)
2. ✅ Software SPI delay instellen (betere communicatie stabiliteit)
3. ✅ Uitgebreide status controle (open circuit detectie)

### **Medium (Belangrijk voor Nauwkeurigheid)**
4. ✅ Kalibratie offset via Preferences (corrigeert systematische fouten)
5. ✅ Warm-up tijd verlengen (betere eerste metingen)

### **Laag (Nice to Have)**
6. ⚠️ Meerdere reads per sample (extra stabiliteit, maar verhoogt CPU belasting)

---

## Test Plan

### Stabiliteit Test
1. Laat systeem 1 uur draaien op constante temperatuur
2. Meet standaarddeviatie van metingen
3. Controleer op uitschieters (>3σ)

### Nauwkeurigheid Test
1. Vergelijk met referentie thermometer bij verschillende temperaturen (25°C, 100°C, 200°C)
2. Bereken systematische fout
3. Pas kalibratie offset toe

### Open Circuit Test
1. Koppel thermocouple los tijdens meting
2. Controleer of open circuit wordt gedetecteerd
3. Controleer of systeem correct reageert (NAN, geen crash)

---

## Conclusie

De huidige implementatie is redelijk stabiel door het gebruik van mediaan filtering, maar kan worden verbeterd door:
1. Respecteren van conversietijd
2. Toevoegen van kalibratie offset
3. Uitgebreide status controle
4. Betere software SPI timing

Deze verbeteringen zullen de stabiliteit en nauwkeurigheid verhogen zonder grote code wijzigingen.


