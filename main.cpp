#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <ESP8266mDNS.h>  // optional mDNS (http://esp8266.local)

// ====== Cloud config (EDIT ME) ======
const char* CLOUD_URL = "https://<your-webhook-or-api>";   // e.g., DataFire/Zapier/your API
const char* CLOUD_KEY = "YOUR_SECRET_TOKEN";                // leave empty if not required
const uint32_t CLOUD_PERIOD_MS = 30000;                     // periodic upload interval (ms)

// ====== Device / Wi-Fi ======
const char* DEVICE_ID  = "esp8266-001";
// Current Wi-Fi
const char* WIFI_SSID = "TP-LINK_697F";
const char* WIFI_PASS = "hjc198902165399";

// (Optional) Static IP — uncomment and adjust if you want a fixed IP
// IPAddress local(192,168,0,50);
// IPAddress gateway(192,168,0,1);
// IPAddress subnet(255,255,255,0);
// IPAddress dns(192,168,0,1);

// ====== DHT sensor on D2 (GPIO4) ======
#define DHT_PIN 4
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

ESP8266WebServer server(80);

// ====== Time helpers (UTC ISO-8601) ======
static time_t nowUTC() { return time(nullptr); }
static String iso8601UTC() {
  time_t t = nowUTC();
  if (t <= 100000) return String("");  // not synced yet
  struct tm *tm = gmtime(&t);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
  return String(buf);
}

// ====== JSON builder (stack buffer, avoids String fragmentation) ======
static String makeMetricsJson(const char* deviceId, const String& ts, float t, float h, const char* aqiLiteral /* "null" or number as string */) {
  char buf[192];
  if (ts.length()) {
    snprintf(buf, sizeof(buf),
      "{\"device_id\":\"%s\",\"ts\":\"%s\",\"temp_c\":%.2f,\"rh\":%.1f,\"aqi\":%s}",
      deviceId, ts.c_str(), t, h, aqiLiteral);
  } else {
    snprintf(buf, sizeof(buf),
      "{\"device_id\":\"%s\",\"ts\":null,\"temp_c\":%.2f,\"rh\":%.1f,\"aqi\":%s}",
      deviceId, t, h, aqiLiteral);
  }
  return String(buf);
}

// ====== UI (React embedded) ======
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP8266 Temperature and Humidity</title>
<style>
  :root{--bd:#e5e7eb;--tx:#111827;--mut:#6b7280}
  body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,Helvetica,Arial;
       color:var(--tx);max-width:720px;margin:24px auto;padding:0 12px}
  .card{border:1px solid var(--bd);border-radius:14px;padding:16px;
        box-shadow:0 2px 10px rgba(0,0,0,.06)}
  h1{font-size:20px;margin:0 0 6px}
  .mut{color:var(--mut);font-size:12px}
  .row{display:flex;gap:12px;margin-top:8px}
  .tile{flex:1;border:1px solid var(--bd);border-radius:12px;padding:12px;text-align:center}
  .big{font-size:28px;font-weight:700;margin:6px 0}
  button{padding:8px 12px;border-radius:10px;border:1px solid var(--bd);background:#fafafa;cursor:pointer}
  table{width:100%;border-collapse:collapse;margin-top:14px}
  th,td{padding:10px;border-bottom:1px solid var(--bd);text-align:left;font-size:14px}
  th{font-weight:600}
</style>
<div class="card">
  <h1>ESP8266 Temperature and Humidity Panel</h1>
  <div id="ip" class="mut">IP: —</div>

  <div class="row">
    <div class="tile"><div>Temperature</div><div id="t" class="big">--.- °C</div></div>
    <div class="tile"><div>Humidity</div><div id="h" class="big">--.- %</div></div>
  </div>

  <div style="margin-top:12px;display:flex;justify-content:space-between;align-items:center">
    <div id="status" class="mut">waiting for update…</div>
    <button onclick="refresh()">refresh now</button>
  </div>

  <div id="react-root" style="margin-top:16px"></div>
</div>

<script>
async function showIP(){
  try{
    const r = await fetch('/api/info',{cache:'no-store'});
    if(!r.ok) throw new Error('HTTP '+r.status);
    const j = await r.json();
    document.getElementById('ip').textContent = 'IP: ' + (j.ip || '—');
  }catch(e){
    document.getElementById('ip').textContent = 'IP: ' + (location.hostname || '(unavailable)');
  }
}

async function refresh(){
  const s=document.getElementById('status');
  try{
    s.textContent='getting…';
    const r=await fetch('/api/metrics',{cache:'no-store'});
    if(!r.ok) throw new Error('HTTP '+r.status);
    const j=await r.json();
    document.getElementById('t').textContent=(j.temp_c??NaN).toFixed(2)+' °C';
    document.getElementById('h').textContent=(j.rh??NaN).toFixed(1)+' %';
    s.textContent='last update: '+new Date().toLocaleTimeString();
  }catch(e){ s.textContent='read failed: '+e.message; }
}
setInterval(refresh, 2500);
showIP();
refresh();
</script>

<script crossorigin src="https://unpkg.com/react@18/umd/react.production.min.js"></script>
<script crossorigin src="https://unpkg.com/react-dom@18/umd/react-dom.production.min.js"></script>
<script>
const e = React.createElement;

function Panel(){
  const [data, setData] = React.useState(null);
  const [err, setErr] = React.useState("");
  const [pushMsg, setPushMsg] = React.useState("");

  const fetchOnce = async () => {
    try{
      setErr("");
      const r = await fetch('/api/metrics',{cache:'no-store'});
      if(!r.ok) throw new Error('HTTP '+r.status);
      setData(await r.json());
    }catch(ex){ setErr(ex.message); }
  };

  const pushNow = async () => {
    try{
      setPushMsg("Uploading…");
      const r = await fetch('/api/push', { method:'POST' });
      const j = await r.json().catch(()=>({}));
      setPushMsg(j.ok ? "Uploaded ✅" : "Upload failed ❌");
    }catch(ex){
      setPushMsg("Upload failed: "+ex.message);
    }finally{
      setTimeout(()=>setPushMsg(""), 2000);
    }
  };

  React.useEffect(()=>{
    fetchOnce();
    const id = setInterval(fetchOnce, 2500);
    return ()=> clearInterval(id);
  },[]);

  const cell = (v)=> (v==null ? '—' : v);
  const fix2 = (v)=> (v==null ? '—' : Number(v).toFixed(2));
  const fix1 = (v)=> (v==null ? '—' : Number(v).toFixed(1));

  return e('div', null,
    e('div', {style:{display:'flex',gap:10,alignItems:'center',justifyContent:'space-between'}},
      e('div', {className:'mut', style:{fontSize:12}}, 'Auto-refresh every 2.5s'),
      e('div', null,
        e('button', {onclick: fetchOnce}, 'Refresh now'),
        e('button', {onclick: pushNow, style:{marginLeft:8}}, 'Upload to Cloud Now')
      ),
    ),
    pushMsg && e('div', {className:'mut', style:{marginTop:6}}, pushMsg),
    err && e('div', {style:{color:'#b91c1c',marginTop:6}}, 'Error: '+err),
    e('table', null,
      e('thead', null, e('tr', null,
        e('th', null, 'device_id'),
        e('th', null, 'ts (UTC)'),
        e('th', null, 'temp_c'),
        e('th', null, 'rh (%)'),
        e('th', null, 'aqi')
      )),
      e('tbody', null, e('tr', null,
        e('td', null, cell(data?.device_id)),
        e('td', null, cell(data?.ts)),
        e('td', null, fix2(data?.temp_c)),
        e('td', null, fix1(data?.rh)),
        e('td', null, cell(data?.aqi))
      )))
  );
}
ReactDOM.createRoot(document.getElementById('react-root')).render(e(Panel));
</script>
)HTML";

// ====== Sensors ======
static float readTempC()    { float t = dht.readTemperature(); return isnan(t)?NAN:t; }
static float readHumidity() { float h = dht.readHumidity();    return isnan(h)?NAN:h; }

// ====== Cloud upload ======
bool postToCloud(float t, float h, const String& isoTs) {
  if (!CLOUD_URL || strlen(CLOUD_URL) == 0) return false;

  WiFiClientSecure client;
  client.setTimeout(5000);   // keep UI responsive even if cloud is slow
  client.setInsecure();      // DEV ONLY (load CA/fingerprint for production)

  HTTPClient http;
  if (!http.begin(client, CLOUD_URL)) return false;

  http.addHeader("Content-Type", "application/json");
  if (CLOUD_KEY && strlen(CLOUD_KEY)) {
    http.addHeader("Authorization", String("Bearer ") + CLOUD_KEY);
  }

  String body = makeMetricsJson(DEVICE_ID, isoTs, t, h, "null");
  int code = http.POST(body);
  http.end();
  Serial.printf("[CLOUD] POST %s -> %d\n", CLOUD_URL, code);
  return code >= 200 && code < 300;
}

// ====== HTTP handlers ======
void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleTemp(){  // legacy quick test
  float t = readTempC();
  if (isnan(t)) { server.send(500,"application/json","{\"ok\":false,\"err\":\"sensor_read_failed\"}"); return; }
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"temp_c\":%.2f,\"temp_f\":%.2f}", t, t*9.0/5.0+32.0);
  server.send(200,"application/json", buf);
}

// Unified schema endpoint used by the page and cloud
void handleMetrics(){
  float t = readTempC(), h = readHumidity();
  if (isnan(t) || isnan(h)) { server.send(500,"application/json","{\"ok\":false,\"err\":\"sensor_read_failed\"}"); return; }
  String ts = iso8601UTC(); // may be empty if NTP not synced yet
  String j  = makeMetricsJson(DEVICE_ID, ts, t, h, "null");
  server.send(200,"application/json", j);
}

// Manual cloud upload trigger (POST /api/push)
void handlePush() {
  float t = readTempC(), h = readHumidity();
  if (isnan(t) || isnan(h)) { server.send(500, "application/json", "{\"ok\":false,\"err\":\"sensor_read_failed\"}"); return; }
  String ts = iso8601UTC();
  bool ok = postToCloud(t, h, ts);
  server.send(200, "application/json", String("{\"ok\":") + (ok?"true":"false") + "}");
}

// Device info endpoint (IP/SSID/device_id)
void handleInfo() {
  IPAddress ip = WiFi.localIP();
  String ssid = WiFi.SSID();
  char buf[160];
  snprintf(buf, sizeof(buf),
    "{\"device_id\":\"%s\",\"ip\":\"%u.%u.%u.%u\",\"ssid\":\"%s\"}",
    DEVICE_ID, ip[0],ip[1],ip[2],ip[3], ssid.c_str());
  server.send(200, "application/json", buf);
}

// Diagnostics: Wi-Fi status and addressing
void handleDiag(){
  IPAddress ip = WiFi.localIP(), gw = WiFi.gatewayIP(), sn = WiFi.subnetMask(), dns = WiFi.dnsIP();
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"status\":%d,\"ssid\":\"%s\",\"ip\":\"%s\",\"gateway\":\"%s\",\"subnet\":\"%s\",\"dns\":\"%s\",\"rssi\":%d}",
    WiFi.status(), WiFi.SSID().c_str(),
    ip.toString().c_str(), gw.toString().c_str(), sn.toString().c_str(), dns.toString().c_str(), WiFi.RSSI());
  server.send(200, "application/json", buf);
}

// Health check and 404 logger
void handlePing(){ server.send(200, "text/plain", "pong"); }
void handleNotFound() {
  Serial.printf("[HTTP] 404: %s\n", server.uri().c_str());
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // DHT warm-up (DHT22: keep sample rate <= 0.5–1 Hz)
  pinMode(DHT_PIN, INPUT_PULLUP);
  dht.begin();
  for(int i=0;i<2;i++){ dht.readTemperature(); dht.readHumidity(); delay(1200); }
  Serial.println("DHT initialized (pin=D2/GPIO4, type=DHT22)");

  // Wi-Fi: STA + AP (so you always have a back door at 192.168.4.1)
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP_STA);

  // (Optional) Static IP before begin()
  // WiFi.config(local, gateway, subnet, dns);

  // Connect STA
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(500); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    Serial.printf("\nConnected! IP: %u.%u.%u.%u\n", ip[0],ip[1],ip[2],ip[3]);
  } else {
    Serial.println("\nSTA connect timed out.");
  }

  // Bring up a fallback AP (always available)
  WiFi.softAP("esp8266-setup", "12345678");
  IPAddress apIP(192,168,4,1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0));
  Serial.println("SoftAP up: SSID=esp8266-setup  PASS=12345678  URL: http://192.168.4.1/");

  // ---- Register routes and start HTTP server (MUST happen before NTP/mDNS) ----
  server.enableCORS(true);
  server.on("/", handleRoot);
  server.on("/api/temp", handleTemp);
  server.on("/api/metrics", handleMetrics);
  server.on("/api/info", handleInfo);
  server.on("/api/push", HTTP_POST, handlePush);
  server.on("/diag", handleDiag);
  server.on("/ping", handlePing);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");

  // NTP (UTC) — done after server is up to avoid blocking access
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("NTP syncing...");
  for (int i = 0; i < 20 && nowUTC() <= 100000; ++i) { delay(250); }

  // Optional mDNS (Windows may need Bonjour)
  if (MDNS.begin("esp8266")) {
    Serial.println("mDNS: http://esp8266.local/");
  }
}

void loop() {
  server.handleClient();

  // periodic serial log of sensor values
  static uint32_t lastPrint=0, lastCloud=0;
  if (millis()-lastPrint > 2500) {
    lastPrint = millis();
    float t=readTempC(), h=readHumidity();
    if (isnan(t)||isnan(h)) Serial.println("[DHT] read failed");
    else Serial.printf("[DHT] T=%.2f °C  H=%.1f %%\n", t, h);
    yield();
  }

  // periodic cloud upload
  if (millis() - lastCloud > CLOUD_PERIOD_MS) {
    lastCloud = millis();
    float t=readTempC(), h=readHumidity();
    if (!isnan(t) && !isnan(h)) {
      String ts = iso8601UTC();
      postToCloud(t, h, ts);
    }
  }
}

