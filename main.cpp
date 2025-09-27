#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>

// ==== Wi-Fi (改成你的 2.4GHz SSID/密码) ====
const char* WIFI_SSID = "TP-LINK_697F";
const char* WIFI_PASS = "hjc198902165399";

// ==== 自动扫描的备选引脚/类型 ====
// 常用引脚：D4(GPIO2)、D5(GPIO14)、D2(GPIO4)
const int   TRY_PINS[]  = {2, 14, 4};
const char* PIN_NAME[]  = {"D4(GPIO2)", "D5(GPIO14)", "D2(GPIO4)"};

// 类型同时尝试 DHT11 / DHT22
const int   TRY_TYPES[] = {DHT11, DHT22};
const char* TYPE_NAME[] = {"DHT11", "DHT22"};

// 全局“已锁定”配置
DHT*  g_dht = nullptr;
int   g_pin = -1;
int   g_type = -1;

ESP8266WebServer server(80);

// 封装读函数（NaN 保护）
static float readTempC()   { if (!g_dht) return NAN; float t = g_dht->readTemperature(); return isnan(t)?NAN:t; }
static float readHumidity(){ if (!g_dht) return NAN; float h = g_dht->readHumidity();    return isnan(h)?NAN:h; }

// 把 pin 转成名字
static const char* pinName(int pin) {
  if (pin == 2)  return PIN_NAME[0];
  if (pin == 14) return PIN_NAME[1];
  return PIN_NAME[2];
}
static const char* typeName(int type) {
  return (type == DHT11) ? TYPE_NAME[0] : TYPE_NAME[1];
}

// 试一个 (pin, type) 组合：成功返回 true，并把 g_dht/g_pin/g_type 设置好
static bool try_one(int pin, int type) {
  Serial.printf("[SCAN] Try %s, %s\n", pinName(pin), typeName(type));
  pinMode(pin, INPUT_PULLUP);      // 稳定 DATA 线
  DHT tester(pin, type);
  tester.begin();

  // 丢弃前两次读（DHT 常见上电 NaN）
  for (int i=0;i<2;i++){ tester.readTemperature(); tester.readHumidity(); delay(1200); }

  // 连续 5 次尝试，≥2s 间隔（DHT11 最大 1Hz）
  for (int k=0; k<5; ++k) {
    float t = tester.readTemperature();
    float h = tester.readHumidity();
    if (!isnan(t) && !isnan(h) && t>-40 && t<125 && h>=0 && h<=100) {
      Serial.printf("[OK]  %s, %s  ->  T=%.2f°C  H=%.1f%%\n", pinName(pin), typeName(type), t, h);
      g_pin = pin; g_type = type;
      g_dht = new DHT(pin, type);
      g_dht->begin();
      // 再预读一次稳定
      delay(1200); g_dht->readTemperature(); g_dht->readHumidity();
      return true;
    } else {
      Serial.println("     read failed, retry...");
    }
    delay(2200);
  }
  return false;
}

// HTTP handlers
static void handleRoot() {
  String info = "<h2>ESP8266 DHT Auto-Detected</h2>";
  if (g_dht) {
    info += String("<p>Pin: ") + pinName(g_pin) + ", Type: " + typeName(g_type) + "</p>";
  } else {
    info += "<p><b>Sensor NOT detected</b>. Check wiring (3V3/GND/DATA) & try D4/D5/D2.</p>";
  }
  info += "<p><a href=\"/api/temp\">/api/temp</a> | <a href=\"/api/metrics\">/api/metrics</a></p>";
  server.send(200, "text/html", info);
}
static void handleTemp() {
  float t = readTempC();
  if (isnan(t)) { server.send(500, "application/json", "{\"ok\":false,\"err\":\"sensor_read_failed\"}"); return; }
  String j = String("{\"ok\":true,\"temp_c\":")+String(t,2)+",\"temp_f\":"+String(t*9.0/5.0+32.0,2)+"}";
  server.send(200,"application/json",j);
}
static void handleMetrics() { 
  float t = readTempC(), h = readHumidity();
  if (isnan(t) || isnan(h)) { server.send(500,"application/json","{\"ok\":false,\"err\":\"sensor_read_failed\"}"); return; }
  String j = String("{\"ok\":true,\"temp_c\":")+String(t,2)+",\"humidity\":"+String(h,1)+"}";
  server.send(200,"application/json",j);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== DHT auto-detect starting (D4/D5/D2 × DHT11/DHT22) ===");

  // 逐一尝试 pin × type
  bool found = false;
  for (int i=0; i<3 && !found; ++i) {
    for (int j=0; j<2 && !found; ++j) {
      found = try_one(TRY_PINS[i], TRY_TYPES[j]);
    }
  }
  if (!found) {
    Serial.println("[FAIL] DHT not found on D4/D5/D2 with DHT11/DHT22.");
    Serial.println("       Check: VCC=3V3, GND, DATA, 10k pull-up (多数小板自带)。");
  } else {
    Serial.printf("[LOCKED] Use %s, %s\n", pinName(g_pin), typeName(g_type));
  }

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s", WIFI_SSID);
  while (WiFi.status()!=WL_CONNECTED){ delay(500); Serial.print("."); }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

  // HTTP
  server.on("/", handleRoot);
  server.on("/api/temp", handleTemp);
  server.on("/api/metrics", handleMetrics);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  // 串口周期输出（≥2.5s 间隔）
  static uint32_t last=0;
  if (millis()-last > 2500) {
    last = millis();
    float t = readTempC(), h = readHumidity();
    if (isnan(t) || isnan(h)) Serial.println("[DHT] read failed");
    else Serial.printf("[DHT] T=%.2f °C  H=%.1f %%  (pin=%s, type=%s)\n", t, h, pinName(g_pin), typeName(g_type));
  }
}
