#ifndef UICONTROLLER_H
#define UICONTROLLER_H

#include <lvgl.h>
#include <stdint.h>
#include <math.h>  // Voor NAN

// Forward declarations voor externe functies
// Deze worden later vervangen door callbacks
// lv_event_t is al gedefinieerd in lvgl.h, dus geen forward declaration nodig
void start_button_event(lv_event_t * e);
void stop_button_event(lv_event_t * e);
void graph_button_event(lv_event_t * e);
void back_button_event(lv_event_t * e);
void t_top_plus_event(lv_event_t * e);
void t_top_minus_event(lv_event_t * e);
void t_bottom_plus_event(lv_event_t * e);
void t_bottom_minus_event(lv_event_t * e);
void temp_plus_event(lv_event_t * e);
void temp_minus_event(lv_event_t * e);
void update_graph_y_axis_labels(void);

class UIController {
public:
    UIController();
    bool begin(int firmwareVersionMajor = 3, int firmwareVersionMinor = 94);
    void update();  // Aanroepen vanuit loop()
    void logGraphData();  // Aanroepen vanuit loop()
    
    // Event handlers
    void onStartButton();
    void onStopButton();
    void onGraphButton();
    void onBackButton();
    void onTtopPlus();
    void onTtopMinus();
    void onTbottomPlus();
    void onTbottomMinus();
    void onTempOffsetPlus();
    void onTempOffsetMinus();
    
    // Status updates
    void updateTemperature(float temp, float lastValidTemp);
    void updateStatus(const char* status);
    void updateCycleCount(int current, int max);
    void updateTimers(unsigned long heating, unsigned long cooling);
    void updateSettings(float tTop, float tBottom);  // Voor T_top en T_bottom labels
    void showInitStatus(const char* message, uint32_t color);
    void hideInitStatus();
    void showWifiStatus(const char* message, bool isError);
    void showGSStatus(const char* message, bool isError);
    void showGSSuccessCheckmark();
    void setButtonsGray();
    void setButtonsNormal();
    
    // Scherm creatie (voor setup)
    void createMainScreen();
    void createGraphScreen();
    
    // Grafiek updates (voor loop)
    void updateGraph();
    
    // Getters voor externe toegang (voor LVGL initialisatie en backward compatibility)
    uint32_t* getDrawBuf() const { return draw_buf; }
    lv_obj_t* getMainScreen() const { return screen_main; }
    lv_obj_t* getGraphScreen() const { return screen_graph; }
    lv_obj_t* getChart() const { return chart; }
    lv_chart_series_t* getChartSeriesRising() const { return chart_series_rising; }
    lv_chart_series_t* getChartSeriesFalling() const { return chart_series_falling; }
    lv_obj_t* getYAxisLabel(int index) const { return (index >= 0 && index < 6) ? y_axis_labels[index] : nullptr; }
    float* getGraphTemps() const { return graph_temps; }
    unsigned long* getGraphTimes() const { return graph_times; }
    int getGraphWriteIndex() const { return graph_write_index; }
    int getGraphCount() const { return graph_count; }
    bool isGraphDataReady() const { return graph_data_ready; }
    unsigned long getGraphLastLogTime() const { return graph_last_log_time; }
    bool isGraphForceRebuild() const { return graph_force_rebuild; }
    void setGraphForceRebuild(bool value) { graph_force_rebuild = value; }
    void setGraphLastLogTime(unsigned long time) { graph_last_log_time = time; }
    void setGraphWriteIndex(int index) { graph_write_index = index; }
    void setGraphCount(int count) { graph_count = count; }
    void setGraphDataReady(bool ready) { graph_data_ready = ready; }
    
    // Callbacks
    typedef void (*StartCallback)();
    typedef void (*StopCallback)();
    typedef void (*SettingChangeCallback)(const char* setting, float value);
    typedef void (*GraphResetCallback)();  // Voor grafiek reset bij START
    typedef bool (*IsActiveCallback)();  // Voor cyclus status checks
    typedef bool (*IsHeatingCallback)();  // Voor verwarmen status
    typedef bool (*IsSystemOffCallback)();  // Voor systeem uit status
    typedef bool (*IsSafetyCoolingCallback)();  // Voor veiligheidskoeling status
    typedef int (*GetCycleCountCallback)();  // Voor cyclus teller
    typedef unsigned long (*GetHeatingElapsedCallback)();  // Voor verwarmen timer
    typedef unsigned long (*GetCoolingElapsedCallback)();  // Voor koelen timer
    typedef float (*GetMedianTempCallback)();  // Voor grafiek data logging
    typedef float (*GetLastValidTempCallback)();  // Voor temperatuur display
    void setStartCallback(StartCallback cb);
    void setStopCallback(StopCallback cb);
    void setSettingChangeCallback(SettingChangeCallback cb);
    void setGraphResetCallback(GraphResetCallback cb);
    void setIsActiveCallback(IsActiveCallback cb);
    void setIsHeatingCallback(IsHeatingCallback cb);
    void setIsSystemOffCallback(IsSystemOffCallback cb);
    void setIsSafetyCoolingCallback(IsSafetyCoolingCallback cb);
    void setGetCycleCountCallback(GetCycleCountCallback cb);
    void setGetHeatingElapsedCallback(GetHeatingElapsedCallback cb);
    void setGetCoolingElapsedCallback(GetCoolingElapsedCallback cb);
    void setGetMedianTempCallback(GetMedianTempCallback cb);
    void setGetLastValidTempCallback(GetLastValidTempCallback cb);
    void setTtopCallback(float (*cb)());  // Voor T_top waarde
    void setTbottomCallback(float (*cb)());  // Voor T_bottom waarde
    void setCyclusMaxCallback(int (*cb)());  // Voor cyclus_max waarde

private:
    void fillChart();
    void clearChart();
    void addChartPoint(int index, float tempValue, int chartValue);
    bool allocateBuffers();
    void initGraphData();
    void updateGraphYAxis();  // Voor update_graph_y_axis_labels()
    
    // Helper functies voor updates (nodig voor externe toegang)
    void formatTijdChar(unsigned long ms, char* buffer, size_t bufferSize);
    
    // Constants
    static constexpr int GRAPH_POINTS = 120;
    static constexpr int SCREEN_WIDTH = 240;
    static constexpr int SCREEN_HEIGHT = 320;
    static constexpr float TEMP_SAFE_THRESHOLD = 37.0f;
    static constexpr unsigned long TEMP_DISPLAY_UPDATE_MS = 285;
    static constexpr unsigned long TEMP_GRAPH_LOG_INTERVAL_MS = 5000;
    static constexpr unsigned long GUI_UPDATE_INTERVAL_MS = 300;
    
    // Callback variabelen
    StartCallback startCallback;
    StopCallback stopCallback;
    SettingChangeCallback settingChangeCallback;
    GraphResetCallback graphResetCallback;
    IsActiveCallback isActiveCallback;
    IsHeatingCallback isHeatingCallback;
    IsSystemOffCallback isSystemOffCallback;
    IsSafetyCoolingCallback isSafetyCoolingCallback;
    GetCycleCountCallback getCycleCountCallback;
    GetHeatingElapsedCallback getHeatingElapsedCallback;
    GetCoolingElapsedCallback getCoolingElapsedCallback;
    GetMedianTempCallback getMedianTempCallback;
    GetLastValidTempCallback getLastValidTempCallback;
    float (*ttopCallback)();
    float (*tbottomCallback)();
    int (*cyclusMaxCallback)();
    
    // LVGL objecten - Main screen
    lv_obj_t* screen_main;
    lv_obj_t* text_label_temp;
    lv_obj_t* text_label_temp_value;
    lv_obj_t* text_label_cyclus;
    lv_obj_t* text_label_status;
    lv_obj_t* text_label_t_top;
    lv_obj_t* text_label_t_bottom;
    lv_obj_t* text_label_verwarmen_tijd;
    lv_obj_t* text_label_koelen_tijd;
    lv_obj_t* text_label_cyclus_max;
    lv_obj_t* text_label_version;
    lv_obj_t* btn_start;
    lv_obj_t* btn_stop;
    lv_obj_t* btn_graph;
    lv_obj_t* btn_t_top_plus;
    lv_obj_t* btn_t_top_minus;
    lv_obj_t* btn_t_bottom_plus;
    lv_obj_t* btn_t_bottom_minus;
    lv_obj_t* btn_temp_plus;
    lv_obj_t* btn_temp_minus;
    lv_obj_t* btn_cyclus_plus;
    lv_obj_t* btn_cyclus_minus;
    lv_obj_t* init_status_label;
    lv_obj_t* wifi_status_label;
    lv_obj_t* gs_status_label;
    
    // LVGL objecten - Graph screen
    lv_obj_t* screen_graph;
    lv_obj_t* chart;
    lv_chart_series_t* chart_series_rising;
    lv_chart_series_t* chart_series_falling;
    lv_obj_t* y_axis_labels[6];
    
    // Grafiek data
    float* graph_temps;
    unsigned long* graph_times;
    int graph_write_index;
    int graph_count;
    bool graph_data_ready;
    unsigned long graph_last_log_time;
    unsigned long last_graph_update_ms;
    bool graph_force_rebuild;
    
    // Status tekst opslag (voor showGSSuccessCheckmark)
    char last_gs_status_text[128];
    
    // Display buffer (voor LVGL)
    uint32_t* draw_buf;
    
    // Versie nummers (voor versie label)
    int firmwareVersionMajor;
    int firmwareVersionMinor;
    
    // Timers voor updates
    unsigned long last_display_ms;
    unsigned long last_displayed_time;  // Voor grafiek updates
};

#endif // UICONTROLLER_H

