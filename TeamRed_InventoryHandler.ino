/*
  ESP32 RFID Inventory Manager - Fixed, complete sketch

  Replace the placeholders:
    - WIFI_SSID, WIFI_PASS
    - TELEGRAM_TOKEN, TELEGRAM_CHAT_ID
    - SHEETS_WEBHOOK_URL
  Install MFRC522v2 library (or adjust includes to your MFRC522 library) and use ESP32 core for Arduino.
*/
// ===== ADD THIS INCLUDE =====
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <vector>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
#include <time.h>

// ===== CONFIG =====
const char* WIFI_SSID = "Advaith";
const char* WIFI_PASS = "12345678";

const char* TELEGRAM_TOKEN = "8729945437:AAEEXaisA1a-ODOaxZg2lJqeKQQHj5eWuZI";
const long TELEGRAM_CHAT_ID = 8714424008; // replace with your chat id

const char* SHEETS_WEBHOOK_URL = "https://script.google.com/macros/s/AKfycbxLWF37FQtUbLfsIjAHQMs_NW0OsXi-JIbhgS13lkrShIWNQtSKajeas5bOyXa5Bx9p/exec";

const char* TZ = "UTC";
const char* NTP_POOL = "pool.ntp.org";

// RFID SS pin (adjust if needed)
MFRC522DriverPinSimple ss_pin(5);
MFRC522DriverSPI driver{ss_pin};
MFRC522 mfrc522{driver};

// Web server
WebServer server(80);

// LED alert pin
const int alertLedPin = 4;

// ===== Data structures =====
struct Product {
  String uid;
  String name;
  int quantity;
  int threshold;
};

struct AuditEntry {
  unsigned long ts;
  String uid;
  String name;
  String action;
  int quantityAfter;
  String note;
};

std::vector<Product> inventory;
std::vector<AuditEntry> auditLog;

// ===== State =====
bool incrementMode = false;
String lastScannedUID = "";
bool lastScanKnown = false;
String lastScanProductName = "";
int lastScanQuantity = 0;
String lastOperationMessage = "";

// Scheduling
enum ScheduleFreq { S_NONE=0, S_WEEKLY=1, S_MONTHLY=2 };
ScheduleFreq scheduleFreq = S_NONE;
int scheduleWeekday = 1;
int scheduleHour = 9;
int scheduleMinute = 0;
unsigned long lastSummarySent = 0;

// ===== Utilities =====
String uidToString(byte *uidBytes, byte uidSize) {
  String s = "";
  for (byte i = 0; i < uidSize; i++) {
    if (uidBytes[i] < 0x10) s += "0";
    s += String(uidBytes[i], HEX);
  }
  s.toLowerCase();
  return s;
}

int findProductIndexByUID(const String &uid) {
  for (size_t i = 0; i < inventory.size(); ++i) {
    if (inventory[i].uid == uid) return (int)i;
  }
  return -1;
}

// ===== Networking helpers =====
void sendTelegram(const String &text) {
  if (strlen(TELEGRAM_TOKEN) < 10) return;
  String url = String("https://api.telegram.org/bot" ) + TELEGRAM_TOKEN + "/sendMessage";
  int attempts = 0;
  const int maxAttempts = 3;
  while (attempts < maxAttempts) {
    attempts++;
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json" );
    String payloadText = text;
    payloadText.replace("\\", "\\\\" );
    payloadText.replace("\"", "\\\"" );
    payloadText.replace("\n", "\\n" );
    String payload = "{\"chat_id\":\"" + String(TELEGRAM_CHAT_ID) + "\",\"text\":\"" + payloadText + "\"}";
    int code = http.POST(payload);
    http.end();
    if (code == 200) return;
    delay(700);
  }
}

void postToSheets(const AuditEntry &e) {
  if (strlen(SHEETS_WEBHOOK_URL) < 10) return;
  HTTPClient http;
  http.begin(SHEETS_WEBHOOK_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  auto urlencode = [](const String &s) {
    String out = "";
    char c;
    for (size_t i = 0; i < s.length(); ++i) {
      c = s[i];
      if ( (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '-' || c == '_' || c == '.' || c == '~') {
        out += c;
      } else if (c == ' ') {
        out += '+';
      } else {
        char buf[5];
        sprintf(buf, "%%%02X", (unsigned char)c);
        out += buf;
      }
    }
    return out;
  };
  String body = "ts=" + String(e.ts) + "&uid=" + urlencode(e.uid) + "&name=" + urlencode(e.name)
                + "&action=" + urlencode(e.action) + "&qty=" + String(e.quantityAfter) + "&note=" + urlencode(e.note);
  int code = http.POST(body);
  http.end();
  (void)code;
}

void addAudit(const String &uid, const String &name, const String &action, int qtyAfter, const String &note="" ) {
  AuditEntry a;
  a.ts = time(nullptr);
  a.uid = uid;
  a.name = name;
  a.action = action;
  a.quantityAfter = qtyAfter;
  a.note = note;
  auditLog.push_back(a);
  postToSheets(a);
  if (auditLog.size() > 2000) {
    auditLog.erase(auditLog.begin(), auditLog.begin() + (auditLog.size() - 2000));
  }
}

void updateAlertLED() {
  bool anyAlert = false;
  for (auto &p : inventory) {
    if (p.quantity <= 0 || p.quantity < p.threshold) { anyAlert = true; break; }
  }
  digitalWrite(alertLedPin, anyAlert ? HIGH : LOW);
}

String inventoryTableHTML() {
  String html = "<table style='width:100%;border-collapse:collapse;color:#fff'>";
  html += "<tr style='text-align:left'><th>UID</th><th>Name</th><th>Qty</th><th>Threshold</th></tr>";
  for (auto &p : inventory) {
    html += "<tr style='border-top:1px solid rgba(255,255,255,0.08)'>";
    html += "<td style='padding:6px;font-family:monospace;'>" + p.uid + "</td>";
    html += "<td style='padding:6px;'>" + p.name + "</td>";
    html += "<td style='padding:6px;'>" + String(p.quantity) + "</td>";
    html += "<td style='padding:6px;'>" + String(p.threshold) + "</td>";
    html += "</tr>";
  }
  html += "</table>";
  return html;
}

String analyticsJSON() {
  String out = "{";
  out += "\"products\":[";
  bool firstP = true;
  for (auto &p : inventory) {
    if (!firstP) out += ",";
    firstP = false;
    out += "{";
    out += "\"uid\":\"" + p.uid + "\"";
    out += ",\"name\":\"" + p.name + "\"";
    out += ",\"threshold\":" + String(p.threshold);
    out += ",\"points\":[";
    int count = 0;
    for (int i = (int)auditLog.size()-1; i >= 0 && count < 48; --i) {
      if (auditLog[i].uid == p.uid) {
        if (count) out += ",";
        out += "[" + String(auditLog[i].ts) + "," + String(auditLog[i].quantityAfter) + "]";
        count++;
      }
    }
    if (count == 0) {
      out += "[" + String(time(nullptr)) + "," + String(p.quantity) + "]";
    }
    out += "]";
    out += "}";
  }
  out += "]";
  out += "}";
  return out;
}

// ===== Web UI (raw literal) =====
const char indexPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32 Inventory</title>
  <style>
    body { font-family: Arial, Helvetica, sans-serif; background:#1e1e2f; color:#fff; padding:16px; margin:0; }
    .card { background:#2a2a40; padding:14px; border-radius:10px; margin-bottom:12px; box-shadow:0 2px 6px rgba(0,0,0,0.4); }
    .mono { font-family:monospace; }
    label.field { display:block; font-size:13px; margin-top:8px; color:#dfe6ff; }
    .row { display:flex; gap:8px; }
    .col { flex:1; }
    button { padding:8px 12px; border:none; border-radius:6px; background:#3498db; color:white; cursor:pointer; }
    input, select, textarea { padding:8px; width:100%; margin-top:6px; border-radius:6px; border:none; box-sizing:border-box; background:#1b1b2b; color:#fff; }
    small.hint { color:#bfc7ff; display:block; margin-top:6px; font-size:12px; }
    .muted { color:#9aa3d6; font-size:13px; }
    table { width:100%; border-collapse:collapse; margin-top:8px; }
    th, td { text-align:left; padding:8px; border-bottom:1px solid rgba(255,255,255,0.04); font-size:13px; }
    th { color:#cfe3ff; font-weight:600; }
  </style>
</head>

<body>

<div class="card">
  <h2>ESP32 Inventory</h2>
  <div class="row">
    <div class="col">
      <label class="field">Mode</label>
      <div class="row" style="align-items:center;">
        <label style="display:flex;align-items:center;gap:8px;">
          <input type="checkbox" id="modeSwitch" onchange="toggleMode()"> <span class="muted">Increment Mode</span>
        </label>
        <div style="margin-left:auto" id="modeText" class="muted">-</div>
      </div>
      <small class="hint">Toggle to switch between adding and removing stock.</small>
    </div>
    <div class="col" style="max-width:220px;">
      <label class="field">Alert LED</label>
      <div id="ledStatus" class="muted">Status: normal</div>
      <small class="hint">LED indicates any item below threshold.</small>
    </div>
  </div>
</div>

<div class="card">
  <h3>Last Scan</h3>
  <label class="field">UID</label>
  <div id="lastUid" class="mono">-</div>

  <label class="field">Known</label>
  <div id="lastKnown" class="muted">-</div>

  <label class="field">Product Name</label>
  <div id="lastName" class="muted">-</div>

  <label class="field">Quantity</label>
  <div id="lastQty" class="muted">-</div>

  <label class="field">Message</label>
  <div id="lastMsg" class="muted"></div>
</div>

<div class="card" id="addBox" style="display:none;">
  <h3>Add New Product</h3>

  <label class="field">Scanned UID</label>
  <div id="newUid" class="mono">-</div>

  <label class="field" for="newName">Product name</label>
  <input id="newName" type="text" placeholder="e.g., Widget A" required>

  <div class="row">
    <div class="col">
      <label class="field" for="newQty">Initial quantity</label>
      <input id="newQty" type="number" min="0" value="1">
    </div>
    <div class="col">
      <label class="field" for="newThreshold">Minimum threshold</label>
      <input id="newThreshold" type="number" min="0" value="1">
    </div>
  </div>

  <label class="field" for="newNote">Note (optional)</label>
  <input id="newNote" type="text" placeholder="e.g., location, batch">

  <div style="margin-top:10px;">
    <button onclick="addProduct()">Add product</button>
    <button style="background:#6c757d;margin-left:8px;" onclick="hideAddBox()">Cancel</button>
  </div>
  <small class="hint">All fields are required except Note. UID is filled from last scan.</small>
</div>

<div class="card">
  <h3>Inventory</h3>
  <div id="inventory" class="muted">loading...</div>
  <small class="hint">Click an item in the table to prefill quick actions (future enhancement).</small>
</div>

<div class="card">
  <h3>Quick Actions</h3>
  <div class="row">
    <div class="col">
      <label class="field">Manual UID (optional)</label>
      <input id="manualUid" type="text" placeholder="paste UID to act on specific item">
    </div>
    <div style="width:140px;">
      <label class="field">Amount</label>
      <input id="manualAmt" type="number" value="1" min="1">
    </div>
  </div>
  <div style="margin-top:8px;">
    <button onclick="manualAdjust(true)">Increment</button>
    <button style="background:#e74c3c;margin-left:8px;" onclick="manualAdjust(false)">Decrement</button>
  </div>
  <small class="hint">Use manual controls when you don't want to scan a tag.</small>
</div>

<div class="card">
  <h3>Schedule Summary</h3>

  <label class="field" for="freq">Frequency</label>
  <select id="freq">
    <option value="none">None</option>
    <option value="weekly">Weekly</option>
    <option value="monthly">Monthly</option>
  </select>

  <div class="row">
    <div class="col">
      <label class="field" for="wd">Weekday (1=Sun..7=Sat)</label>
      <input id="wd" type="number" min="1" max="7" value="1">
    </div>
    <div class="col">
      <label class="field" for="hr">Hour (0-23)</label>
      <input id="hr" type="number" min="0" max="23" value="9">
    </div>
    <div class="col">
      <label class="field" for="mn">Minute (0-59)</label>
      <input id="mn" type="number" min="0" max="59" value="0">
    </div>
  </div>

  <div style="margin-top:10px;">
    <button onclick="setSchedule()">Set schedule</button>
    <button style="background:#6c757d;margin-left:8px;" onclick="clearSchedule()">Clear</button>
  </div>
  <small class="hint">Scheduled summary will be sent via Telegram and logged to Sheets.</small>
</div>

<script>
function toggleMode() {
  const m = document.getElementById('modeSwitch').checked ? 'increment' : 'decrement';
  fetch('/setmode?m=' + encodeURIComponent(m)).catch(()=>{});
  document.getElementById('modeText').innerText = m;
}

function addProduct() {
  const uid = document.getElementById('newUid').innerText.trim();
  const name = encodeURIComponent(document.getElementById('newName').value.trim());
  const qty = encodeURIComponent(document.getElementById('newQty').value);
  const threshold = encodeURIComponent(document.getElementById('newThreshold').value);
  const note = encodeURIComponent(document.getElementById('newNote').value || '');
  if (!uid || uid === '-' ) { alert('No UID available. Scan a tag first.'); return; }
  if (!name) { alert('Enter product name'); return; }
  fetch('/add?uid=' + uid + '&name=' + name + '&qty=' + qty + '&threshold=' + threshold + '&note=' + note)
    .then(() => { hideAddBox(); poll(); });
}

function hideAddBox() {
  document.getElementById('addBox').style.display = 'none';
}

function setSchedule() {
  const f = document.getElementById('freq').value;
  const wd = document.getElementById('wd').value;
  const hr = document.getElementById('hr').value;
  const mn = document.getElementById('mn').value;
  fetch('/setschedule?freq=' + encodeURIComponent(f) + '&wd=' + encodeURIComponent(wd) + '&hr=' + encodeURIComponent(hr) + '&mn=' + encodeURIComponent(mn))
    .then(()=> alert('Schedule updated')).catch(()=>alert('Schedule update failed'));
}

function clearSchedule() {
  fetch('/setschedule?freq=none&wd=1&hr=0&mn=0').then(()=>alert('Schedule cleared')).catch(()=>alert('Failed'));
}

function manualAdjust(isIncrement) {
  const uid = document.getElementById('manualUid').value.trim();
  const amt = parseInt(document.getElementById('manualAmt').value) || 1;
  if (!uid) { alert('Enter UID or scan a tag first'); return; }
  // call server endpoint to perform manual adjust (server should implement /manual?uid=&amt=&op=)
  const op = isIncrement ? 'inc' : 'dec';
  fetch('/manual?uid=' + encodeURIComponent(uid) + '&amt=' + amt + '&op=' + op)
    .then(()=> poll())
    .catch(()=> alert('Manual adjust failed'));
}

function poll() {
  fetch('/status')
  .then(r => r.json())
  .then(d => {
    document.getElementById('modeText').innerText = d.mode;
    document.getElementById('modeSwitch').checked = (d.mode === 'increment');

    document.getElementById('lastUid').innerText = d.lastUid || '-';
    document.getElementById('lastKnown').innerText = d.lastKnown ? 'yes' : 'no';
    document.getElementById('lastName').innerText = d.lastName || '-';
    document.getElementById('lastQty').innerText = (d.lastQty !== null && d.lastQty !== undefined) ? d.lastQty : '-';
    document.getElementById('lastMsg').innerText = d.lastMsg || '';

    document.getElementById('inventory').innerHTML = d.inventoryHtml || '<div class="muted">No items</div>';

    // show add box when unknown UID scanned
    if (!d.lastKnown && d.lastUid) {
      document.getElementById('addBox').style.display = 'block';
      document.getElementById('newUid').innerText = d.lastUid;
    } else {
      document.getElementById('addBox').style.display = 'none';
    }

    // update schedule UI
    if (d.schedule) {
      document.getElementById('freq').value = d.schedule.freq || 'none';
      document.getElementById('wd').value = d.schedule.weekday || 1;
      document.getElementById('hr').value = d.schedule.hour || 9;
      document.getElementById('mn').value = d.schedule.minute || 0;
    }

    // LED status (if server provides)
    if (d.led) {
      document.getElementById('ledStatus').innerText = 'Status: ' + d.led;
    }
  })
  .catch(err => {
    console.log('poll error', err);
  });
}

setInterval(poll, 2000);
window.onload = poll;
</script>

</body>
</html>
)rawliteral";
// ===== Web handlers =====
void handleRoot() { server.send_P(200, "text/html", indexPage); }

void handleSetMode() {
  if (!server.hasArg("m")) { server.send(400, "text/plain", "Missing mode"); return; }
  String m = server.arg("m");
  incrementMode = (m == "increment");
  server.send(200, "text/plain", "OK");
}

void handleAddProduct() {
  if (!server.hasArg("uid") || !server.hasArg("name") || !server.hasArg("threshold") || !server.hasArg("qty")) {
    server.send(400, "text/plain", "Missing args"); return;
  }
  String uid = server.arg("uid"); uid.toLowerCase();
  String name = server.arg("name");
  int threshold = server.arg("threshold").toInt();
  int qty = server.arg("qty").toInt();
  int idx = findProductIndexByUID(uid);
  if (idx >= 0) {
    inventory[idx].name = name;
    inventory[idx].threshold = threshold;
    inventory[idx].quantity = qty;
    addAudit(uid, name, "update", qty, "updated via UI");
  } else {
    Product p; p.uid = uid; p.name = name; p.threshold = threshold; p.quantity = qty;
    inventory.push_back(p);
    addAudit(uid, name, "add", qty, "added via UI");
  }
  updateAlertLED();
  server.send(200, "text/plain", "Added");
}

void handleStatus() {
  String invHtml = inventoryTableHTML();
  invHtml.replace("\\", "\\\\");
  invHtml.replace("\"", "\\\"");
  invHtml.replace("\n", "\\n");
  String payload = "{";
  payload += "\"mode\":\"" + String(incrementMode ? "increment" : "decrement") + "\"";
  payload += ",\"lastUid\":\"" + lastScannedUID + "\"";
  payload += ",\"lastKnown\":" + String(lastScanKnown ? "true" : "false");
  payload += ",\"lastName\":\"" + lastScanProductName + "\"";
  payload += ",\"lastQty\":" + String(lastScanQuantity);
  payload += ",\"lastMsg\":\"" + lastOperationMessage + "\"";
  String freqStr = "none";
  if (scheduleFreq == S_WEEKLY) freqStr = "weekly";
  else if (scheduleFreq == S_MONTHLY) freqStr = "monthly";
  payload += ",\"schedule\":{\"freq\":\"" + freqStr + "\",\"weekday\":" + String(scheduleWeekday) + ",\"hour\":" + String(scheduleHour) + ",\"minute\":" + String(scheduleMinute) + "}";
  payload += ",\"inventoryHtml\":\"" + invHtml + "\"";
  payload += "}";
  server.send(200, "application/json", payload);
}

void handleAnalytics() {
  String j = analyticsJSON();
  server.send(200, "application/json", j);
}

void handleSetSchedule() {
  if (!server.hasArg("freq") || !server.hasArg("wd") || !server.hasArg("hr") || !server.hasArg("mn")) {
    server.send(400, "text/plain", "Missing args"); return;
  }
  String f = server.arg("freq");
  if (f == "weekly") scheduleFreq = S_WEEKLY;
  else if (f == "monthly") scheduleFreq = S_MONTHLY;
  else scheduleFreq = S_NONE;
  scheduleWeekday = server.arg("wd").toInt();
  scheduleHour = server.arg("hr").toInt();
  scheduleMinute = server.arg("mn").toInt();
  server.send(200, "text/plain", "OK");
}

// ===== Setup & Loop =====
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(alertLedPin, OUTPUT);
  digitalWrite(alertLedPin, LOW);

  mfrc522.PCD_Init();
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522, Serial);
  Serial.println(F("RFID reader initialized."));

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to WiFi '%s'", WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    Serial.print(".");
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed");
  }

  configTime(0, 0, NTP_POOL);
  setenv("TZ", TZ, 1);
  tzset();

  server.on("/", handleRoot);
  server.on("/setmode", handleSetMode);
  server.on("/add", handleAddProduct);
  server.on("/status", handleStatus);
  server.on("/analytics", handleAnalytics);
  server.on("/setschedule", handleSetSchedule);
  server.begin();
  Serial.println("HTTP server started");
}

void sendBelowThresholdNotification(const Product &p) {
  String title = "Low stock: " + p.name;
  String body = "Item '" + p.name + "' (UID " + p.uid + ") qty=" + String(p.quantity) + " threshold=" + String(p.threshold);
  sendTelegram(title + "\n" + body);
  addAudit(p.uid, p.name, "alert", p.quantity, "below threshold - notification sent");
}

void sendScheduledSummary() {
  String summary = "";
  for (auto &p : inventory) {
    if (p.quantity <= 0 || p.quantity < p.threshold) {
      summary += p.name + " (uid:" + p.uid + ") qty=" + String(p.quantity) + " thr=" + String(p.threshold) + "\n";
    }
  }
  if (summary.length() == 0) summary = "All items above threshold.";
  sendTelegram("Inventory summary:\n" + summary);
  addAudit("SCHEDULE", "SUMMARY", "summary_sent", 0, summary);
  lastSummarySent = time(nullptr);
}

void checkSchedule() {
  if (scheduleFreq == S_NONE) return;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  int wday = timeinfo.tm_wday + 1;
  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  if (hour == scheduleHour && minute == scheduleMinute) {
    if (scheduleFreq == S_WEEKLY) {
      if (wday == scheduleWeekday) {
        if (time(nullptr) - lastSummarySent > 50) sendScheduledSummary();
      }
    } else if (scheduleFreq == S_MONTHLY) {
      int mday = timeinfo.tm_mday;
      if (mday == 1) {
        if (time(nullptr) - lastSummarySent > 50) sendScheduledSummary();
      }
    }
  }
}

void loop() {
  server.handleClient();
  checkSchedule();

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String uid = uidToString(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.print("Scanned UID: "); Serial.println(uid);
    lastScannedUID = uid;
    lastOperationMessage = "";

    int idx = findProductIndexByUID(uid);
    if (idx >= 0) {
      lastScanKnown = true;
      Product &p = inventory[idx];
      lastScanProductName = p.name;
      if (incrementMode) {
        p.quantity++;
        addAudit(uid, p.name, "increment", p.quantity, "");
        lastOperationMessage = "Incremented";
      } else {
        if (p.quantity <= 0) {
          lastOperationMessage = "Error: no more product (quantity is 0). Cannot decrement.";
          addAudit(uid, p.name, "error", p.quantity, "decrement blocked - zero");
        } else {
          p.quantity--;
          if (p.quantity < 0) p.quantity = 0;
          addAudit(uid, p.name, "decrement", p.quantity, "");
          lastOperationMessage = "Decremented";
        }
      }
      lastScanQuantity = p.quantity;
      if (p.quantity <= 0 || p.quantity < p.threshold) {
        updateAlertLED();
        sendBelowThresholdNotification(p);
      } else {
        updateAlertLED();
      }
    } else {
      lastScanKnown = false;
      lastScanProductName = "";
      lastScanQuantity = -1;
      lastOperationMessage = "Unknown UID - add via web UI";
      addAudit(uid, "", "unknown", -1, "unknown uid scanned");
      Serial.println("Unknown UID scanned");
    }

    mfrc522.PICC_HaltA();
    delay(200);
  }

  static unsigned long lastScanMillis = 0;
  if (lastScannedUID.length() > 0) lastScanMillis = millis();
  if (millis() - lastScanMillis > 30000) {
    lastScannedUID = "";
    lastScanKnown = false;
    lastScanProductName = "";
    lastScanQuantity = 0;
    lastOperationMessage = "";
  }
}
