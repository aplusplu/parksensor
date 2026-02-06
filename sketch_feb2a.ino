#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoWebsockets.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WebServer.h>

using namespace websockets;

// ---------------- WIFI ----------------
const char* WIFI_SSID = "MediaCollege";
const char* WIFI_PASS = "vms12345";

// ---------------- MDNS ----------------
const char* MDNS_NAME = "esp32-distance"; // -> esp32-distance.local

// ---------------- WS ------------------
WebsocketsServer wsServer;
const uint16_t WS_PORT = 81;

static const int MAX_CLIENTS = 5;
WebsocketsClient clients[MAX_CLIENTS];
bool clientUsed[MAX_CLIENTS] = {false};

// ---------------- HTTP (debug) --------
WebServer httpServer(80);

// ---------------- OLED ----------------
#define OLED_ADDR 0x3C
#define OLED_RESET -1
Adafruit_SSD1306 display(128, 32, &Wire, OLED_RESET);

// ---------------- URM13 UART ----------
HardwareSerial urm(2); // Serial2
static const uint8_t  SLAVE    = 0x0D;
static const uint16_t REG_DIST = 0x0005; // distance register (we will treat as cm)
static const uint16_t REG_CTRL = 0x0008;

// ---------------- Modbus CRC ----------
uint16_t modbusCRC(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}

bool readExact(uint8_t* buf, size_t n, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  size_t got = 0;
  while (got < n && (millis() - t0) < timeoutMs) {
    if (urm.available()) buf[got++] = (uint8_t)urm.read();
    else delay(1);
  }
  return got == n;
}

void drainUart(uint32_t ms = 5) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (urm.available()) (void)urm.read();
    delay(1);
  }
}

bool modbusRead1(uint8_t addr, uint16_t reg, uint16_t &out) {
  uint8_t req[8] = {
    addr, 0x03,
    (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
    0x00, 0x01,
    0, 0
  };
  uint16_t crc = modbusCRC(req, 6);
  req[6] = crc & 0xFF;
  req[7] = (crc >> 8) & 0xFF;

  drainUart();
  urm.write(req, sizeof(req));
  urm.flush();

  uint8_t resp[7];
  if (!readExact(resp, 7, 300)) return false;

  if (resp[0] != addr || resp[1] != 0x03 || resp[2] != 0x02) return false;

  uint16_t rcrc = (uint16_t)resp[5] | ((uint16_t)resp[6] << 8);
  uint16_t ccrc = modbusCRC(resp, 5);
  if (rcrc != ccrc) return false;

  out = ((uint16_t)resp[3] << 8) | resp[4];
  return true;
}

bool modbusWrite1(uint8_t addr, uint16_t reg, uint16_t value) {
  uint8_t req[8] = {
    addr, 0x06,
    (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
    (uint8_t)(value >> 8), (uint8_t)(value & 0xFF),
    0, 0
  };
  uint16_t crc = modbusCRC(req, 6);
  req[6] = crc & 0xFF;
  req[7] = (crc >> 8) & 0xFF;

  drainUart();
  urm.write(req, sizeof(req));
  urm.flush();

  uint8_t resp[8];
  if (!readExact(resp, 8, 300)) return false;

  if (resp[0] != addr || resp[1] != 0x06) return false;

  uint16_t rcrc = (uint16_t)resp[6] | ((uint16_t)resp[7] << 8);
  uint16_t ccrc = modbusCRC(resp, 6);
  return rcrc == ccrc;
}

static inline void sort5(uint16_t a[5]) {
  for (int i=0;i<5;i++) for (int j=i+1;j<5;j++) {
    if (a[j] < a[i]) { uint16_t t=a[i]; a[i]=a[j]; a[j]=t; }
  }
}

// ---------------- Distance read (cm) ----
// Reads distance register multiple times and returns median.
// We treat REG_DIST value as centimeters (per URM13 typical behavior/spec resolution 1cm).
bool readDistanceMedianCm(uint16_t &cmOut) {
  for (int attempt=0; attempt<3; attempt++) {
    uint16_t ctrl = 0;
    if (!modbusRead1(SLAVE, REG_CTRL, ctrl)) continue;

    // trigger measurement bit3
    if (!modbusWrite1(SLAVE, REG_CTRL, ctrl | (1 << 3))) continue;

    delay(220);

    uint16_t s[5]; int ok = 0;
    for (int i=0;i<5;i++){
      uint16_t raw = 0;
      if (modbusRead1(SLAVE, REG_DIST, raw) && raw != 0xFFFF && raw != 0) {
        s[ok++] = raw; // raw is treated as cm
      }
      delay(25);
    }

    if (ok < 3) continue;
    while (ok < 5) { s[ok] = s[ok-1]; ok++; }
    sort5(s);
    cmOut = s[2];
    return true;
  }
  return false;
}

// ---------------- OLED UI (cm scale) ----
uint16_t cmMin = 15;   // URM13 spec
uint16_t cmMax = 900;  // URM13 spec

int cmToX(uint16_t cm) {
  if (cm < cmMin) cm = cmMin;
  if (cm > cmMax) cm = cmMax;
  float t = (cm - cmMin) / float(cmMax - cmMin);
  int x = 6 + int(t * 116);
  if (x < 6) x = 6;
  if (x > 122) x = 122;
  return x;
}

void drawOled(uint16_t cm, bool valid, uint32_t tick) {
  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("DIST:");

  if (!valid) {
    display.setCursor(40, 0);
    display.print("N/A");
    display.drawRect(0, 9, 128, 23, SSD1306_WHITE);
    display.display();
    return;
  }

  display.setCursor(40, 0);
  display.print(cm);
  display.print(" cm");

  const int BAR_X = 6, BAR_Y = 20, BAR_W = 116;
  display.drawFastHLine(BAR_X, BAR_Y, BAR_W, SSD1306_WHITE);
  display.drawFastHLine(BAR_X, BAR_Y+1, BAR_W, SSD1306_WHITE);

  int x = cmToX(cm);
  bool pulse = ((tick / 250) % 2) == 0;
  display.drawFastVLine(x, BAR_Y-6, 12, SSD1306_WHITE);
  if (pulse) display.fillCircle(x, BAR_Y-8, 3, SSD1306_WHITE);
  else       display.drawCircle(x, BAR_Y-8, 3, SSD1306_WHITE);

  display.drawRect(0, 9, 128, 23, SSD1306_WHITE);
  display.display();
}

// ---------------- WS helpers ----------
void acceptNewClients() {
  if (!wsServer.available()) return;
  WebsocketsClient c = wsServer.accept();

  for (int i=0;i<MAX_CLIENTS;i++){
    if (!clientUsed[i]) {
      clients[i] = c;
      clientUsed[i] = true;

      clients[i].onEvent([i](WebsocketsEvent e, String data) {
        (void)data;
        if (e == WebsocketsEvent::ConnectionClosed) clientUsed[i] = false;
      });

      Serial.print("WS client connected slot=");
      Serial.println(i);
      return;
    }
  }
  c.close();
}

void pollClients() {
  for (int i=0;i<MAX_CLIENTS;i++){
    if (clientUsed[i]) clients[i].poll();
  }
}

void wsBroadcast(uint16_t cm, bool valid) {
  char buf[96];
  if (!valid) {
    snprintf(buf, sizeof(buf), "{\"ok\":0}");
  } else {
    uint16_t mm = cm * 10; // simple derived value for UI
    snprintf(buf, sizeof(buf), "{\"ok\":1,\"cm\":%u,\"mm\":%u}", cm, mm);
  }

  for (int i=0;i<MAX_CLIENTS;i++){
    if (!clientUsed[i]) continue;
    if (!clients[i].available()) { clientUsed[i] = false; continue; }
    clients[i].send(buf);
  }
}

// ---------------- Setup/Loop ----------
uint32_t lastSample = 0;

void setup() {
  Serial.begin(115200);
  delay(200);

  // URM13 UART: RX2=16 TX2=17
  urm.begin(19200, SERIAL_8N1, 16, 17);

  // OLED I2C: SDA=21 SCL=22
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
  }
  display.clearDisplay();
  display.display();

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print("."); }
  Serial.println();
  Serial.print("WiFi OK. IP: ");
  Serial.println(WiFi.localIP());

  // mDNS (may not resolve on Windows, but ok)
  if (MDNS.begin(MDNS_NAME)) {
    Serial.print("mDNS OK: ");
    Serial.print(MDNS_NAME);
    Serial.println(".local");
  } else {
    Serial.println("mDNS failed");
  }

  // HTTP debug
  httpServer.on("/ping", []() { httpServer.send(200, "text/plain", "pong"); });
  httpServer.on("/", []() {
    String s;
    s += "ESP32 Distance Server\n";
    s += "IP: " + WiFi.localIP().toString() + "\n";
    s += "WS: ws://" + WiFi.localIP().toString() + ":81\n";
    s += "mDNS: ws://" + String(MDNS_NAME) + ".local:81\n";
    httpServer.send(200, "text/plain", s);
  });
  httpServer.begin();
  Serial.println("HTTP on :80 (/ping)");

  // WS
  wsServer.listen(WS_PORT);
  Serial.print("WS on ws://");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(WS_PORT);
}

void loop() {
  httpServer.handleClient();
  acceptNewClients();
  pollClients();

  // sample ~5Hz
  if (millis() - lastSample < 200) return;
  lastSample = millis();

  uint16_t cm = 0;
  bool ok = readDistanceMedianCm(cm);

  drawOled(cm, ok, millis());
  wsBroadcast(cm, ok);

  if (ok) {
    Serial.print("Distance: ");
    Serial.print(cm);
    Serial.println(" cm");
  } else {
    Serial.println("Distance: N/A");
  }
}
