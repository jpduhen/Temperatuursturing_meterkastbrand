#include "UIController.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

// Constructor - initialiseer alle pointers naar nullptr
UIController::UIController() 
    : startCallback(nullptr), stopCallback(nullptr), settingChangeCallback(nullptr), graphResetCallback(nullptr),
      isActiveCallback(nullptr), isHeatingCallback(nullptr), isSystemOffCallback(nullptr), isSafetyCoolingCallback(nullptr),
      getCycleCountCallback(nullptr), getHeatingElapsedCallback(nullptr), getCoolingElapsedCallback(nullptr),
      getMedianTempCallback(nullptr), getLastValidTempCallback(nullptr),
      ttopCallback(nullptr), tbottomCallback(nullptr), cyclusMaxCallback(nullptr),
      screen_main(nullptr), text_label_temp(nullptr), text_label_temp_value(nullptr),
      text_label_cyclus(nullptr), text_label_status(nullptr), text_label_t_top(nullptr),
      text_label_t_bottom(nullptr), text_label_verwarmen_tijd(nullptr), text_label_koelen_tijd(nullptr),
      text_label_cyclus_max(nullptr), text_label_version(nullptr), btn_start(nullptr),
      btn_stop(nullptr), btn_graph(nullptr), btn_t_top_plus(nullptr), btn_t_top_minus(nullptr),
      btn_t_bottom_plus(nullptr), btn_t_bottom_minus(nullptr), btn_temp_plus(nullptr),
      btn_temp_minus(nullptr), btn_cyclus_plus(nullptr), btn_cyclus_minus(nullptr),
      init_status_label(nullptr), wifi_status_label(nullptr), gs_status_label(nullptr),
      screen_graph(nullptr), chart(nullptr), chart_series_rising(nullptr), chart_series_falling(nullptr),
      graph_temps(nullptr), graph_times(nullptr), graph_write_index(0), graph_count(0),
      graph_data_ready(false), graph_last_log_time(0), last_graph_update_ms(0),
      graph_force_rebuild(false), draw_buf(nullptr),
      firmwareVersionMajor(3), firmwareVersionMinor(92),
      last_display_ms(0), last_displayed_time(0) {
    // Initialiseer y_axis_labels array
    for (int i = 0; i < 6; i++) {
        y_axis_labels[i] = nullptr;
    }
}

bool UIController::begin(int firmwareVersionMajor, int firmwareVersionMinor) {
    this->firmwareVersionMajor = firmwareVersionMajor;
    this->firmwareVersionMinor = firmwareVersionMinor;
    
    // Alloceer buffers
    if (!allocateBuffers()) {
        return false;
    }
    
    // Initialiseer grafiek data
    initGraphData();
    
    // Maak schermen (wordt later aangeroepen vanuit setup na LVGL init)
    // createMainScreen() en createGraphScreen() worden apart aangeroepen
    
    return true;
}

void UIController::update() {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::logGraphData() {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::onStartButton() {
    if (startCallback) {
        startCallback();
    }
    // Reset grafiek data bij START
    if (graphResetCallback) {
        graphResetCallback();
    }
}

void UIController::onStopButton() {
    if (stopCallback) {
        stopCallback();
    }
}

void UIController::onGraphButton() {
    // Force rebuild bij volgende update
    graph_force_rebuild = true;
    
    // Update Y-as labels
    updateGraphYAxis();
    
    // Schakel naar grafiek scherm
    if (screen_graph != nullptr) {
        lv_scr_load(screen_graph);
    }
    
    // Update grafiek direct
    updateGraph();
}

void UIController::onBackButton() {
    // Schakel terug naar main scherm
    if (screen_main != nullptr) {
        lv_scr_load(screen_main);
    }
}

void UIController::onTtopPlus() {
    if (settingChangeCallback) {
        settingChangeCallback("T_top", 1.0);  // +1.0
    }
}

void UIController::onTtopMinus() {
    if (settingChangeCallback) {
        settingChangeCallback("T_top", -1.0);  // -1.0
    }
}

void UIController::onTbottomPlus() {
    if (settingChangeCallback) {
        settingChangeCallback("T_bottom", 1.0);  // +1.0
    }
}

void UIController::onTbottomMinus() {
    if (settingChangeCallback) {
        settingChangeCallback("T_bottom", -1.0);  // -1.0
    }
}

void UIController::onTempOffsetPlus() {
    if (settingChangeCallback) {
        settingChangeCallback("temp_offset", 0.1);  // +0.1
    }
}

void UIController::onTempOffsetMinus() {
    if (settingChangeCallback) {
        settingChangeCallback("temp_offset", -0.1);  // -0.1
    }
}

void UIController::updateTemperature(float temp, float lastValidTemp) {
    if (text_label_temp_value == nullptr) return;
    
    unsigned long now = millis();
    if (now - last_display_ms >= TEMP_DISPLAY_UPDATE_MS) {
        last_display_ms = now;
        
        char temp_text[16];
        if (!isnan(temp)) {
            snprintf(temp_text, sizeof(temp_text), "%.1f°C", temp);
            lv_label_set_text(text_label_temp_value, temp_text);
            
            // Color coding: groen onder TEMP_SAFE_THRESHOLD, rood vanaf TEMP_SAFE_THRESHOLD
            if (temp < TEMP_SAFE_THRESHOLD) {
                lv_obj_set_style_text_color(text_label_temp_value, lv_color_hex(0x00AA00), 0);
            } else {
                lv_obj_set_style_text_color(text_label_temp_value, lv_color_hex(0xCC0000), 0);
            }
        } else if (!isnan(lastValidTemp)) {
            // Geen nieuwe metingen, maar toon laatste geldige waarde (grijs)
            snprintf(temp_text, sizeof(temp_text), "%.1f°C", lastValidTemp);
            lv_label_set_text(text_label_temp_value, temp_text);
            lv_obj_set_style_text_color(text_label_temp_value, lv_color_hex(0x808080), 0);
        } else {
            // Geen metingen - toon --.--
            lv_label_set_text(text_label_temp_value, "--.--°C");
            lv_obj_set_style_text_color(text_label_temp_value, lv_color_hex(0x808080), 0);
        }
    }
}

void UIController::updateStatus(const char* status) {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::updateCycleCount(int current, int max) {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::updateTimers(unsigned long heating, unsigned long cooling) {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::showInitStatus(const char* message, uint32_t color) {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::hideInitStatus() {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::showWifiStatus(const char* message, bool isError) {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::showGSStatus(const char* message, bool isError) {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::showGSSuccessCheckmark() {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::setButtonsGray() {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::setButtonsNormal() {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::setStartCallback(StartCallback cb) {
    startCallback = cb;
}

void UIController::setStopCallback(StopCallback cb) {
    stopCallback = cb;
}

void UIController::setSettingChangeCallback(SettingChangeCallback cb) {
    settingChangeCallback = cb;
}

void UIController::setGraphResetCallback(GraphResetCallback cb) {
    graphResetCallback = cb;
}

void UIController::setIsActiveCallback(IsActiveCallback cb) {
    isActiveCallback = cb;
}

void UIController::setIsHeatingCallback(IsHeatingCallback cb) {
    isHeatingCallback = cb;
}

void UIController::setIsSystemOffCallback(IsSystemOffCallback cb) {
    isSystemOffCallback = cb;
}

void UIController::setIsSafetyCoolingCallback(IsSafetyCoolingCallback cb) {
    isSafetyCoolingCallback = cb;
}

void UIController::setGetCycleCountCallback(GetCycleCountCallback cb) {
    getCycleCountCallback = cb;
}

void UIController::setGetHeatingElapsedCallback(GetHeatingElapsedCallback cb) {
    getHeatingElapsedCallback = cb;
}

void UIController::setGetCoolingElapsedCallback(GetCoolingElapsedCallback cb) {
    getCoolingElapsedCallback = cb;
}

void UIController::setGetMedianTempCallback(GetMedianTempCallback cb) {
    getMedianTempCallback = cb;
}

void UIController::setGetLastValidTempCallback(GetLastValidTempCallback cb) {
    getLastValidTempCallback = cb;
}

void UIController::setTtopCallback(float (*cb)()) {
    ttopCallback = cb;
}

void UIController::setTbottomCallback(float (*cb)()) {
    tbottomCallback = cb;
}

void UIController::setCyclusMaxCallback(int (*cb)()) {
    cyclusMaxCallback = cb;
}

void UIController::createMainScreen() {
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
    lv_obj_set_width(text_label_status, 230);
    lv_label_set_long_mode(text_label_status, LV_LABEL_LONG_CLIP);
    
    // Regel 2: Grote temperatuur waarde
    text_label_temp_value = lv_label_create(lv_screen_active());
    lv_label_set_text(text_label_temp_value, "--.--°C");
    lv_obj_set_style_text_align(text_label_temp_value, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(text_label_temp_value, LV_ALIGN_TOP_LEFT, 10, 27);
    static lv_style_t style_temp;
    lv_style_init(&style_temp);
    lv_style_set_text_font(&style_temp, &lv_font_montserrat_36);
    lv_obj_add_style(text_label_temp_value, &style_temp, 0);
    
    // Regel 3: Cycli [-] [+] knoppen
    btn_temp_minus = lv_btn_create(lv_screen_active());
    lv_obj_set_size(btn_temp_minus, 25, 25);
    lv_obj_align(btn_temp_minus, LV_ALIGN_TOP_LEFT, 125, 68);
    lv_obj_set_style_bg_color(btn_temp_minus, lv_color_hex(0x0066CC), LV_PART_MAIN);
    lv_obj_t * btn_temp_minus_label = lv_label_create(btn_temp_minus);
    lv_label_set_text(btn_temp_minus_label, "-");
    lv_obj_center(btn_temp_minus_label);
    lv_obj_add_event_cb(btn_temp_minus, temp_minus_event, LV_EVENT_ALL, NULL);
    
    btn_temp_plus = lv_btn_create(lv_screen_active());
    lv_obj_set_size(btn_temp_plus, 25, 25);
    lv_obj_align(btn_temp_plus, LV_ALIGN_TOP_LEFT, 160, 68);
    lv_obj_set_style_bg_color(btn_temp_plus, lv_color_hex(0x0066CC), LV_PART_MAIN);
    lv_obj_t * btn_temp_plus_label = lv_label_create(btn_temp_plus);
    lv_label_set_text(btn_temp_plus_label, "+");
    lv_obj_center(btn_temp_plus_label);
    lv_obj_add_event_cb(btn_temp_plus, temp_plus_event, LV_EVENT_ALL, NULL);
    
    // Cyclus teller (rechts, regel 3)
    text_label_cyclus = lv_label_create(lv_screen_active());
    lv_label_set_text(text_label_cyclus, "Cycli: 1/inf");
    lv_obj_set_style_text_align(text_label_cyclus, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(text_label_cyclus, LV_ALIGN_TOP_RIGHT, -5, 70);
    lv_obj_set_width(text_label_cyclus, 120);
    
    // Regel 4: Top (links) met [-] [+] knoppen, rechts opwarmen timer
    text_label_t_top = lv_label_create(lv_screen_active());
    lv_label_set_text(text_label_t_top, "Top: 80.0°C");
    lv_obj_set_style_text_align(text_label_t_top, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(text_label_t_top, LV_ALIGN_TOP_LEFT, 10, 100);
    lv_obj_set_width(text_label_t_top, 120);
    
    btn_t_top_minus = lv_btn_create(lv_screen_active());
    lv_obj_set_size(btn_t_top_minus, 25, 25);
    lv_obj_align(btn_t_top_minus, LV_ALIGN_TOP_LEFT, 125, 98);
    lv_obj_set_style_bg_color(btn_t_top_minus, lv_color_hex(0x0066CC), LV_PART_MAIN);
    lv_obj_t * btn_t_top_minus_label = lv_label_create(btn_t_top_minus);
    lv_label_set_text(btn_t_top_minus_label, "-");
    lv_obj_center(btn_t_top_minus_label);
    lv_obj_add_event_cb(btn_t_top_minus, t_top_minus_event, LV_EVENT_ALL, NULL);
    
    btn_t_top_plus = lv_btn_create(lv_screen_active());
    lv_obj_set_size(btn_t_top_plus, 25, 25);
    lv_obj_align(btn_t_top_plus, LV_ALIGN_TOP_LEFT, 160, 98);
    lv_obj_set_style_bg_color(btn_t_top_plus, lv_color_hex(0x0066CC), LV_PART_MAIN);
    lv_obj_t * btn_t_top_plus_label = lv_label_create(btn_t_top_plus);
    lv_label_set_text(btn_t_top_plus_label, "+");
    lv_obj_center(btn_t_top_plus_label);
    lv_obj_add_event_cb(btn_t_top_plus, t_top_plus_event, LV_EVENT_ALL, NULL);
    
    text_label_verwarmen_tijd = lv_label_create(lv_screen_active());
    lv_label_set_text(text_label_verwarmen_tijd, "opwarmen: 0:00");
    lv_obj_set_style_text_align(text_label_verwarmen_tijd, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(text_label_verwarmen_tijd, LV_ALIGN_TOP_RIGHT, -5, 100);
    lv_obj_set_width(text_label_verwarmen_tijd, 140);
    
    // Regel 5: Dal (links) met [-] [+] knoppen, rechts afkoelen timer
    text_label_t_bottom = lv_label_create(lv_screen_active());
    lv_label_set_text(text_label_t_bottom, "Dal: 25.0°C");
    lv_obj_set_style_text_align(text_label_t_bottom, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(text_label_t_bottom, LV_ALIGN_TOP_LEFT, 10, 130);
    lv_obj_set_width(text_label_t_bottom, 120);
    
    btn_t_bottom_minus = lv_btn_create(lv_screen_active());
    lv_obj_set_size(btn_t_bottom_minus, 25, 25);
    lv_obj_align(btn_t_bottom_minus, LV_ALIGN_TOP_LEFT, 125, 128);
    lv_obj_set_style_bg_color(btn_t_bottom_minus, lv_color_hex(0x0066CC), LV_PART_MAIN);
    lv_obj_t * btn_t_bottom_minus_label = lv_label_create(btn_t_bottom_minus);
    lv_label_set_text(btn_t_bottom_minus_label, "-");
    lv_obj_center(btn_t_bottom_minus_label);
    lv_obj_add_event_cb(btn_t_bottom_minus, t_bottom_minus_event, LV_EVENT_ALL, NULL);
    
    btn_t_bottom_plus = lv_btn_create(lv_screen_active());
    lv_obj_set_size(btn_t_bottom_plus, 25, 25);
    lv_obj_align(btn_t_bottom_plus, LV_ALIGN_TOP_LEFT, 160, 128);
    lv_obj_set_style_bg_color(btn_t_bottom_plus, lv_color_hex(0x0066CC), LV_PART_MAIN);
    lv_obj_t * btn_t_bottom_plus_label = lv_label_create(btn_t_bottom_plus);
    lv_label_set_text(btn_t_bottom_plus_label, "+");
    lv_obj_center(btn_t_bottom_plus_label);
    lv_obj_add_event_cb(btn_t_bottom_plus, t_bottom_plus_event, LV_EVENT_ALL, NULL);
    
    text_label_koelen_tijd = lv_label_create(lv_screen_active());
    lv_label_set_text(text_label_koelen_tijd, "afkoelen: 0:00");
    lv_obj_set_style_text_align(text_label_koelen_tijd, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(text_label_koelen_tijd, LV_ALIGN_TOP_RIGHT, -5, 130);
    lv_obj_set_width(text_label_koelen_tijd, 140);
    
    // Status regels
    init_status_label = lv_label_create(lv_screen_active());
    lv_label_set_text(init_status_label, "WiFi initialiseren");
    lv_obj_set_style_text_align(init_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(init_status_label, LV_ALIGN_TOP_LEFT, 10, 167);
    lv_obj_set_style_text_color(init_status_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_font(init_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    
    wifi_status_label = lv_label_create(lv_screen_active());
    lv_label_set_text(wifi_status_label, "");
    lv_obj_set_style_text_align(wifi_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(wifi_status_label, LV_ALIGN_BOTTOM_LEFT, 10, -65);
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    
    gs_status_label = lv_label_create(lv_screen_active());
    lv_label_set_text(gs_status_label, "");
    lv_obj_set_style_text_align(gs_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(gs_status_label, LV_ALIGN_BOTTOM_LEFT, 10, -50);
    lv_obj_set_style_text_color(gs_status_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(gs_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    
    // Knoppen
    btn_start = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_start, 80, 40);
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x00AA00), LV_PART_MAIN);
    lv_obj_t * btn_start_label = lv_label_create(btn_start);
    lv_label_set_text(btn_start_label, "START");
    lv_obj_center(btn_start_label);
    lv_obj_add_event_cb(btn_start, start_button_event, LV_EVENT_ALL, NULL);
    
    btn_graph = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_graph, 100, 40);
    lv_obj_align(btn_graph, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(btn_graph, lv_color_hex(0x0066CC), LV_PART_MAIN);
    lv_obj_t * btn_graph_label = lv_label_create(btn_graph);
    lv_label_set_text(btn_graph_label, "GRAFIEK");
    lv_obj_center(btn_graph_label);
    lv_obj_add_event_cb(btn_graph, graph_button_event, LV_EVENT_ALL, NULL);
    
    btn_stop = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn_stop, 80, 40);
    lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_obj_t * btn_stop_label = lv_label_create(btn_stop);
    lv_label_set_text(btn_stop_label, "STOP");
    lv_obj_center(btn_stop_label);
    lv_obj_add_event_cb(btn_stop, stop_button_event, LV_EVENT_ALL, NULL);
    
    // Versienummer label
    text_label_version = lv_label_create(lv_scr_act());
    char version_str[16];
    snprintf(version_str, sizeof(version_str), "v%d.%02d", firmwareVersionMajor, firmwareVersionMinor);
    lv_label_set_text(text_label_version, version_str);
    lv_obj_set_style_text_color(text_label_version, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_opa(text_label_version, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_text_font(text_label_version, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(text_label_version, LV_ALIGN_BOTTOM_RIGHT, -10, -55);
    
    // Maak grafiek scherm
    createGraphScreen();
}

void UIController::createGraphScreen() {
    // Placeholder - wordt later volledig geïmplementeerd
    // Voor nu: basis structuur
    screen_graph = lv_obj_create(NULL);
    lv_obj_clear_flag(screen_graph, LV_OBJ_FLAG_SCROLLABLE);
    
    // Titel label
    lv_obj_t * title_label = lv_label_create(screen_graph);
    lv_label_set_text(title_label, "Temperatuur (graden C - minuten)");
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 2);
    
    // Chart maken
    chart = lv_chart_create(screen_graph);
    lv_obj_set_size(chart, 280, 155);
    lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 35, 18);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, GRAPH_POINTS);
    lv_chart_set_axis_min_value(chart, LV_CHART_AXIS_PRIMARY_Y, 20);
    lv_chart_set_axis_max_value(chart, LV_CHART_AXIS_PRIMARY_Y, 120);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 6, 10);
    
    // Y-as labels
    for (int i = 0; i <= 5; i++) {
        y_axis_labels[i] = lv_label_create(screen_graph);
        lv_obj_set_style_text_align(y_axis_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
        int y_pos = 18 + (i * 155 / 5) - 5 - (i * 4) + 10 - 2;
        lv_obj_align(y_axis_labels[i], LV_ALIGN_TOP_LEFT, 10, y_pos);
    }
    // Initialiseer labels (tijdelijk - wordt later volledig geïmplementeerd)
    updateGraphYAxis();
    
    // Series toevoegen
    chart_series_rising = lv_chart_add_series(chart, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y);
    chart_series_falling = lv_chart_add_series(chart, lv_color_hex(0x0066CC), LV_CHART_AXIS_PRIMARY_Y);
    
    // X-as labels (vereenvoudigd - volledige implementatie later)
    int chart_x_start = 35;
    int chart_width = 280;
    int chart_y_start = 18;
    int chart_height = 155;
    int num_vertical_divs = 10;
    int num_labels = 10;
    
    for (int i = 0; i < num_labels; i++) {
        lv_obj_t * x_value_label = lv_label_create(screen_graph);
        int minute_value = 9 - i;
        char label_text[8];
        snprintf(label_text, sizeof(label_text), "%d", minute_value);
        lv_label_set_text(x_value_label, label_text);
        lv_obj_set_style_text_align(x_value_label, LV_TEXT_ALIGN_CENTER, 0);
        
        int base_x_pos = chart_x_start + (i * chart_width / (num_vertical_divs + 1)) + (i * 4) + 6;
        int offset = 0;
        if (minute_value == 9) {
            offset = 0;
        } else if (minute_value == 8 || minute_value == 7) {
            offset = 0;
        } else if (minute_value == 2) {
            offset = -4;
        } else if (minute_value >= 3 && minute_value <= 6) {
            offset = -2;
        } else if (minute_value == 1) {
            offset = -2;
        } else if (minute_value == 0) {
            offset = -6;
        }
        
        int x_pos = base_x_pos + offset;
        int y_pos = chart_y_start + chart_height + 3;
        lv_obj_align(x_value_label, LV_ALIGN_TOP_LEFT, x_pos, y_pos);
    }
    
    // Terug knop
    lv_obj_t * btn_back = lv_btn_create(screen_graph);
    lv_obj_set_size(btn_back, 100, 40);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x0066CC), LV_PART_MAIN);
    lv_obj_t * btn_back_label = lv_label_create(btn_back);
    lv_label_set_text(btn_back_label, "TERUG");
    lv_obj_center(btn_back_label);
    lv_obj_add_event_cb(btn_back, back_button_event, LV_EVENT_ALL, NULL);
}

void UIController::updateGraph() {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::fillChart() {
    // Placeholder - wordt later geïmplementeerd
}

void UIController::clearChart() {
    // Placeholder - wordt later geïmplementeerd
}

bool UIController::allocateBuffers() {
    // Bereken draw_buf grootte
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

void UIController::initGraphData() {
    if (graph_temps == nullptr || graph_times == nullptr) {
        return;
    }
    for (int i = 0; i < GRAPH_POINTS; i++) {
        graph_temps[i] = NAN;
        graph_times[i] = 0;
    }
    graph_write_index = 0;
    graph_count = 0;
    graph_data_ready = false;
    graph_last_log_time = 0;
    graph_force_rebuild = false;
    last_graph_update_ms = 0;
}

void UIController::formatTijdChar(unsigned long ms, char* buffer, size_t bufferSize) {
    unsigned long seconden = ms / 1000;
    unsigned long minuten = seconden / 60;
    unsigned long sec = seconden % 60;
    snprintf(buffer, bufferSize, "%lu:%02lu", minuten, sec);
}

void UIController::updateGraphYAxis() {
    if (chart == nullptr) return;
    
    // Haal T_top op via callback
    float max_temp = ttopCallback ? ttopCallback() : 100.0;
    float min_temp = 20.0;
    float range = max_temp - min_temp;
    
    // Update chart range
    lv_chart_set_axis_min_value(chart, LV_CHART_AXIS_PRIMARY_Y, (int)min_temp);
    lv_chart_set_axis_max_value(chart, LV_CHART_AXIS_PRIMARY_Y, (int)max_temp);
    
    // Update alle 6 labels
    char label_text[16];
    for (int i = 0; i <= 5; i++) {
        if (y_axis_labels[i] == nullptr) continue;
        float temp_value = max_temp - (i * range / 5.0);
        int temp_int = (int)(temp_value + 0.5);
        snprintf(label_text, sizeof(label_text), "%d", temp_int);
        lv_label_set_text(y_axis_labels[i], label_text);
    }
}
