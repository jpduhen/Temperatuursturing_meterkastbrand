# Changelog - Arduino Temperatuur Cyclus Controller

Dit document bevat een overzicht van alle wijzigingen per versie. Dit helpt bij het verder ontwikkelen op verschillende computers.

## Versie 3.85 (Huidige versie)

**Temperatuur Stagnatie Beveiliging - Losse Thermokoppel Detectie:**
- Beveiliging toegevoegd die detecteert wanneer temperatuur te lang constant blijft (losse thermokoppel)
- Detecteert wanneer temperatuur langer dan 2 minuten binnen bandbreedte van 3 graden blijft
- Bij detectie: veiligheidskoeling wordt geactiveerd en systeem wordt uitgeschakeld
- Logging toegevoegd: "Beveiliging: Temperatuur stagnatie" wordt gelogd wanneer beveiliging activeert
- Stagnatie tracking wordt gereset bij faseovergangen (opwarmen→afkoelen) en bij START
- Stagnatie tracking wordt ook gereset wanneer temperatuur weer buiten bandbreedte komt
- Voorkomt dat verwarming oneindig lang aan blijft bij losse thermokoppel

---

## Versie 3.84

**Veiligheidskoeling Naloop - 2 Minuten Extra Koeling:**
- Naloop van 2 minuten toegevoegd aan veiligheidskoeling voor inductie-verwarming
- Na bereiken van veiligheidstemperatuur (< 35°C) blijft koeling nog 2 minuten actief
- Dit voorkomt dat restwarmte in het systeem de temperatuur weer laat oplopen
- Naloop timer wordt gereset als temperatuur weer boven 35°C komt tijdens koeling
- Alle onderdelen worden nu voldoende afgekoeld voordat systeem volledig uitschakelt

---

## Versie 3.83

**Opwarmen Beveiliging - 2x Gemiddelde Tijd Check:**
- Beveiliging toegevoegd die voorkomt dat opwarmen langer duurt dan 2x de gemiddelde opwarmtijd
- Gemiddelde opwarmtijd wordt bijgehouden met exponentiële moving average (70% oude waarde + 30% nieuwe waarde)
- Bij detectie van te lange opwarmtijd: veiligheidskoeling wordt geactiveerd en systeem wordt uitgeschakeld
- Logging toegevoegd: "Beveiliging: Opwarmen te lang" wordt gelogd wanneer beveiliging activeert
- Gemiddelde opwarmtijd wordt gereset bij START
- Beveiliging werkt alleen na minstens 1 succesvolle opwarmfase (om betrouwbaar gemiddelde te hebben)

---

## Versie 3.82

**Grafiek Fix - Meetpunten Verschijnen Direct na Scherm Wissel:**
- Fix voor probleem waarbij meetpunten niet verschenen wanneer grafiekscherm werd geopend kort na START
- `fill_chart_completely()` en `update_graph_display()` aangepast: correcte start index berekening
- Als `graph_count < GRAPH_POINTS`: begin bij index 0 (eerste punt)
- Als `graph_count == GRAPH_POINTS`: begin bij `graph_write_index` (oudste punt in circulaire buffer)
- `last_displayed_time` wordt nu correct gereset bij `graph_force_rebuild`
- Directe grafiek update toegevoegd in `graph_button_event()` na scherm wissel
- Meetpunten verschijnen nu direct wanneer grafiekscherm wordt geopend, ook kort na START

---

## Versie 3.81

**Grafiek Real-Time Updates en Cyclus Teller Fix:**
- Grafiek update interval verlaagd van 2 seconden naar 0.5 seconde voor snellere updates
- Directe grafiek update toegevoegd in `log_graph_data()`: wanneer nieuwe data wordt gelogd en grafiek scherm actief is, wordt grafiek direct bijgewerkt
- Minimale vertraging tussen data logging en grafiek weergave (real-time updates)
- Cyclus teller fix: bij "Afkoelen tot Opwarmen" wordt nu `cyclus_teller - 1` gelogd (vorige cyclus) i.p.v. huidige cyclus
- Dit komt overeen met de cyclus_tijd die wordt gelogd (beide verwijzen naar de cyclus die net is afgerond)

---

## Versie 3.80

**Grafiek Update Fix - Hangende Waarden:**
- `update_graph_display()` volledig herschreven: gebruikt nu tijd-gebaseerde tracking i.p.v. count-gebaseerd
- Gebruikt `last_displayed_time` om te bepalen welke punten nieuw zijn (i.p.v. `last_displayed_count`)
- Loopt door alle punten en voegt toe als `graph_times[i] > last_displayed_time`
- Geen limiet meer op aantal punten per update - voegt alle nieuwe punten toe
- Grafiek blijft niet meer hangen op één positie - alle nieuwe meetwaarden worden direct getoond

---

## Versie 3.79

**Cyclus Tijd Logging Verbetering:**
- Cyclus tijd wordt nu alleen gelogd bij "Afkoelen tot Opwarmen" (wanneer volledige cyclus is afgerond)
- Bij "Opwarmen tot Afkoelen" wordt cyclus tijd kolom leeg gelaten (lege string i.p.v. "0:00")
- Dit maakt de logging duidelijker: cyclus tijd alleen zichtbaar wanneer de volledige cyclus (opwarmen + afkoelen) is voltooid

---

## Versie 3.78

**Grafiek Volledige Herimplementatie - Eenvoudige Circulaire Array:**
- Grafiek data structuur volledig herschreven naar eenvoudige circulaire array
- Nieuwe variabelen: `graph_write_index` (write index, 0-119), `graph_count` (aantal punten, 0-120), `graph_force_rebuild` (rebuild flag)
- `log_graph_data()`: Schrijft naar `graph_write_index`, wrapt automatisch rond, verhoogt `graph_count`
- `fill_chart_completely()`: Begin bij `graph_write_index` (oudste), loop `graph_count` punten verder in chronologische volgorde
- `update_graph_display()`: Voegt alleen nieuwe punten toe sinds laatste update, gebruikt `last_displayed_count` tracking
- Verwijderd: Complexe wrap-around logica, tijd-gebaseerde sortering, `graph_last_displayed`, `graph_last_displayed_time`, `graph_chart_point_count`
- Verwijderd: `update_graph_data()` functie (niet meer nodig)
- Grafiek toont nu altijd correct de volledige 10 minuten (120 punten) zonder hangende waarden

---

## Versie 3.77

**Grafiek Verbeteringen - Volledige Weergave en Hangende Waarden Fix:**
- Fix voor probleem waarbij grafiek alleen laatste 5 minuten toonde na terugkeren van meetscherm
- `fill_chart_completely()` aangepast: bij wrap-around worden nu alle 120 indices bekeken (0-119)
- Gap-detectie toegevoegd: als er een gap >2 minuten is tussen laatst weergegeven en nieuwste data, wordt grafiek opnieuw opgebouwd
- Tijd-check in `update_graph_display()` soepeler gemaakt: marge verhoogd van 10 naar 60 seconden
- Dit voorkomt dat punten worden overgeslagen bij kleine timing verschillen of wrap-around situaties
- Grafiek toont nu altijd volledige 10 minuten (120 punten) en hangt niet meer op vaste waarden

---

## Versie 3.76

**Verbeterde Knopbediening Responsiviteit:**
- Logging task verplaatst van Core 0 naar Core 1 voor betere responsiviteit
- Logging task prioriteit verlaagd van 2 naar 1 zodat knopbediening prioriteit heeft
- Core 0 is nu volledig beschikbaar voor real-time taken (WiFi stack en systeemtaken)
- Main loop en logging task draaien nu beide op Core 1, maar logging blokkeert minder
- Knopbediening is nu merkbaar responsiever door betere CPU-tijd verdeling

---

## Versie 3.75

**Grafiek-opbouw Fix - Hangende Meetwaarden:**
- Fix voor probleem waarbij meetwaarden soms "hingen" op een vaste waarde terwijl sensordata wel wijzigde
- Tijd-check aangepast van `>` naar `>=` om punten met gelijke tijdstempels niet over te slaan
- Wrap-around logica verbeterd: punten met tijd binnen 10 seconden worden geaccepteerd bij wrap-around
- Tracking variabelen verbeterd: `graph_last_displayed_time` behoudt altijd de hoogste tijd om teruggaan te voorkomen
- Alle nieuwe meetwaarden worden nu correct getoond, ook bij gelijke tijdstempels of wrap-around situaties

---

## Versie 3.74

**Geheugenbewaking Verwijderd:**
- Alle geheugenbewaking code verwijderd (geen problemen meer geconstateerd)
- `MEM_WARN_*` defines verwijderd
- `MEM_CHECK_INTERVAL_MS` define verwijderd
- `text_label_mem_warning` GUI element verwijderd
- Geheugen monitoring code uit `updateGUI()` verwijderd (~60 regels)
- Geheugen monitoring code uit `loop()` verwijderd (~40 regels)
- Header comment bijgewerkt (geheugenwaarschuwingen verwijderd)
- ~100 regels code verwijderd, code is eenvoudiger en sneller

---

## Versie 3.73

**Code Opschoning en Refactoring:**
- `getMedianTemp()` vereenvoudigd: verwijderd overbodige fallback logica die circulaire array direct gebruikte
- `calculateMedian()` functie verwijderd (niet gebruikt, ~50 regels)
- `calculateMedianFromArray()` bugfix: buffer size verhoogd van 3 naar 7 samples (was te klein voor circulaire array)
- `TEMP_MEDIAN_SAMPLES` define verplaatst naar vóór gebruik (compile fix)

**Dubbele Code Geëlimineerd:**
- Helper functie `updateTempDisplay()` toegevoegd: vervangt 2 identieke code blokken in `handleHeatingPhase()` en `handleCoolingPhase()`
- Helper functie `getCriticalTemp()` toegevoegd: vervangt 3 identieke code blokken voor kritieke temperatuurmeting met fallback
- ~45 regels dubbele code verwijderd, betere onderhoudbaarheid

**Overbodige Code Verwijderd:**
- Ongebruikte constante `TEMP_CELSIUS` verwijderd
- Ongebruikte functie `systeemUit()` verwijderd (identiek aan `alleRelaisUit()`, maar nergens gebruikt)
- ~25 overbodige "BELANGRIJK:" comments verwijderd/vereenvoudigd

**Resultaat:**
- ~142 regels code verwijderd/vereenvoudigd
- Code is schoner, beter onderhoudbaar en compacter
- Geen functionaliteit verloren, alleen code kwaliteit verbeterd

---

## Versie 3.72

**Circulaire Array met Mediaan Filtering:**
- Nieuwe temperatuurmeetmethode: circulaire array met 7 waarden
- Elke 285ms nieuwe meting, mediaan wordt direct berekend en gebruikt
- Display update elke 285ms (synchroon met sampling) met mediaan waarde
- Fluctuaties sterk verminderd door mediaanfiltering
- Logging gebruikt ook mediaan waarde (consistent met display)
- Bij eerste 7 metingen: mediaan wordt berekend zodra er 2+ waarden zijn

**Responsievere Knopbediening:**
- Alle knoppen gebruiken nu `LV_EVENT_PRESSED` i.p.v. `LV_EVENT_CLICKED` voor snellere respons
- LVGL task handler wordt nu 10-15x per loop aangeroepen (was 2-3x)
- Non-blocking delays: `delay()` vervangen door `yield()` + `lv_task_handler()` loops
- Touch events worden verwerkt tijdens langdurige operaties (temperatuurmetingen, grafiekupdates)
- Terug-knop is nu bijna instantaan responsief

**Verbeterde Temperatuurweergave:**
- Laatste geldige temperatuurwaarde wordt behouden (geen "--.--" meer bij tijdelijke fouten)
- Scherm en log tonen nu exact dezelfde temperatuurwaarde bij faseovergangen
- Display wordt direct geüpdatet bij faseovergangen (zonder te wachten op normale update interval)

**Temperatuur Conversie Versneld:**
- `readTempC_critical()` gebruikt nu 3 samples i.p.v. 5 (was ~250ms, nu ~90ms)
- Sample delay verlaagd van 50ms naar 30ms
- Minimum valid_count verlaagd van 3 naar 2
- Conversietijd ongeveer 3x sneller

**Logging Verbeteringen:**
- Laatste afkoelfase wordt nu altijd gelogd voordat systeem stopt
- Bij bereiken van max cycli: eerst "Afkoelen tot Opwarmen" log, daarna "Uit" log

---

## Versie 3.71

**Responsievere Knopbediening en Temperatuur Verbeteringen:**
- Laatste geldige temperatuurwaarde behouden om "--.--" te voorkomen
- Knopbediening responsiever gemaakt door LV_EVENT_PRESSED te gebruiken
- Scherm en log synchroniseren - beide tonen nu dezelfde temperatuurwaarde
- Temperatuur conversie versneld - minder samples en kortere delays

---

## Versie 3.70

**Majority Voting voor Kritieke Metingen:**
- Majority voting toegevoegd voor kritieke temperatuurmetingen bij faseovergangen
- Nieuwe functie `readTempC_critical()`: doet 5 reads met 50ms delay en gebruikt mediaan voor extra robuustheid
- Helper functie `calculateMedianFromArray()`: berekent mediaan van een float array (insertion sort)
- Kritieke checks gebruiken nu majority voting:
  - `handleHeatingPhase()`: T_top bereik check (opwarmen→afkoelen overgang)
  - `handleCoolingPhase()`: T_bottom bereik check (afkoelen→opwarmen overgang)
  - `handleSafetyCooling()`: Veiligheidskoeling check (< 35°C)
- Fallback mechanisme: bij falen van critical read wordt normale mediaan gebruikt
- Nieuwe constanten: `MAX6675_CRITICAL_SAMPLES` (5) en `MAX6675_CRITICAL_SAMPLE_DELAY_MS` (50ms)
- WARNING: Critical reads duren langer (~250ms * 5 = ~1.25s) - alleen gebruikt voor kritieke beslissingen

---

## Versie 3.69

**MAX6675 Library Verbeteringen - Robuustheid en Betrouwbaarheid:**
- Retry mechanisme toegevoegd: bij tijdelijke communicatiefouten wordt nu 3x opnieuw geprobeerd met 10ms delay tussen retries
- Data validatie verbeterd: controle op onrealistische waarden (< -200°C of > 1200°C) direct na library read
- Code herstructurering: `readTempC()` splitst in `readTempC_single()` (interne functie) en `readTempC()` (publieke functie met retry)
- Nieuwe constanten: `MAX6675_READ_RETRIES` (3) en `MAX6675_RETRY_DELAY_MS` (10ms)
- Verbeterde error recovery: tijdelijke SPI fouten worden nu automatisch hersteld zonder data verlies
- Zie `MAX6675_LIBRARY_REVIEW.md` voor volledige analyse van library en aanbevelingen

---

## Versie 3.68

**Knop en status tekstregel posities gecorrigeerd:**
- Alle knoppen en status tekstregels 10 pixels naar beneden verplaatst (correctie van eerdere wijziging)
- START knop: Y-positie van -15 naar -5 (BOTTOM_LEFT alignment)
- GRAFIEK knop: Y-positie van -15 naar -5 (BOTTOM_MID alignment)
- STOP knop: Y-positie van -15 naar -5 (BOTTOM_RIGHT alignment)
- TERUG knop (grafiekscherm): Y-positie van -15 naar -5 (BOTTOM_MID alignment)
- WiFi initialiseren tekst: Y-positie van 157 naar 167 (TOP_LEFT alignment)
- WiFi SSID/IP status regel: Y-positie van -75 naar -65 (BOTTOM_LEFT alignment)
- Google Sheets status regel: Y-positie van -60 naar -50 (BOTTOM_LEFT alignment)

---

## Versie 3.67

**Knop posities aangepast:**
- Alle knoppen 5 pixels naar beneden verplaatst voor betere positionering
- START knop: Y-positie van -10 naar -15
- GRAFIEK knop: Y-positie van -10 naar -15
- STOP knop: Y-positie van -10 naar -15
- TERUG knop (grafiekscherm): Y-positie van -10 naar -15

---

## Versie 3.66

**MAX6675 Stabiliteit en Nauwkeurigheid Verbeteringen:**
- Conversietijd respectering toegevoegd: MAX6675 heeft ~220ms nodig voor conversie, code respecteert nu minimaal 250ms tussen reads
- Software SPI delay ingesteld (1µs) voor betere communicatie stabiliteit bij hoge CPU belasting
- Uitgebreide status controle: open circuit detectie (bit 2) wordt nu expliciet gecontroleerd en gerapporteerd als NAN
- Kalibratie offset toegevoegd via Preferences: `temp_offset` kan worden opgeslagen en wordt automatisch toegepast
- Warm-up tijd verbeterd: 1 seconde warm-up + eerste 3 metingen worden gediscard voor stabiliteit
- `readTempC()` functie volledig herschreven met verbeterde foutdetectie en timing controle
- Nieuwe constanten: `MAX6675_CONVERSION_TIME_MS`, `MAX6675_WARMUP_TIME_MS`, `MAX6675_SW_SPI_DELAY_US`
- `g_lastMax6675ReadTime` timer toegevoegd om conversietijd te respecteren
- Zie `MAX6675_ANALYSE.md` voor volledige analyse en test plan

---

## Versie 3.65

**Grafiek sliding window implementatie:**
- ECHTE SLIDING WINDOW: Eerste keer volledig opbouwen, daarna alleen nieuwe punten toevoegen
- LVGL SHIFT mode verwijdert automatisch oudste punt (m0 valt uit wanneer m120 wordt toegevoegd)
- `graph_chart_point_count` toegevoegd om bij te houden hoeveel punten in chart zitten

---

## Versie 3.64

**Grafiek herimplementatie:**
- RADICAAL ANDERE AANPAK: Grafiek wordt nu altijd volledig opnieuw opgebouwd met laatste 120 punten gesorteerd op tijd
- Geen complexe wrap-around logica meer, gewoon sorteren op tijd en laatste 120 tonen

---

## Versie 3.63

**Grafiek wrap-around fix:**
- Bij opnieuw openen grafiekscherm worden nu alle 120 punten correct getoond (0-119 in chronologische volgorde)

---

## Versie 3.62

**Grafiek volledig herschreven:**
- Grafiek volledig herschreven als sliding window: laatste 120 punten blijven zichtbaar, oude punten schuiven automatisch uit

---

## Versie 3.61

**Grafiek wrap-around fix:**
- Eerste keer display bij wrap-around toont nu laatste 120 punten in chronologische volgorde

---

## Versie 3.60

**Cyclus stop fix:**
- Systeem stopt nu pas NA de laatste cyclus is afgerond (cyclus_teller > cyclus_max)

---

## Versie 3.59

**Grafiek wrap-around fix:**
- Grafiek loopt nu door na 9 minuten zonder opnieuw te beginnen

---

## Versie 3.58

**Logging en grafiek verbeteringen:**
- Logging tekst aangepast ("Opwarmen tot Afkoelen", "Afkoelen tot Opwarmen")
- Grafiek doorloop bij scherm wissel (geen wissen bij terugkeren)
- Grafiek punten limiet (max 120 punten)

---

## Versie 3.57

**Grafiek verbeteringen:**
- Grafiek doorloop verbeterd
- Timer labels breedte verhoogd

---

## Versie 3.56

**Grafiek doorloop:**
- Grafiek doorloop bij wrap-around (geen reset na 10 minuten)

---

## Versie 3.55

**Temperatuur limiet verhoogd:**
- T_TOP maximum verhoogd naar 350°C

---

## Versie 3.54

**Solid State Relais implementatie:**
- Solid State Relais (SSR) implementatie (GPIO 5/23)

---

## Versie 3.53

**Temperatuur limiet verhoogd:**
- T_TOP maximum verhoogd naar 250°C

---

## Versie 3.52

**Logging logica aangepast:**
- Verwijderd: dubbele "Opwarmen" logging bij START
- Logging gebeurt nu alleen bij faseovergangen (opwarmen→afkoelen, afkoelen→opwarmen)
- Bij opwarmen→afkoelen: log "Afkoelen" met temperatuur op moment van overgang (T_top)
- Bij afkoelen→opwarmen: log "Opwarmen" met temperatuur op moment van overgang (T_bottom)
- Fasetijd toont nu de duur van de fase die net is afgelopen
- Temperatuur is nu de temperatuur op het moment van faseovergang (niet gemiddelde)
- Dit zorgt voor correcte logging: 1 regel per faseovergang met juiste temperatuur en fasetijd

---

## Versie 3.51

**Verwarming reset pulsen verwijderd:**
- Reset pulsen op GPIO 23 elke minuut zijn niet meer nodig
- Alle code voor verwarming reset functionaliteit verwijderd
- `HEATING_RESET_INTERVAL_MS` en `HEATING_RESET_DURATION_MS` constanten verwijderd
- `verwarming_reset_tijd` en `verwarming_reset_start` variabelen verwijderd
- Reset logica verwijderd uit `handleHeatingPhase()`
- Alle initialisaties van reset variabelen verwijderd

---

## Versie 3.50

**Mediaan berekening geoptimaliseerd:**
- Mediaan berekening geoptimaliseerd (insertion sort, caching)
- Delays verminderd
- Dit voorkomt CPU overbelasting die crashes kan veroorzaken rond T_top bereik
- Onnodige delays verwijderd uit kritieke overgang (delay(1) en delay(5))
- Alleen yield() calls behouden voor watchdog feeding
- Dit voorkomt timing issues die crashes kunnen veroorzaken

---

## Versie 3.39

**Tweede refactoring ronde:**

**MAGIC NUMBERS VERVANGEN:**
- Alle magic numbers vervangen door named constants (20+ constanten)
- Temperatuur, timing, geheugen en queue constanten gedefinieerd
- Betere leesbaarheid en onderhoudbaarheid

**CODE DUPLICATIE GEËLIMINEERD:**
- Helper functie `saveAndLogSetting()` toegevoegd
- Alle 6 event handlers vereenvoudigd (~40 regels verwijderd)

**COMPLEXE FUNCTIES OPGESPLITST:**
- `cyclusLogica()` opgesplitst in 3 helper functies
- Van ~150 regels naar ~30 regels (`handleSafetyCooling`, `handleHeatingPhase`, `handleCoolingPhase`)
- Betere code organisatie en onderhoudbaarheid

**CODE VEREENVOUDIGING:**
- Ongebruikte variabele `lastCheckmarkTime` verwijderd
- String vergelijkingen gefixed (== naar strcmp) - bugfix
- Helper functie `resetFaseTijd()` toegevoegd (5 plaatsen vereenvoudigd)

**COMMENTAAR OPGERUIMD:**
- Overbodige "BELANGRIJK:" comments verwijderd/vereenvoudigd
- Commentaar headers vereenvoudigd
- Lege regels en redundante comments opgeruimd

---

## Versie 3.38

**Eerste refactoring ronde - CODE OPSCHONING:**
- Simulatie code volledig verwijderd
- Ongebruikte SERIAL_DEBUG code blokken verwijderd
- Overbodige Serial.println statements verwijderd
- Ongebruikte backward compatibility functies verwijderd
- Ongebruikte `logMemoryStats()` functie verwijderd
- Oude versie changelog comments verwijderd
- Code vereenvoudigd en gestabiliseerd

---

## Versie 3.37

**Grafiek wis functie en serial logging verwijderd:**
- `clear_chart()` functie toegevoegd om alle oude punten uit grafiek te wissen
- Bij drukken op grafiekknop worden nu eerst alle oude punten gewist voordat nieuwe data wordt weergegeven
- Voorkomt dat resten van vorige metingen zichtbaar blijven
- Alle Serial.print/println/printf statements verwijderd om crashes door timing issues te voorkomen
- Geen serial output meer tijdens normale operatie

---

## Versie 3.36

**Tijdzone correctie, grafiek fix en schakellogica verbeterd:**
- NTP tijdzone offset toegevoegd: +1 uur (3600 seconden) voor lokale tijd
- Timestamps in Google Sheets logging gebruiken nu lokale tijd in plaats van UTC
- Grafiek start nu vanaf index 0 (begin) in plaats van vanaf einde
- Bij openen grafiek scherm worden chart series gereset en `graph_last_displayed` op -1 gezet
- Alle data vanaf START wordt nu correct weergegeven, niet alleen laatste minuut
- Logica herstructureerd: eerst controleren of `cyclus_actief` true is
- Als `cyclus_actief` true is, wordt veiligheidskoeling check volledig overgeslagen
- Veiligheidskoeling check alleen actief als `!cyclus_actief && systeem_uit`
- Voorkomt definitief dat systeem op 35°C schakelt in plaats van T_dal tijdens normale cycli

---

## Versie 3.35

**Schakellogica verbeterd (T_dal fix):**
- Verbeterde logica om te voorkomen dat systeem op 35°C schakelt in plaats van T_dal tijdens normale cycli

---

## Versie 3.34

**Diverse fixes:**
- Diverse bugfixes en verbeteringen

---

## Versie 3.33

**String-problemen gefixed (crash preventie):**
- `gs_tokenStatusCallback`: String.c_str() vervangen door lokale String variabelen
- Voorkomt crashes door String objecten die buiten scope gaan
- `sscanf` buffer bounds checking toegevoegd in geheugen logging parsing
- `strcat` vervangen door `snprintf` met bounds checking in `updateGUI()`
- String concatenatie in `setup()` vervangen door char arrays met `snprintf`
- `strncpy` buffer overflow check toegevoegd in `showGSSuccessCheckmark()`
- Alle string operaties gebruiken nu veilige char arrays met bounds checking

---

## Versie 3.32

**T_TOP maximum verlaagd, grafiek logging verbeterd, schakellogica fix:**
- Maximum T_top temperatuur verlaagd van 300°C naar 227°C (verwarming maximum)
- Alle validatie checks aangepast (grafiek, logging, instellingen)
- `graph_last_log_time` wordt nu alleen bijgewerkt na succesvolle data opslag
- Voorkomt dat grafiek stopt met loggen als array bounds check faalt
- Extra check toegevoegd voor ongeldige temperatuur om vastlopen te voorkomen
- Grafiek blijft nu continu bijwerken van START tot STOP
- `koelingsfase_actief` check verplaatst naar begin van `cyclusLogica()`
- Veiligheidskoeling check alleen actief als cyclus NIET actief is
- Voorkomt dat systeem op 35°C schakelt in plaats van T_dal tijdens normale cycli
- Extra debug output toegevoegd voor troubleshooting

---

## Versie 3.31

**Verwarming beveiliging reset en veiligheidskoeling fase reset:**
- Automatische reset van verwarming beveiliging tijdens opwarmen
- Elke 4 minuten wordt GPIO 23 gedurende 0,33 seconden laag gezet om beveiliging te resetten
- Voorkomt automatische uitschakeling van verwarming na 4 minuten
- Reset timer wordt automatisch geïnitialiseerd bij start opwarmen fase
- Reset logica werkt alleen tijdens actieve opwarmen fase
- Veiligheidskoeling fase reset functionaliteit toegevoegd

---

## Versie 3.30

**Diverse verbeteringen:**
- Diverse bugfixes en verbeteringen

---

## Versie 3.26

**Simulatie mode uitgeschakeld:**
- Simulatie mode uitgeschakeld - code klaar voor productie met echte MAX6675 sensor

---

## Versie 3.25

**KRITIEKE GEHEUGEN OPTIMALISATIES voor 1000+ cycli stabiliteit:**
- ALLE String operaties vervangen door char arrays (`formatTijd`, `getTimestamp`, event handlers)
- `g_lastGSStatusText` String vervangen door char array
- Statische buffers gebruikt in `formatTijd()` en `getTimestamp()` wrappers
- Verbeterde queue management: verwijder tot 5 oude entries bij overflow
- Uitgebreide geheugen monitoring: free heap, min free, largest block, stack monitoring
- Heap fragmentatie detectie toegevoegd (waarschuwing bij <5KB largest block)
- Queue status monitoring toegevoegd
- Stack monitoring voor logging task toegevoegd
- Alle fase tijd berekeningen gebruiken nu char arrays i.p.v. String
- GEHEUGENSTATISTIEKEN LOGGING: automatische logging naar Google Sheets elke minuut + bij waarschuwingen
- Google Sheets uitgebreid naar 12 kolommen (8 origineel + 4 geheugen kolommen)
- Geheugen data zichtbaar in Google Sheets zonder Serial bus nodig

---

## Versie 3.21

**KRITIEKE FIXES voor crashes na meerdere cycli:**
- Array bounds checking toegevoegd voor alle `graph_temps`/`graph_times` toegangen
- Null pointer checks verbeterd voor alle array en LVGL object toegangen
- String operatie in `update_graph_y_axis_labels()` vervangen door char array
- Watchdog feeding toegevoegd in `update_graph_display()` tijdens lange loops
- Chart updates beperkt tot max 10 punten per update om heap fragmentatie te voorkomen
- Error recovery toegevoegd bij out-of-bounds `graph_index`
- Queue overflow protection: voorkomt crashes bij volle logging queue
- Timeout protection toegevoegd aan Google Sheets operaties (voorkomt infinite loops)

---

## Versie 3.20

**Geheugen optimalisaties:**
- String operaties vervangen door char arrays in `updateGUI()`
- `FirebaseJson` objecten expliciet gescoped voor automatische vrijgave
- Logging task stack size verhoogd naar 16384 bytes
- Memory monitoring toegevoegd met waarschuwingen
- `update_graph_display()` verbeterd met null pointer checks en watchdog feeding

---

## Versie 3.19

**Geheugen optimalisaties en stabiliteit:**
- `updateGUI()`: Replaced all String concatenations with `snprintf()` and char arrays to prevent heap fragmentation
- `formatTijdChar()`: New helper function for time formatting using char arrays
- `logToGoogleSheet_internal()`: `FirebaseJson` objects are now scoped within a block to ensure immediate destruction and memory release. `totaal_cycli` uses a char array
- `loggingTask`: Stack size increased from 8192 to 16384 bytes to accommodate `FirebaseJson` operations
- `loop()`: Added memory monitoring (`ESP.getFreeHeap()`, `ESP.getMinFreeHeap()`) with a warning for low memory
- `update_graph_y_axis_labels()`: Replaced String operation with `snprintf()` and char array
- `add_chart_point_with_trend()`: Added null pointer and array bounds checks
- `log_graph_data()`: Added null pointer and array bounds checks, with error recovery for `graph_index`
- `update_graph_display()`: Added null pointer checks for chart objects and arrays. Added `yield()` calls within loops to feed the watchdog. Limited chart updates to a maximum of 10 points per update to prevent large, single-shot memory allocations

---

## Versie 3.14

**Google logging feedback en grafiek labels:**
- `volatile bool g_logSuccessFlag` en `volatile unsigned long g_logSuccessTime` toegevoegd
- `String g_lastGSStatusText` toegevoegd om de originele status tekst op te slaan
- `showGSStatus()`: Slaat het bericht op in `g_lastGSStatusText`
- `showGSSuccessCheckmark()`: Vervangt "GEREED" met "LOGGING" in `g_lastGSStatusText` en toont het in groen
- `logToGoogleSheet_internal()`: Zet `g_logSuccessFlag = true` en `g_logSuccessTime = millis()` bij succes
- `loop()`: Controleert `g_logSuccessFlag` om `showGSSuccessCheckmark()` aan te roepen, en na 1 seconde wordt de `gs_status_label` tekst teruggezet naar `g_lastGSStatusText` in grijs
- Grafiek labels fine-tuning: label '9' 2px naar links, '8' en '7' ongewijzigd, '6' t/m '1' 2px naar links, '0' 6px naar links
- Grafiek labels verder aangepast: '9' en '2' nog 2px naar links

---

## Versie 3.13

**Grafiek labels en titel:**
- Grafiek titel gewijzigd van "Temperatuur" naar "Temperatuur (graden C - minuten)"
- Minuut labels [9 8 7 6 5 4 3 2 1 0] toegevoegd onder de grafiek, uitgelijnd met verticale divisielijnen
- Labels positionering aangepast: + (i * 4) + 6 (2px links, 4px bredere spacing)
- Individuele label offsets toegevoegd voor fine-tuning

---

## Versie 3.12

**Dynamische grafiek lijn kleur:**
- `lv_chart_series_t * chart_series_rising` (rood) en `* chart_series_falling` (blauw) gedeclareerd
- `lv_create_graph_screen()`: Beide series toegevoegd aan de chart
- `add_chart_point_with_trend()`: Nieuwe helper functie om te bepalen of temperatuur stijgt of daalt vergeleken met het vorige punt en voegt het punt toe aan de juiste serie, waarbij de andere op `LV_CHART_POINT_NONE` wordt gezet
- `update_graph_display()`: Aangepast om `add_chart_point_with_trend()` te gebruiken voor alle punten

---

## Versie 3.11

**Grafiek datapunten verhoogd:**
- `#define GRAPH_POINTS` verhoogd van 100 naar 120

---

## Versie 3.10

**FreeRTOS logging task en Preferences opslag:**
- Google Sheets logging verplaatst naar aparte FreeRTOS task op Core 0 voor betere responsiviteit
- `LogRequest` struct met char arrays voor status en fase tijd
- `logQueue` en `loggingTaskHandle` aangemaakt
- `loggingTask()`: FreeRTOS task die draait op Core 0, ontvangt `LogRequest` van `logQueue` en roept `logToGoogleSheet_internal()` aan. Bevat periodieke `sheet.ready()` voor token refresh
- `logToGoogleSheet()`: Publieke functie om `LogRequest` naar `logQueue` te sturen (non-blocking)
- `logToGoogleSheet_internal()`: Interne functie die daadwerkelijke Google Sheets API calls uitvoert
- `setup()`: Initialiseert `logQueue` en maakt `loggingTask` aan op Core 0
- `loop()`: Directe Google Sheets token refresh verwijderd
- Instellingen opslag in non-volatile memory (Preferences): T_top, T_bottom, cyclus_max
- `loadSettings()`: Leest instellingen uit Preferences met default waarden. Wordt aangeroepen in `setup()`
- `saveSettings()`: Schrijft instellingen naar Preferences. Wordt aangeroepen in event handlers
- Grafiek data collectie start nu direct bij het indrukken van de START knop, niet pas bij het indrukken van de grafiek-knop

---

## Belangrijke technische details

### Hardware
- CYD 2.8 inch display (LVGL graphics, touch bediening, MAX6675 temperatuurmeting)
- GPIO 5/23 Solid State Relais (SSR) besturing (koelen/verwarmen)
- MAX6675 thermocouple sensor (CS=22, SO=35, SCK=27)

### Basis functionaliteit
- Cyclus logica: verwarmen tot Top, koelen tot Dal, automatisch stoppen bij max cycli
- Temperatuur instellingen via +/- knoppen (0-350°C, min 5°C verschil)
- Cycli insteller (0 = oneindig), cyclus teller formaat "xxx/yyy" of "xxx/inf"
- Dynamische Start/Stop knoppen (kleur op basis van systeemstatus)
- Intelligente start modus op basis van huidige temperatuur
- Veiligheidskoeling: koel tot <35°C voordat systeem uitgaat bij STOP

### Display
- GUI update elke 0.3s, temperatuur update elke 2s (gemiddelde van 6-7 metingen)
- Temperatuur kleurcodering: groen<37°C (veilig), rood≥37°C (niet veilig aanraken)
- Opwarmen/afkoelen timers (m:ss formaat)
- Status tekst rechts uitgelijnd, breedte 230px
- Versienummer (v3.65) klein in rechter onderhoek

### Grafiek
- Temperatuur/tijd grafiek (280x155px, 120 datapunten, 6 divisielijnen)
- Automatische logging elke 5s, kleurcodering: rood stijgend, blauw dalend
- Dynamische Y-as labels: 20°C tot T_top (6 labels, verdeeld over bereik)
- Chart range past automatisch aan bij T_top wijziging (20°C - T_top)
- X-as minuten labels (9-0)
- GRAFIEK knop tussen START en STOP, TERUG knop naar hoofdscherm

### Google Sheets logging
- FreeRTOS task op Core 0 voor betere responsiviteit
- Logging bij: START, verwarmen→koelen, koelen→verwarmen, STOP, veiligheidskoeling afgerond
- Tabblad "DataLog-K", 8 kolommen: Timestamp, Meettemperatuur, Status, Huidige cyclus, Totaal cycli, T_top, T_dal, Fase tijd
- Geheugen waarschuwingen zichtbaar op scherm (rechts boven meettemperatuur, rood bij problemen)
- Geheugen waarschuwingen: LOW_HEAP (<20KB), FRAG (<5KB largest block), LOW_STACK (<2KB), QUEUE_FULL
- Timestamp formaat: [yy-mm-dd hh:mm:ss], WiFi en NTP tijd synchronisatie
- Status op hoofdscherm (grijs/rood bij fout)

### Instellingen
- Opslag in non-volatile memory (Preferences): T_top, T_bottom, cyclus_max
- Automatisch laden bij opstarten, opslaan bij wijziging

---

## Notities voor verdere ontwikkeling

- Bij het werken op een andere PC, lees eerst dit CHANGELOG.md bestand om op de hoogte te zijn van alle wijzigingen
- Versienummer staat in `FIRMWARE_VERSION_MAJOR` en `FIRMWARE_VERSION_MINOR` defines
- Bij elke wijziging: update versienummer EN voeg entry toe aan dit CHANGELOG.md bestand
- Code bevat uitgebreide geheugen optimalisaties voor stabiliteit bij 1000+ cycli
- Alle String operaties zijn vervangen door char arrays om heap fragmentatie te voorkomen
- FreeRTOS logging task draait op Core 0 voor betere responsiviteit
- Grafiek gebruikt LVGL SHIFT mode voor automatische sliding window functionaliteit

