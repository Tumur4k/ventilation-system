#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ── PIN ТОДОРХОЙЛОЛТ ──
#define DHTPIN1  13
#define DHTTYPE  DHT11

DHT dht1(DHTPIN1, DHTTYPE);

// ── NETWORK ──
const char* ssid        = "gcllc14";
const char* password    = "qcqcqcqc";
const char* mqtt_server = "broker.hivemq.com";

// ── RELAY / WAREHOUSE ──
const int relayPins[] = {4};
const int wh_ids[]    = {5};
const int numRelays   = 1;

// ── АВТО ЦИКЛ ТОХИРГОО ──
const unsigned long CYCLE_ON_MS  = 30UL * 60 * 1000;
const unsigned long CYCLE_OFF_MS =  5UL * 60 * 1000;

bool          autoCycleEnabled[1] = {false};
bool          cyclePhaseOn[1]     = {true};
unsigned long cycleStartTime[1]   = {0};

float lastTemp     = 0;
float lastHumidity = 0;

WiFiClient   espClient;
PubSubClient client(espClient);

// ──────────────────────────────────────────────
void setRelay(int i, bool on) {
  digitalWrite(relayPins[i], on ? LOW : HIGH);
}
bool getRelay(int i) {
  return digitalRead(relayPins[i]) == LOW;
}

void publishAutoStatus(int i) {
  if (!client.connected()) return;
  String base = "warehouse/" + String(wh_ids[i]);
  if (!autoCycleEnabled[i]) {
    client.publish((base + "/auto_status").c_str(), "MANUAL", true);
    return;
  }
  unsigned long elapsed = millis() - cycleStartTime[i];
  unsigned long limit   = cyclePhaseOn[i] ? CYCLE_ON_MS : CYCLE_OFF_MS;
  unsigned long remain  = elapsed < limit ? limit - elapsed : 0;
  char buf[32];
  snprintf(buf, sizeof(buf), "AUTO|%s|%lum%02lus",
    cyclePhaseOn[i] ? "ON" : "OFF",
    remain / 60000,
    (remain % 60000) / 1000
  );
  client.publish((base + "/auto_status").c_str(), buf, true);
}

void publishAllStatus() {
  client.publish("warehouse/system/esp3_status", "ONLINE", true);

  String base = "warehouse/5";
  client.publish((base + "/status").c_str(), getRelay(0) ? "ON" : "OFF", true);
  if (lastTemp > 0) {
    client.publish((base + "/temp").c_str(),     String(lastTemp).c_str(),     true);
    client.publish((base + "/humidity").c_str(), String(lastHumidity).c_str(), true);
  }
  publishAutoStatus(0);
}

void startAutoCycle(int i) {
  autoCycleEnabled[i] = true;
  cyclePhaseOn[i]     = true;
  cycleStartTime[i]   = millis();
  setRelay(i, true);
  if (client.connected()) {
    String base = "warehouse/" + String(wh_ids[i]);
    client.publish((base + "/status").c_str(), "ON", true);
    publishAutoStatus(i);
  }
  Serial.println("WH" + String(wh_ids[i]) + " AUTO START → 30min ON");
}

void stopAutoCycle(int i) {
  autoCycleEnabled[i] = false;
  if (client.connected()) publishAutoStatus(i);
  Serial.println("WH" + String(wh_ids[i]) + " AUTO STOP → MANUAL mode");
}

void handleAutoCycle(int i) {
  if (!autoCycleEnabled[i]) return;
  unsigned long elapsed = millis() - cycleStartTime[i];

  if (cyclePhaseOn[i] && elapsed >= CYCLE_ON_MS) {
    cyclePhaseOn[i]   = false;
    cycleStartTime[i] = millis();
    setRelay(i, false);
    Serial.println("WH" + String(wh_ids[i]) + " AUTO → OFF phase (5min rest)");
    if (client.connected()) {
      String base = "warehouse/" + String(wh_ids[i]);
      client.publish((base + "/status").c_str(), "OFF", true);
      publishAutoStatus(i);
    }
  } else if (!cyclePhaseOn[i] && elapsed >= CYCLE_OFF_MS) {
    cyclePhaseOn[i]   = true;
    cycleStartTime[i] = millis();
    setRelay(i, true);
    Serial.println("WH" + String(wh_ids[i]) + " AUTO → ON phase (30min)");
    if (client.connected()) {
      String base = "warehouse/" + String(wh_ids[i]);
      client.publish((base + "/status").c_str(), "ON", true);
      publishAutoStatus(i);
    }
  }
}

// ──────────────────────────────────────────────
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  String topicStr = String(topic);
  Serial.println("[MQTT] " + topicStr + " → " + message);

  if (topicStr == "warehouse/all/auto") {
    if (message == "ON") startAutoCycle(0);
    else                 stopAutoCycle(0);
    return;
  }

  if (topicStr == "warehouse/all/control") {
    if (autoCycleEnabled[0]) stopAutoCycle(0);
    setRelay(0, message == "ON");
    publishAllStatus();
    return;
  }

  if (topicStr == "warehouse/system/request_update") {
    publishAllStatus();
    return;
  }

  if (topicStr == "warehouse/5/auto") {
    if (message == "ON") startAutoCycle(0);
    else                 stopAutoCycle(0);
    return;
  }

  if (topicStr == "warehouse/5/control") {
    if (autoCycleEnabled[0]) stopAutoCycle(0);
    setRelay(0, message == "ON");
    client.publish("warehouse/5/status", message == "ON" ? "ON" : "OFF", true);
    Serial.println("WH5 MANUAL → " + message);
  }
}

// ──────────────────────────────────────────────
void setup_wifi() {
  delay(10);
  Serial.print("WiFi connecting: "); Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi OK — " + WiFi.localIP().toString());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("MQTT...");
    String cid = "ESP32-Node3-" + String(random(0, 9999));
    if (client.connect(cid.c_str(), "warehouse/system/esp3_status", 1, true, "OFFLINE")) {
      Serial.println(" connected!");
      client.publish("warehouse/system/esp3_status", "ONLINE", true);

      client.subscribe("warehouse/5/control");
      client.subscribe("warehouse/5/auto");
      client.subscribe("warehouse/all/control");
      client.subscribe("warehouse/all/auto");
      client.subscribe("warehouse/system/request_update");

      publishAllStatus();
    } else {
      Serial.print(" fail rc="); Serial.println(client.state());
      delay(5000);
    }
  }
}

// ──────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  dht1.begin();

  pinMode(relayPins[0], OUTPUT);
  setRelay(0, false);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

// ──────────────────────────────────────────────
unsigned long lastSensor  = 0;
unsigned long lastAutoLog = 0;

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  handleAutoCycle(0);

  if (millis() - lastSensor > 5000) {
    lastSensor = millis();

    float t1 = dht1.readTemperature(), h1 = dht1.readHumidity();
    if (!isnan(t1) && !isnan(h1)) {
      lastTemp = t1; lastHumidity = h1;
      client.publish("warehouse/5/temp",     String(t1).c_str(), true);
      client.publish("warehouse/5/humidity", String(h1).c_str(), true);
      Serial.println("WH5: " + String(t1) + "°C  " + String(h1) + "%");
    } else Serial.println("WH5: DHT read failed");
  }

  if (millis() - lastAutoLog > 60000) {
    lastAutoLog = millis();
    if (autoCycleEnabled[0]) publishAutoStatus(0);
  }
}
