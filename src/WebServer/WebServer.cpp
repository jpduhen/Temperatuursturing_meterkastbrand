#include "WebServer.h"
#include "../SettingsStore/SettingsStore.h"
#include "../CycleController/CycleController.h"
#include "../TempSensor/TempSensor.h"
#include "../UIController/UIController.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

ConfigWebServer::ConfigWebServer() 
    : server(80), settingsStore(nullptr), cycleController(nullptr), 
      tempSensor(nullptr), uiController(nullptr),
      startCallback(nullptr), stopCallback(nullptr), settingsChangeCallback(nullptr),
      getCurrentTempCallback(nullptr), getMedianTempCallback(nullptr),
      isActiveCallback(nullptr), isHeatingCallback(nullptr),
      getCycleCountCallback(nullptr), getTtopCallback(nullptr),
      getTbottomCallback(nullptr), getCycleMaxCallback(nullptr),
      getTempOffsetCallback(nullptr) {
}

void ConfigWebServer::begin(int port) {
    server = WebServer(port);
    
    // Route handlers
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/settings", HTTP_GET, [this]() { handleSettings(); });
    server.on("/status", HTTP_GET, [this]() { handleStatus(); });
    server.on("/start", HTTP_POST, [this]() { handleStart(); });
    server.on("/stop", HTTP_POST, [this]() { handleStop(); });
    server.on("/save", HTTP_POST, [this]() { handleSaveSettings(); });
    
    server.begin();
}

void ConfigWebServer::handleClient() {
    server.handleClient();
}

void ConfigWebServer::handleRoot() {
    server.send(200, "text/html", generateHTML());
}

void ConfigWebServer::handleSettings() {
    // Return current settings as JSON (handmatig gegenereerd)
    String response = "{";
    if (getTtopCallback) {
        response += "\"tTop\":" + String(getTtopCallback(), 1);
    } else {
        response += "\"tTop\":80.0";
    }
    if (getTbottomCallback) {
        response += ",\"tBottom\":" + String(getTbottomCallback(), 1);
    } else {
        response += ",\"tBottom\":25.0";
    }
    if (getTempOffsetCallback) {
        response += ",\"tempOffset\":" + String(getTempOffsetCallback(), 1);
    } else {
        response += ",\"tempOffset\":0.0";
    }
    if (getCycleMaxCallback) {
        response += ",\"cycleMax\":" + String(getCycleMaxCallback());
    } else {
        response += ",\"cycleMax\":0";
    }
    response += "}";
    server.send(200, "application/json", response);
}

void ConfigWebServer::handleStatus() {
    server.send(200, "application/json", generateStatusJSON());
}

void ConfigWebServer::handleStart() {
    if (startCallback) {
        startCallback();
        server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Cyclus gestart\"}");
    } else {
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Start callback niet ingesteld\"}");
    }
}

void ConfigWebServer::handleStop() {
    if (stopCallback) {
        stopCallback();
        server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Cyclus gestopt\"}");
    } else {
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Stop callback niet ingesteld\"}");
    }
}

void ConfigWebServer::handleSaveSettings() {
    // Parse JSON handmatig (eenvoudige parser voor deze specifieke use case)
    String body = server.hasArg("plain") ? server.arg("plain") : "";
    if (body.length() == 0) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Geen data ontvangen\"}");
        return;
    }
    
    // Eenvoudige JSON parsing - zoek naar key-value pairs
    float tTop = getTtopCallback ? getTtopCallback() : 80.0;
    float tBottom = getTbottomCallback ? getTbottomCallback() : 25.0;
    float tempOffset = getTempOffsetCallback ? getTempOffsetCallback() : 0.0;
    int cycleMax = getCycleMaxCallback ? getCycleMaxCallback() : 0;
    
    // Parse tTop
    int tTopIdx = body.indexOf("\"tTop\"");
    if (tTopIdx >= 0) {
        int colonIdx = body.indexOf(':', tTopIdx);
        int commaIdx = body.indexOf(',', colonIdx);
        int endIdx = body.indexOf('}', colonIdx);
        if (commaIdx < 0 || commaIdx > endIdx) commaIdx = endIdx;
        if (colonIdx >= 0 && commaIdx > colonIdx) {
            String value = body.substring(colonIdx + 1, commaIdx);
            value.trim();
            tTop = value.toFloat();
        }
    }
    
    // Parse tBottom
    int tBottomIdx = body.indexOf("\"tBottom\"");
    if (tBottomIdx >= 0) {
        int colonIdx = body.indexOf(':', tBottomIdx);
        int commaIdx = body.indexOf(',', colonIdx);
        int endIdx = body.indexOf('}', colonIdx);
        if (commaIdx < 0 || commaIdx > endIdx) commaIdx = endIdx;
        if (colonIdx >= 0 && commaIdx > colonIdx) {
            String value = body.substring(colonIdx + 1, commaIdx);
            value.trim();
            tBottom = value.toFloat();
        }
    }
    
    // Parse tempOffset
    int tempOffsetIdx = body.indexOf("\"tempOffset\"");
    if (tempOffsetIdx >= 0) {
        int colonIdx = body.indexOf(':', tempOffsetIdx);
        int commaIdx = body.indexOf(',', colonIdx);
        int endIdx = body.indexOf('}', colonIdx);
        if (commaIdx < 0 || commaIdx > endIdx) commaIdx = endIdx;
        if (colonIdx >= 0 && commaIdx > colonIdx) {
            String value = body.substring(colonIdx + 1, commaIdx);
            value.trim();
            tempOffset = value.toFloat();
        }
    }
    
    // Parse cycleMax
    int cycleMaxIdx = body.indexOf("\"cycleMax\"");
    if (cycleMaxIdx >= 0) {
        int colonIdx = body.indexOf(':', cycleMaxIdx);
        int commaIdx = body.indexOf(',', colonIdx);
        int endIdx = body.indexOf('}', colonIdx);
        if (commaIdx < 0 || commaIdx > endIdx) commaIdx = endIdx;
        if (colonIdx >= 0 && commaIdx > colonIdx) {
            String value = body.substring(colonIdx + 1, commaIdx);
            value.trim();
            cycleMax = value.toInt();
        }
    }
    
    // Validatie
    if (tTop < tBottom + 5.0) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"T_top moet minstens 5°C hoger zijn dan T_bottom\"}");
        return;
    }
    
    if (tTop > 350.0) tTop = 350.0;
    if (tBottom < 0.0) tBottom = 0.0;
    if (tempOffset < -10.0) tempOffset = -10.0;
    if (tempOffset > 10.0) tempOffset = 10.0;
    if (cycleMax < 0) cycleMax = 0;
    
    // Call callback om instellingen op te slaan
    if (settingsChangeCallback) {
        settingsChangeCallback(tTop, tBottom, tempOffset, cycleMax);
    }
    
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Instellingen opgeslagen\"}");
}

String ConfigWebServer::generateStatusJSON() {
    // Genereer JSON handmatig
    String response = "{";
    
    // Status string
    String statusStr = "Uit";
    if (isActiveCallback && isActiveCallback()) {
        if (isHeatingCallback && isHeatingCallback()) {
            statusStr = "Verwarmen";
        } else {
            statusStr = "Koelen";
        }
    } else {
        statusStr = "Gereed";
    }
    response += "\"status\":\"" + statusStr + "\"";
    
    if (getCurrentTempCallback) {
        float temp = getCurrentTempCallback();
        if (!isnan(temp)) {
            response += ",\"currentTemp\":" + String(temp, 1);
        }
    }
    
    if (getMedianTempCallback) {
        float temp = getMedianTempCallback();
        if (!isnan(temp)) {
            response += ",\"medianTemp\":" + String(temp, 1);
        }
    }
    
    if (isActiveCallback) {
        response += ",\"isActive\":" + String(isActiveCallback() ? "true" : "false");
    }
    
    if (isHeatingCallback) {
        response += ",\"isHeating\":" + String(isHeatingCallback() ? "true" : "false");
    }
    
    if (getCycleCountCallback) {
        response += ",\"cycleCount\":" + String(getCycleCountCallback());
    }
    
    if (getTtopCallback) {
        response += ",\"tTop\":" + String(getTtopCallback(), 1);
    }
    
    if (getTbottomCallback) {
        response += ",\"tBottom\":" + String(getTbottomCallback(), 1);
    }
    
    if (getCycleMaxCallback) {
        response += ",\"cycleMax\":" + String(getCycleMaxCallback());
    }
    
    if (getTempOffsetCallback) {
        response += ",\"tempOffset\":" + String(getTempOffsetCallback(), 1);
    }
    
    response += "}";
    return response;
}

String ConfigWebServer::generateHTML() {
    String html = R"(
<!DOCTYPE html>
<html lang="nl">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Temperatuur Cyclus Controller</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            background: white;
            border-radius: 12px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.2);
            overflow: hidden;
        }
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }
        .header h1 { font-size: 28px; margin-bottom: 10px; }
        .header p { opacity: 0.9; }
        .content { padding: 30px; }
        .section {
            margin-bottom: 30px;
            padding: 20px;
            background: #f8f9fa;
            border-radius: 8px;
        }
        .section h2 {
            color: #333;
            margin-bottom: 20px;
            font-size: 20px;
            border-bottom: 2px solid #667eea;
            padding-bottom: 10px;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 8px;
            color: #555;
            font-weight: 500;
        }
        input[type="number"] {
            width: 100%;
            padding: 12px;
            border: 2px solid #ddd;
            border-radius: 6px;
            font-size: 16px;
            transition: border-color 0.3s;
        }
        input[type="number"]:focus {
            outline: none;
            border-color: #667eea;
        }
        .button-group {
            display: flex;
            gap: 10px;
            margin-top: 20px;
        }
        button {
            flex: 1;
            padding: 14px 24px;
            border: none;
            border-radius: 6px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s;
        }
        .btn-primary {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
        }
        .btn-primary:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4); }
        .btn-success {
            background: #28a745;
            color: white;
        }
        .btn-success:hover { background: #218838; }
        .btn-danger {
            background: #dc3545;
            color: white;
        }
        .btn-danger:hover { background: #c82333; }
        .status {
            padding: 15px;
            background: #e9ecef;
            border-radius: 6px;
            margin-bottom: 20px;
        }
        .status-item {
            display: flex;
            justify-content: space-between;
            padding: 8px 0;
            border-bottom: 1px solid #dee2e6;
        }
        .status-item:last-child { border-bottom: none; }
        .status-label { font-weight: 600; color: #555; }
        .status-value { color: #333; }
        .status-active { color: #28a745; font-weight: 600; }
        .status-inactive { color: #6c757d; }
        .status-heating { color: #dc3545; font-weight: 600; }
        .status-cooling { color: #007bff; font-weight: 600; }
        .message {
            padding: 12px;
            border-radius: 6px;
            margin-bottom: 20px;
            display: none;
        }
        .message-success {
            background: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        .message-error {
            background: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🌡️ Temperatuur Cyclus Controller</h1>
            <p>Versie 3.97</p>
        </div>
        <div class="content">
            <div id="message" class="message"></div>
            
            <div class="section">
                <h2>📊 Status</h2>
                <div id="status" class="status">
                    <div class="status-item">
                        <span class="status-label">Huidige Temperatuur:</span>
                        <span class="status-value" id="currentTemp">--</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Mediaan Temperatuur:</span>
                        <span class="status-value" id="medianTemp">--</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Status:</span>
                        <span class="status-value" id="statusText">--</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Huidige Cyclus:</span>
                        <span class="status-value" id="cycleCount">--</span>
                    </div>
                </div>
            </div>
            
            <div class="section">
                <h2>⚙️ Instellingen</h2>
                <form id="settingsForm">
                    <div class="form-group">
                        <label for="tTop">T_top (°C):</label>
                        <input type="number" id="tTop" name="tTop" step="0.1" min="0" max="350" required>
                    </div>
                    <div class="form-group">
                        <label for="tBottom">T_bottom (°C):</label>
                        <input type="number" id="tBottom" name="tBottom" step="0.1" min="0" max="350" required>
                    </div>
                    <div class="form-group">
                        <label for="tempOffset">Temperatuur Offset (°C):</label>
                        <input type="number" id="tempOffset" name="tempOffset" step="0.1" min="-10" max="10" required>
                    </div>
                    <div class="form-group">
                        <label for="cycleMax">Maximaal Aantal Cycli (0 = oneindig):</label>
                        <input type="number" id="cycleMax" name="cycleMax" min="0" required>
                    </div>
                    <div class="button-group">
                        <button type="submit" class="btn-primary">💾 Opslaan</button>
                    </div>
                </form>
            </div>
            
            <div class="section">
                <h2>🎮 Besturing</h2>
                <div class="button-group">
                    <button type="button" id="startBtn" class="btn-success">▶️ START</button>
                    <button type="button" id="stopBtn" class="btn-danger">⏹️ STOP</button>
                </div>
            </div>
        </div>
    </div>
    
    <script>
        let statusInterval;
        
        function showMessage(text, isError = false) {
            const msg = document.getElementById('message');
            msg.textContent = text;
            msg.className = 'message ' + (isError ? 'message-error' : 'message-success');
            msg.style.display = 'block';
            setTimeout(() => { msg.style.display = 'none'; }, 5000);
        }
        
        function updateStatus() {
            fetch('/status')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('currentTemp').textContent = 
                        data.currentTemp !== undefined ? data.currentTemp.toFixed(1) + '°C' : '--';
                    document.getElementById('medianTemp').textContent = 
                        data.medianTemp !== undefined ? data.medianTemp.toFixed(1) + '°C' : '--';
                    
                    const statusText = document.getElementById('statusText');
                    statusText.textContent = data.status || '--';
                    statusText.className = 'status-value ' + 
                        (data.status === 'Verwarmen' ? 'status-heating' : 
                         data.status === 'Koelen' ? 'status-cooling' : 
                         data.status === 'Gereed' ? 'status-active' : 'status-inactive');
                    
                    document.getElementById('cycleCount').textContent = 
                        data.cycleCount !== undefined ? data.cycleCount : '--';
                })
                .catch(e => console.error('Status update error:', e));
        }
        
        function loadSettings() {
            fetch('/settings')
                .then(r => r.json())
                .then(data => {
                    if (data.tTop !== undefined) document.getElementById('tTop').value = data.tTop;
                    if (data.tBottom !== undefined) document.getElementById('tBottom').value = data.tBottom;
                    if (data.tempOffset !== undefined) document.getElementById('tempOffset').value = data.tempOffset;
                    if (data.cycleMax !== undefined) document.getElementById('cycleMax').value = data.cycleMax;
                })
                .catch(e => console.error('Settings load error:', e));
        }
        
        document.getElementById('settingsForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            const formData = {
                tTop: parseFloat(document.getElementById('tTop').value),
                tBottom: parseFloat(document.getElementById('tBottom').value),
                tempOffset: parseFloat(document.getElementById('tempOffset').value),
                cycleMax: parseInt(document.getElementById('cycleMax').value)
            };
            
            try {
                const response = await fetch('/save', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(formData)
                });
                const result = await response.json();
                if (result.status === 'ok') {
                    showMessage('Instellingen opgeslagen!', false);
                } else {
                    showMessage('Fout: ' + result.message, true);
                }
            } catch (error) {
                showMessage('Fout bij opslaan: ' + error.message, true);
            }
        });
        
        document.getElementById('startBtn').addEventListener('click', async () => {
            try {
                const response = await fetch('/start', { method: 'POST' });
                const result = await response.json();
                showMessage(result.message || 'Cyclus gestart', result.status !== 'ok');
            } catch (error) {
                showMessage('Fout bij starten: ' + error.message, true);
            }
        });
        
        document.getElementById('stopBtn').addEventListener('click', async () => {
            try {
                const response = await fetch('/stop', { method: 'POST' });
                const result = await response.json();
                showMessage(result.message || 'Cyclus gestopt', result.status !== 'ok');
            } catch (error) {
                showMessage('Fout bij stoppen: ' + error.message, true);
            }
        });
        
        // Initial load
        loadSettings();
        updateStatus();
        
        // Auto-refresh status every 2 seconds
        statusInterval = setInterval(updateStatus, 2000);
    </script>
</body>
</html>
)";
    return html;
}

