#include "wifi_manager.h"
#include "globals.h"        // DODANE - potrzebne dla struktury Wifi
#include <DNSServer.h>


const char* wifi_config_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Konfiguracja WiFi - ESP32</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; background: #f5f5f5; }
        .container { max-width: 500px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        input, select { display: block; margin: 10px auto; padding: 10px; width: 90%; max-width: 300px; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }
        button { padding: 10px 20px; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; margin: 5px; font-size: 16px; }
        button:hover { background: #45a049; }
        .reset-btn { background: #f44336; }
        .reset-btn:hover { background: #da190b; }
        .scan-btn { background: #2196F3; }
        .ip-config { margin-top: 10px; padding: 10px; background: #f9f9f9; border-radius: 5px; display: none; }
        .ip-config.show { display: block; }
        .network-list { margin: 20px auto; text-align: left; width: 90%; max-width: 300px; }
        .network-item { padding: 8px; margin: 5px 0; background: #f0f0f0; border-radius: 5px; cursor: pointer; }
        .network-item:hover { background: #e0e0e0; }
    </style>
    <script>
        function toggleIPConfig() {
            var dhcp = document.getElementById('dhcp').value === 'true';
            var ipConfig = document.getElementById('ipConfig');
            if (dhcp) {
                ipConfig.classList.remove('show');
            } else {
                ipConfig.classList.add('show');
            }
        }
        
        function scanNetworks() {
            fetch('/scan')
                .then(response => response.json())
                .then(data => {
                    let list = document.getElementById('networkList');
                    list.innerHTML = '';
                    data.forEach(network => {
                        let div = document.createElement('div');
                        div.className = 'network-item';
                        div.onclick = () => document.getElementById('ssid').value = network.ssid;
                        let encryption = network.enc === 0 ? 'Brak' : 'Zabezpieczone';
                        div.innerHTML = '<strong>' + network.ssid + '</strong><br><small>Sygnał: ' + network.rssi + ' dBm | ' + encryption + '</small>';
                        list.appendChild(div);
                    });
                });
        }
    </script>
</head>
<body>
    <div class="container">
        <h2>🔧 Konfiguracja WiFi</h2>
        
        <form action="/connect" method="POST">
            <input type="text" id="ssid" name="ssid" placeholder="Nazwa WiFi" required value="{SSID}">
            <input type="password" name="pass" placeholder="Hasło" value="{PASS}">
            
            <select id="dhcp" name="dhcp" onchange="toggleIPConfig()">
                <option value="true" {DHCP_SELECTED}>DHCP (automatyczny IP)</option>
                <option value="false" {STATIC_SELECTED}>Statyczny IP</option>
            </select>
            
            <div id="ipConfig" class="ip-config {IP_CONFIG_CLASS}">
                <input type="text" name="ip" placeholder="Statyczne IP" value="{IP}">
                <input type="text" name="gate" placeholder="Gateway" value="{GATE}">
                <input type="text" name="mask" placeholder="Maska" value="{MASK}">
                <input type="text" name="dns" placeholder="DNS" value="{DNS}">
            </div>
            
            <button type="submit">💾 Zapisz i Połącz</button>
        </form>
        
        <button type="button" class="scan-btn" onclick="scanNetworks()">📡 Skanuj sieci WiFi</button>
        <button type="button" class="reset-btn" onclick="fetch('/reset', {method: 'POST'}).then(() => alert('Reset konfiguracji...'))">🗑️ Resetuj ustawienia</button>
        
        <div id="networkList" class="network-list"></div>
    </div>
    
    <script>
        toggleIPConfig();
    </script>
</body>
</html>
)rawliteral";

WiFiManager::WiFiManager(ConfigManager* cfg) : server(80), config(cfg), apMode(false) {}

// ==========  METODA: begin z parametrem forceAP ==========
bool WiFiManager::begin(bool forceAP) {
  // Jeśli wymuszono tryb AP
  if (forceAP) {
    Serial.println("🔘 [WiFiManager] Wymuszony tryb AP (przycisk BOOT)");
    startAPMode();
    return true;
  }
  
  // Normalne uruchomienie (istniejący kod)
  return begin();
}

// ========== ISTNIEJĄCA METODA: begin bez parametrów ==========
bool WiFiManager::begin() {
  WifiConfig* wifiCfg = config->getWifiConfig();
  
  // ===== LOGI DIAGNOSTYCZNE =====
  Serial.println("\n========== DIAGNOSTYKA WiFi ==========");
  Serial.print("SSID: '"); Serial.print(wifiCfg->ssid); Serial.println("'");
  Serial.print("DHCP flag: ");
  if (wifiCfg->dhcp) {
    Serial.println("true (używam DHCP)");
  } else {
    Serial.println("false (używam statycznego IP)");
  }
  Serial.print("IP z config: '"); Serial.print(wifiCfg->ip); Serial.println("'");
  Serial.print("Gate: '"); Serial.print(wifiCfg->gate); Serial.println("'");
  Serial.print("Mask: '"); Serial.print(wifiCfg->mask); Serial.println("'");
  Serial.print("DNS: '"); Serial.print(wifiCfg->dns); Serial.println("'");
  Serial.println("======================================\n");
  
  // Sprawdź czy mamy zapisaną jakąkolwiek konfigurację
  if (strlen(wifiCfg->ssid) == 0) {
    Serial.println("Brak zapisanego SSID - przełączam w tryb AP");
    startAPMode();
    return false;
  }
  
  // Próba połączenia z WiFi
  Serial.print("Próba połączenia z ");
  Serial.println(wifiCfg->ssid);
  
  WiFi.mode(WIFI_STA);
  
  // ===== KONFIGURACJA IP =====
  if (!wifiCfg->dhcp) {
    Serial.println(">> UŻYWAM STATYCZNEGO IP <<");
    if (strlen(wifiCfg->ip) > 0 && strcmp(wifiCfg->ip, "0.0.0.0") != 0) {
      IPAddress localIP, gateway, subnet, dns;
      
      localIP.fromString(wifiCfg->ip);
      gateway.fromString(wifiCfg->gate);
      subnet.fromString(wifiCfg->mask);
      dns.fromString(wifiCfg->dns);
      
      Serial.print("  IP: "); Serial.println(localIP);
      Serial.print("  Gate: "); Serial.println(gateway);
      Serial.print("  Mask: "); Serial.println(subnet);
      Serial.print("  DNS: "); Serial.println(dns);
      
      if (!WiFi.config(localIP, gateway, subnet, dns)) {
        Serial.println("  ❌ Błąd konfiguracji statycznego IP!");
      } else {
        Serial.println("  ✅ Statyczny IP skonfigurowany");
      }
    } else {
      Serial.println("  ⚠️ Brak danych dla statycznego IP - używam DHCP");
      wifiCfg->dhcp = true;  // Wymuś DHCP
    }
  } else {
    Serial.println(">> UŻYWAM DHCP (automatyczny IP) <<");
  }
  
  // Rozpocznij łączenie
  WiFi.begin(wifiCfg->ssid, wifiCfg->pass);
  
  // Próby połączenia
  int attempts = 0;
  const int maxAttempts = 40;
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    esp_task_wdt_reset();  // 🔥 KOPNIJ WATCHDOGA
    delay(500);
    Serial.print(".");
    attempts++;
    
    if (attempts % 10 == 0) {
      Serial.print(" [");
      Serial.print(attempts);
      Serial.print("/");
      Serial.print(maxAttempts);
      Serial.println("]");
    }
  }
  
  // Sprawdź wynik
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi połączone!");
    Serial.print("  IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("  Maska: ");
    Serial.println(WiFi.subnetMask());
    apMode = false;
    return true;
  }
  
  // Nie udało się
  Serial.println("\n❌ Nie udało się połączyć z WiFi!");
  Serial.println("Przełączam w tryb AP...");
  startAPMode();
  return false;
}


void WiFiManager::startAPMode() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Adres IP AP: ");
  Serial.println(IP);
  
  dnsServer.start(53, "*", IP);
  
  server.on("/", std::bind(&WiFiManager::handleRoot, this));
  server.on("/connect", HTTP_POST, std::bind(&WiFiManager::handleConnect, this));
  server.on("/reset", HTTP_POST, std::bind(&WiFiManager::handleReset, this));
  server.on("/scan", HTTP_GET, std::bind(&WiFiManager::handleScan, this));
  
  server.begin();
  Serial.println("Serwer konfiguracyjny uruchomiony");
}

void WiFiManager::handleRoot() {
  String html = wifi_config_html;
  WifiConfig* wifiCfg = config->getWifiConfig();
  
  // Podstaw wartości z konfiguracji
  html.replace("{SSID}", wifiCfg->ssid);
  html.replace("{PASS}", wifiCfg->pass);
  html.replace("{IP}", wifiCfg->ip);
  html.replace("{GATE}", wifiCfg->gate);
  html.replace("{MASK}", wifiCfg->mask);
  html.replace("{DNS}", wifiCfg->dns);
  
  // Ustawienia dla selecta DHCP/Static
  if (wifiCfg->dhcp) {
    html.replace("{DHCP_SELECTED}", "selected");
    html.replace("{STATIC_SELECTED}", "");
    html.replace("{IP_CONFIG_CLASS}", "");
  } else {
    html.replace("{DHCP_SELECTED}", "");
    html.replace("{STATIC_SELECTED}", "selected");
    html.replace("{IP_CONFIG_CLASS}", "show");
  }
  
  server.send(200, "text/html", html);
}

void WiFiManager::handleStatus() {
  DynamicJsonDocument doc(256);
  if (!apMode && WiFi.status() == WL_CONNECTED) {
    doc["connected"] = true;
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
  } else {
    doc["connected"] = false;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void WiFiManager::handleConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  bool dhcp = server.arg("dhcp") == "true";
  
  String ip = "", gate = "", mask = "", dns = "";
  
  if (!dhcp) {
    ip = server.arg("ip");
    gate = server.arg("gate");
    mask = server.arg("mask");
    dns = server.arg("dns");
  }
  
  if (ssid.length() > 0) {
    config->saveWifiConfig(ssid.c_str(), pass.c_str(), 
                           ip.c_str(), gate.c_str(), mask.c_str(), 
                           dns.c_str(), dhcp);
    server.send(200, "text/html", "<h3>✅ Zapisano! Restartowanie...</h3><script>setTimeout(function(){window.location.href='/';},2000);</script>");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "<h3>❌ Błąd: SSID nie może być puste!</h3>");
  }
}

void WiFiManager::handleSetAP() {
  // Przełącz w tryb AP
  apMode = true;
  startAPMode();
  server.send(200, "text/plain", "OK");
}

void WiFiManager::handleSetAPConfig() {
  if (server.hasArg("ap_ssid")) {
    String ssid = server.arg("ap_ssid");
    String pass = server.arg("ap_pass");
    String ip = server.arg("ap_ip");
    String active = server.arg("ap_active");  
    
    config->saveAPConfig(ssid.c_str(), pass.c_str(), ip.c_str(), active == "true");
    server.send(200, "text/html", "<h3>Zapisano! Restart...</h3><script>setTimeout(function(){window.location.href='/';},2000);</script>");
    Serial.println("Zapisano konfigurację AP. Restartowanie...");
    //LOG_PRINT("Zapisano konfigurację AP. Restartowanie...");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Brak parametrów");
  }
}


void WiFiManager::handleReset() {
  config->saveWifiConfig("", "", "", "", "", "");
  server.send(200, "text/html", "<h3>Resetowano! Restart...</h3><script>setTimeout(function(){window.location.href='/';},2000);</script>");
  delay(1000);
  ESP.restart();
}

void WiFiManager::handleScan() {
  String json = "[";
  int n = WiFi.scanComplete();
  if (n == -2) {
    WiFi.scanNetworks(true);
  } else if (n >= 0) {
    for (int i = 0; i < n; ++i) {
      if (i) json += ",";
      json += "{";
      json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
      json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      json += "\"enc\":" + String(WiFi.encryptionType(i));
      json += "}";
    }
    WiFi.scanDelete();
    if (WiFi.scanComplete() == -1) {
      WiFi.scanNetworks(true);
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

void WiFiManager::handle() {
  if (apMode) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
}

bool WiFiManager::isAPMode() {
  return apMode;
}

String WiFiManager::getLocalIP() {
  if (apMode) {
    return WiFi.softAPIP().toString();
  } else {
    return WiFi.localIP().toString();
  }
}

void WiFiManager::reconnect() {
  if (apMode) return;
  
  WifiConfig* wifiCfg = config->getWifiConfig();
  if (strlen(wifiCfg->ssid) > 0) {
    WiFi.begin(wifiCfg->ssid, wifiCfg->pass);
  }
}