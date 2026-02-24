#include "config.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <IPAddress.h>

IPAddress apIP(192, 168, 1, 1);
IPAddress apGW(192, 168, 1, 1);
IPAddress apSN(255, 255, 255, 0);

// ================= WIFI AP =================
#define AP_SSID "CopperGuard_Config"
#define AP_PASS "12345678"

#define EEPROM_SIZE 512
#define MAGIC_KEY 0xA5
#define CONFIG_VERSION 0x03

ESP8266WebServer server(80);
ConfigData config;

// ================= DEFAULTS =================
static void setFactoryDefaults() {
  memset(&config, 0, sizeof(ConfigData));

  // Keep defaults short and safe for buffers
  strcpy(config.deviceName, "CopperGuard");
  strcpy(config.location, "Colombo 01");

  // Default numbers (edit as needed)
  strcpy(config.statusNumbers[0], "+94704775855");
  strcpy(config.emergencyNumbers[0], "+94704775855");

  // Status interval (minutes)
  config.statusIntervalMinutes = 60;

  for (int i = 0; i < 4; i++) {
    config.lineEnable[i] = true;
    config.lineName[i][0] = '\0';   // EMPTY
  }
}

// ================= LOAD CONFIG =================
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);

  const uint8_t magic = EEPROM.read(0);
  const uint8_t ver   = EEPROM.read(1);

  if (magic != MAGIC_KEY || ver != CONFIG_VERSION) {
    // -------- FACTORY DEFAULTS --------
    setFactoryDefaults();
    saveConfig();
  } else {
    // -------- LOAD VALID CONFIG --------
    EEPROM.get(2, config);

    // Basic sanity (avoids weird 0xFF issues if EEPROM got corrupted)
    if (config.statusIntervalMinutes == 0 || config.statusIntervalMinutes > 1440) {
      config.statusIntervalMinutes = 60;
    }
  }
}

// ================= SAVE CONFIG =================
void saveConfig() {
  EEPROM.write(0, MAGIC_KEY);
  EEPROM.write(1, CONFIG_VERSION);
  EEPROM.put(2, config);
  EEPROM.commit();
}

// ================= WEB UI =================
static String pageHTML() {
  String h;
  h += "<!DOCTYPE html><html><head>";
  h += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  h += "<style>";

  /* ===== GLOBAL ===== */
  h += "body{font-family:system-ui,Segoe UI,Roboto,Arial;"
       "background:radial-gradient(circle at top,#0f2027,#05080d);"
       "margin:0;padding:18px;color:#e6f1ff;}";

  /* ===== CARD ===== */
  h += ".card{background:rgba(255,255,255,0.06);"
       "backdrop-filter:blur(10px);"
       "border-radius:16px;padding:18px;margin-bottom:18px;"
       "box-shadow:0 0 20px rgba(0,200,255,0.08);"
       "border:1px solid rgba(0,200,255,0.15);}";

  /* ===== TITLE ===== */
  h += ".title{text-align:center;margin-bottom:18px;}";
  h += ".title h1{margin:0;font-size:26px;"
       "letter-spacing:1px;color:#7dd3fc;"
       "text-shadow:0 0 12px rgba(0,200,255,0.6);}";
  h += ".title p{margin-top:6px;font-size:13px;"
       "color:#a5c9e8;letter-spacing:0.5px;}";

  h += "h2{margin:0 0 14px 0;font-size:18px;color:#cfe9ff;}";

  h += "label{font-size:13px;color:#9bbcd6;}";

  /* ===== INPUTS ===== */
  h += "input[type=text],input[type=number]{width:100%;padding:11px;margin-top:6px;margin-bottom:12px;"
       "border-radius:12px;border:1px solid rgba(0,200,255,0.3);"
       "background:rgba(0,0,0,0.4);color:#e6f1ff;font-size:14px;"
       "outline:none;}";

  h += "input[type=text]::placeholder,input[type=number]::placeholder{color:#7aa3c2;}";

  h += ".small-note{font-size:12px;color:#a5c9e8;margin-top:-8px;margin-bottom:14px;}";

  /* ===== LINE CARD ===== */
  h += ".line-card{display:flex;align-items:center;gap:12px;"
       "padding:12px;border-radius:14px;"
       "background:rgba(0,0,0,0.35);"
       "border:1px solid rgba(0,200,255,0.2);"
       "margin-bottom:12px;}";

  h += ".line-label{width:40px;font-weight:700;font-size:15px;"
       "color:#7dd3fc;text-shadow:0 0 6px rgba(0,200,255,0.5);}";

  h += ".line-enable{display:flex;align-items:center;gap:6px;font-size:13px;color:#cfe9ff;}";
  h += ".line-enable input{transform:scale(1.2);}";

  h += ".line-name{flex:1;padding:9px;border-radius:10px;"
       "border:1px solid rgba(0,200,255,0.3);"
       "background:rgba(0,0,0,0.45);"
       "color:#e6f1ff;font-size:13px;}";

  /* ===== BUTTON ===== */
  h += "button{width:100%;padding:15px;border:none;border-radius:14px;"
       "background:linear-gradient(135deg,#00c6ff,#0072ff);"
       "color:#fff;font-size:16px;font-weight:700;"
       "letter-spacing:0.6px;"
       "box-shadow:0 0 18px rgba(0,200,255,0.5);}";

  h += "</style></head><body>";

  /* ===== HEADER ===== */
  h += "<div class='title'>";
  h += "<h1>Copper Guard</h1>";
  h += "<p>SLT Digital Lab Innovation Center</p>";
  h += "</div>";

  /* ===== DEVICE INFO ===== */
  h += "<div class='card'><h2>Device Configuration</h2>";
  h += "<form action='/save'>";

  h += "<label>Device Name</label>";
  h += "<input type='text' name='dn' value='" + String(config.deviceName) + "'>";

  h += "<label>Location</label>";
  h += "<input type='text' name='loc' value='" + String(config.location) + "'>";

  h += "<label>Status Recipients</label>";
  for (int i = 0; i < 5; i++) {
    h += "<input type='text' name='st" + String(i) + "' placeholder='Phone " + String(i + 1) +
         " (e.g., +9477xxxxxxx)' value='" + String(config.statusNumbers[i]) + "'>";
  }

  h += "<label>Emergency Recipients</label>";
  for (int i = 0; i < 5; i++) {
    h += "<input type='text' name='em" + String(i) + "' placeholder='Phone " + String(i + 1) +
         " (e.g., +9477xxxxxxx)' value='" + String(config.emergencyNumbers[i]) + "'>";
  }
  h += "<div class='small-note'>Emergency recipients receive CUT alerts (SMS + call). Status recipients receive boot + periodic status updates.</div>";

  h += "<label>Status Interval (minutes)</label>";
  h += "<input type='number' name='stint' min='1' max='1440' value='" + String(config.statusIntervalMinutes) + "'>";
  h += "<div class='small-note'>Example: 60 = every hour, 15 = every 15 minutes.</div>";

  h += "</div>";

  /* ===== LINES ===== */
  h += "<div class='card'><h2>Monitoring Lines</h2>";

  for (int i = 0; i < 4; i++) {
    String ln = String(config.lineName[i]);
    ln.trim();
    if (ln.length() == 0 || ln.indexOf('\xFF') >= 0) ln = "";

    h += "<div class='line-card'>";

    h += "<div class='line-label'>L" + String(i + 1) + "</div>";

    h += "<div class='line-enable'>";
    h += "<input type='checkbox' name='le" + String(i) + "'";
    if (config.lineEnable[i]) h += " checked";
    h += "><span>Enable</span></div>";

    h += "<input class='line-name' type='text' name='ln" + String(i) +
         "' placeholder='Line name (optional)' value='" +
          ln + "'>";

    h += "</div>";
  }

  h += "</div>";

  /* ===== SAVE ===== */
  h += "<button type='submit'>SAVE CONFIGURATION</button>";
  h += "</form></body></html>";

  return h;
}

static void handleRoot() {
  server.send(200, "text/html", pageHTML());
}

static void handleSave() {
  if (server.hasArg("dn"))   server.arg("dn").toCharArray(config.deviceName, sizeof(config.deviceName));
  if (server.hasArg("loc"))  server.arg("loc").toCharArray(config.location, sizeof(config.location));

  // Status recipients
  for (int i = 0; i < 5; i++) {
    String key = "st" + String(i);
    if (server.hasArg(key)) {
      server.arg(key).toCharArray(config.statusNumbers[i], 16);
    }
  }

  // Emergency recipients
  for (int i = 0; i < 5; i++) {
    String key = "em" + String(i);
    if (server.hasArg(key)) {
      server.arg(key).toCharArray(config.emergencyNumbers[i], 16);
    }
  }

  // Interval
  if (server.hasArg("stint")) {
    long v = server.arg("stint").toInt();
    if (v < 1) v = 1;
    if (v > 1440) v = 1440;
    config.statusIntervalMinutes = (uint16_t)v;
  }

  for (int i = 0; i < 4; i++) {
    config.lineEnable[i] = server.hasArg("le" + String(i));

    if (server.hasArg("ln" + String(i))) {
      server.arg("ln" + String(i)).toCharArray(config.lineName[i], 12);
    }
  }

  saveConfig();
  server.send(200, "text/html",
    "<h3 style='font-family:Arial'>Saved successfully.<br>Reboot device.</h3>"
  );
}

// ================= CONFIG MODE =================
void startConfigMode() {
  loadConfig();

  // Ensure WiFi radio is awake even if normal mode forced it off/sleep
  WiFi.forceSleepWake();
  delay(1);
  WiFi.mode(WIFI_AP);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.softAPConfig(apIP, apGW, apSN);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();

  while (true) {
    // Self-heal AP if it drops (power spikes / sleep interactions)
    if (WiFi.getMode() != WIFI_AP) {
      WiFi.mode(WIFI_AP);
      WiFi.setSleepMode(WIFI_NONE_SLEEP);
      WiFi.softAPConfig(apIP, apGW, apSN);
      WiFi.softAP(AP_SSID, AP_PASS);
    }
    server.handleClient();
    delay(10);
    yield();   // watchdog friendliness
  }
}
