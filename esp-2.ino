#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ── PIN ТОДОРХОЙЛОЛТ ──
#define DHTPIN1  13
#define DHTPIN2  14
#define DHTTYPE  DHT11

DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);

// ── NETWORK ──
const char* ssid        = "TuMuR4K";
const char* password    = "dataavaach";
const char* mqtt_server = "broker.hivemq.com";

// ── RELAY / WAREHOUSE ──
const int relayPins[] = {4, 5};
const int wh_ids[]    = {3, 4};
const int numRelays   = 2;

// ── АВТО ЦИКЛ ТОХИРГОО ──
const unsigned long CYCLE_ON_MS  = 30UL * 60 * 1000;
const unsigned long CYCLE_OFF_MS =  5UL * 60 * 1000;

bool          autoCycleEnabled[2] = {false, false};
bool          cyclePhaseOn[2]     = {true,  true};
unsigned long cycleStartTime[2]   = {0, 0};

float lastTemp[2]     = {0, 0};
float lastHumidity[2] = {0, 0};

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
  client.publish("warehouse/system/esp2_status", "ONLINE", true);

  for (int i = 0; i < numRelays; i++) {
    String base = "warehouse/" + String(wh_ids[i]);
    client.publish((base + "/status").c_str(), getRelay(i) ? "ON" : "OFF", true);
    if (lastTemp[i] > 0) {
      client.publish((base + "/temp").c_str(),     String(lastTemp[i]).c_str(),     true);
      client.publish((base + "/humidity").c_str(), String(lastHumidity[i]).c_str(), true);
    }
    publishAutoStatus(i);
  }
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
    for (int i = 0; i < numRelays; i++) {
      if (message == "ON") startAutoCycle(i);
      else                 stopAutoCycle(i);
    }
    return;
  }

  if (topicStr == "warehouse/all/control") {
    for (int i = 0; i < numRelays; i++) {
      if (autoCycleEnabled[i]) stopAutoCycle(i);
      setRelay(i, message == "ON");
    }
    publishAllStatus();
    return;
  }

  if (topicStr == "warehouse/system/request_update") {
    publishAllStatus();
    return;
  }

  for (int i = 0; i < numRelays; i++) {
    String base = "warehouse/" + String(wh_ids[i]);

    if (topicStr == base + "/auto") {
      if (message == "ON") startAutoCycle(i);
      else                 stopAutoCycle(i);
      return;
    }

    if (topicStr == base + "/control") {
      if (autoCycleEnabled[i]) stopAutoCycle(i);
      setRelay(i, message == "ON");
      client.publish((base + "/status").c_str(), message == "ON" ? "ON" : "OFF", true);
      Serial.println("WH" + String(wh_ids[i]) + " MANUAL → " + message);
    }
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
    String cid = "ESP32-Node2-" + String(random(0, 9999));
    if (client.connect(cid.c_str(), "warehouse/system/esp2_status", 1, true, "OFFLINE")) {
      Serial.println(" connected!");
      client.publish("warehouse/system/esp2_status", "ONLINE", true);

      client.subscribe("warehouse/3/control");
      client.subscribe("warehouse/4/control");
      client.subscribe("warehouse/3/auto");
      client.subscribe("warehouse/4/auto");
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
  dht1.begin(); dht2.begin();

  for (int i = 0; i < numRelays; i++) {
    pinMode(relayPins[i], OUTPUT);
    setRelay(i, false);
  }

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

  for (int i = 0; i < numRelays; i++) handleAutoCycle(i);

  if (millis() - lastSensor > 5000) {
    lastSensor = millis();

    float t1 = dht1.readTemperature(), h1 = dht1.readHumidity();
    if (!isnan(t1) && !isnan(h1)) {
      lastTemp[0] = t1; lastHumidity[0] = h1;
      client.publish("warehouse/3/temp",     String(t1).c_str(), true);
      client.publish("warehouse/3/humidity", String(h1).c_str(), true);
      Serial.println("WH3: " + String(t1) + "°C  " + String(h1) + "%");
    } else Serial.println("WH3: DHT read failed");

    float t2 = dht2.readTemperature(), h2 = dht2.readHumidity();
    if (!isnan(t2) && !isnan(h2)) {
      lastTemp[1] = t2; lastHumidity[1] = h2;
      client.publish("warehouse/4/temp",     String(t2).c_str(), true);
      client.publish("warehouse/4/humidity", String(h2).c_str(), true);
      Serial.println("WH4: " + String(t2) + "°C  " + String(h2) + "%");
    } else Serial.println("WH4: DHT read failed");
  }

  if (millis() - lastAutoLog > 60000) {
    lastAutoLog = millis();
    for (int i = 0; i < numRelays; i++) {
      if (autoCycleEnabled[i]) publishAutoStatus(i);
    }
  }
}
