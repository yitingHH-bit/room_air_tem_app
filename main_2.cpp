#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
// #include <ESP8266mDNS.h>  // 如需用 http://esp8266.local 打开可以启用

// ==== Wi-Fi（改成你的 2.4GHz SSID/密码）====
const char* WIFI_SSID = "TP-LINK_697F";
const char* WIFI_PASS = "hjc198902165399";
// ==== DHT11 固定在 D2(GPIO4) ====
#define DHT_PIN 4
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);
ESP8266WebServer server(80);

// ---- 网页面板（放全局 + PROGMEM）----
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="zh"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP8266 温湿度</title>
<style>
  :root{--bd:#e5e7eb;--tx:#111827;--mut:#6b7280}
  body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,Helvetica,Arial;
       color:var(--tx);max-width:560px;margin:24px auto;padding:0 12px}
  .card{border:1px solid var(--bd);border-radius:14px;padding:16px;
        box-shadow:0 2px 10px rgba(0,0,0,.06)}
  h1{font-size:20px;margin:0 0 12px}
  .row{display:flex;gap:12px}
  .tile{flex:1;border:1px solid var(--bd);border-radius:12px;padding:12px;text-align:center}
  .big{font-size:28px;font-weight:700;margin:6px 0}
  .mut{color:var(--mut);font-size:12px}
  button{padding:8px 12px;border-radius:10px;border:1px solid var(--bd);background:#fafafa;cursor:pointer}
</style>
<div class="card">
  <h1>ESP8266 温湿度面板</h1>
  <div class="row">
    <div class="tile"><div>温度</div><div id="t" class="big">--.- °C</div></div>
    <div class="tile"><div>湿度</div><div id="h" class="big">--.- %</div></div>
  </div>
  <div style="margin-top:12px;display:flex;justify-content:space-between;align-items:center">
    <div id="status" class="mut">等待更新…</div>
    <button onclick="refresh()">立即刷新</button>
  </div>
</div>
<script>
async function refresh(){
  const s=document.getElementById('status');
  try{
    s.textContent='获取中…';
    const r=await fetch('/api/metrics',{cache:'no-store'});
    if(!r.ok) throw new Error('HTTP '+r.status);
    const j=await r.json();
    document.getElementById('t').textContent=j.temp_c.toFixed(2)+' °C';
    document.getElementById('h').textContent=j.humidity.toFixed(1)+' %';
    s.textContent='最后更新：'+new Date().toLocaleTimeString();
  }catch(e){ s.textContent='读取失败：'+e.message; }
}
setInterval(refresh, 2500);
refresh();
</script>
)HTML";

// ---- 读取封装（NaN 保护）----
static float readTempC()    { float t = dht.readTemperature(); return isnan(t)?NAN:t; }
static float readHumidity() { float h = dht.readHumidity();    return isnan(h)?NAN:h; }

// ---- HTTP handlers ----
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);   // 从 PROGMEM 发送
}
void handleTemp(){
  float t = readTempC();
  if (isnan(t)) { server.send(500,"application/json","{\"ok\":false,\"err\":\"sensor_read_failed\"}"); return; }
  String j = String("{\"ok\":true,\"temp_c\":")+String(t,2)+",\"temp_f\":"+String(t*9.0/5.0+32.0,2)+"}";
  server.send(200,"application/json",j);
}
void handleMetrics(){
  float t = readTempC(), h = readHumidity();
  if (isnan(t) || isnan(h)) { server.send(500,"application/json","{\"ok\":false,\"err\":\"sensor_read_failed\"}"); return; }
  String j = String("{\"ok\":true,\"temp_c\":")+String(t,2)+",\"humidity\":"+String(h,1)+"}";
  server.send(200,"application/json",j);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // DHT 初始化
  pinMode(DHT_PIN, INPUT_PULLUP);
  dht.begin();
  for(int i=0;i<2;i++){ dht.readTemperature(); dht.readHumidity(); delay(1200); }
  Serial.println("DHT initialized (pin=D2/GPIO4, type=DHT11)");

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

  // if (MDNS.begin("esp8266")) Serial.println("mDNS: http://esp8266.local/");

  // HTTP 路由
  server.on("/", handleRoot);
  server.on("/api/temp", handleTemp);
  server.on("/api/metrics", handleMetrics);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  static uint32_t last=0;
  if (millis()-last > 2500) {
    last = millis();
    float t=readTempC(), h=readHumidity();
    if (isnan(t)||isnan(h)) Serial.println("[DHT] read failed");
    else Serial.printf("[DHT] T=%.2f °C  H=%.1f %%\n", t, h);
    yield();
  }
}
