/*
  ESP32-CAM (AI-Thinker) + ST7735 160x128 + SD card + button + Wi-Fi AP
  - Live preview on the TFT: whole camera image rotated 90 deg and fitted
    to the screen (letterbox, black bars, NO cropping)
  - Short press on GPIO12 button = save the full image to the SD card
    (same orientation and field of view as the preview, 480x640 portrait)
  - Long press (LONG_PRESS_MS) = turn the Wi-Fi access point on / off
    Wi-Fi is OFF at boot. While it is ON the preview is paused.
    Connect with a phone and open http://192.168.4.1 to view / download /
    delete the photos on the SD card.
  - Camera is initialized ONCE and never re-initialized

  Arduino IDE: Board "AI Thinker ESP32-CAM", PSRAM = Enabled
*/

#include "esp_camera.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include <WebServer.h>
#include <vector>
#include <algorithm>

// ---------- Pins ----------
#define TFT_SCK   14
#define TFT_MOSI  15
#define SD_MISO    2
#define SD_CS     13
#define TFT_CS     1
#define TFT_DC     3
#define BTN_PIN   12

// ---------- Settings ----------
#define FLIP_V 0                       // set to 1 if image is upside down (also affects photos)
#define FLIP_H 0                       // set to 1 if image is mirrored (also affects photos)
#define PREVIEW_SIZE FRAMESIZE_QQVGA   // 160x120 source for the preview (works reliably on this board)
#define PHOTO_SIZE   FRAMESIZE_VGA     // 640x480 source (saved rotated as 480x640)
#define JPEG_QUALITY 85                // 0-100, higher = better
#define FAST_BLIT    1                 // 1 = fast block transfer, 0 = slow fallback (drawRGBBitmap)

#define SCR_W 160                      // landscape (setRotation(1))
#define SCR_H 128

#define AP_SSID "ESP32-CAM"            // Wi-Fi name
#define AP_PASS "12345678"             // at least 8 characters
#define LONG_PRESS_MS 1500             // hold the button this long to toggle Wi-Fi

// ---------- AI-Thinker ESP32-CAM camera pins ----------
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

SPIClass spiBus(HSPI);                              // shared by TFT and SD
Adafruit_ST7735 tft(&spiBus, TFT_CS, TFT_DC, -1);   // RST tied to 3V3
WebServer server(80);

static uint16_t frameBuf[SCR_W * SCR_H];            // one full screen frame (40 KB)

framesize_t previewSize = PREVIEW_SIZE;   // auto-falls back to VGA if no frames arrive
int picCounter = 1;
bool sdOK = false;
bool wifiActive = false;
esp_err_t camErr = ESP_OK;

// ---------- Helpers ----------
void showMessage(const char *l1, const char *l2 = "") {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 50);
  tft.println(l1);
  tft.setCursor(10, 70);
  tft.println(l2);
}

// ---------- Camera ----------
// Buffers are allocated for the biggest size (PHOTO_SIZE) once.
// Afterwards only the frame size is switched, which needs no re-init.
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size   = PHOTO_SIZE;
  config.jpeg_quality = 10;
  config.fb_count     = 1;                        // 1 = reliable with frame size switching
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  camErr = esp_camera_init(&config);
  if (camErr != ESP_OK) return false;

  sensor_t *s = esp_camera_sensor_get();
  if (!s) return false;
  s->set_vflip(s, FLIP_V);
  s->set_hmirror(s, FLIP_H);
  s->set_framesize(s, previewSize);
  return true;
}

// ---------- Preview ----------
// Rotates the whole frame 90 degrees and scales it to FIT the screen.
// No cropping: unused screen area stays black (letterbox).
// Mapping: rotated pixel (X, Y) = source pixel (col Y, row h-1-X).
// Returns false if the camera gave no frame.
bool drawPreview() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;

  const uint16_t *src = (const uint16_t *)fb->buf;
  const int w = fb->width;
  const int h = fb->height;

  // Rotated image size
  const int RW = h;
  const int RH = w;

  // Fitted size on screen
  int dW, dH;
  if (RW * SCR_H <= RH * SCR_W) {      // limited by screen height
    dH = SCR_H;
    dW = (RW * SCR_H) / RH;
  } else {                             // limited by screen width
    dW = SCR_W;
    dH = (RH * SCR_W) / RW;
  }
  const int ox = (SCR_W - dW) / 2;
  const int oy = (SCR_H - dH) / 2;

  int yMap[SCR_H];
  for (int dy = 0; dy < dH; dy++) yMap[dy] = (dy * RH) / dH;

  memset(frameBuf, 0, sizeof(frameBuf));   // black bars

  for (int dx = 0; dx < dW; dx++) {
    const int X = (dx * RW) / dW;
    const uint16_t *rowPtr = src + (size_t)(h - 1 - X) * w;
    for (int dy = 0; dy < dH; dy++) {
      uint16_t c = rowPtr[yMap[dy]];
#if FAST_BLIT
      frameBuf[(oy + dy) * SCR_W + ox + dx] = c;           // camera byte order is already what the TFT wants
#else
      frameBuf[(oy + dy) * SCR_W + ox + dx] = (uint16_t)((c << 8) | (c >> 8));
#endif
    }
  }
  esp_camera_fb_return(fb);

#if FAST_BLIT
  tft.startWrite();
  tft.setAddrWindow(0, 0, SCR_W, SCR_H);
  spiBus.writeBytes((const uint8_t *)frameBuf, SCR_W * SCR_H * 2);
  tft.endWrite();
#else
  tft.drawRGBBitmap(0, 0, frameBuf, SCR_W, SCR_H);
#endif
  return true;
}

// ---------- Photo ----------
// Saves the whole frame rotated 90 degrees (same orientation and same
// field of view as the preview), at full capture resolution.
void capturePhoto() {
  if (!sdOK) {
    showMessage("No SD card!");
    delay(1500);
    tft.fillScreen(ST77XX_BLACK);
    return;
  }

  showMessage("Taking photo...");

  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, PHOTO_SIZE);

  // Throw away a few frames so the new size and exposure settle
  for (int i = 0; i < 4; i++) {
    camera_fb_t *f = esp_camera_fb_get();
    if (f) esp_camera_fb_return(f);
    delay(30);
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    showMessage("Capture failed");
    s->set_framesize(s, previewSize);
    delay(1500);
    tft.fillScreen(ST77XX_BLACK);
    return;
  }

  uint8_t *jpgBuf = NULL;
  size_t jpgLen = 0;
  bool ok = false;

  const int w = fb->width;    // 640
  const int h = fb->height;   // 480
  const int outW = h;         // 480
  const int outH = w;         // 640

  uint16_t *out = (uint16_t *)ps_malloc((size_t)outW * outH * 2);

  if (out) {
    const uint16_t *src = (const uint16_t *)fb->buf;
    // Same mapping as the preview: rotated (X, Y) = source (col Y, row h-1-X)
    for (int Y = 0; Y < outH; Y++) {
      for (int X = 0; X < outW; X++) {
        out[(size_t)Y * outW + X] = src[(size_t)(h - 1 - X) * w + Y];
      }
    }
    esp_camera_fb_return(fb);

    ok = fmt2jpg((uint8_t *)out, (size_t)outW * outH * 2, outW, outH,
                 PIXFORMAT_RGB565, JPEG_QUALITY, &jpgBuf, &jpgLen);
    free(out);
  } else {
    esp_camera_fb_return(fb);
  }

  s->set_framesize(s, previewSize);   // back to the fast preview size

  if (!ok) {
    showMessage("JPEG convert failed");
  } else {
    char path[24];
    do {
      snprintf(path, sizeof(path), "/pic_%04d.jpg", picCounter++);
    } while (SD.exists(path));

    File file = SD.open(path, FILE_WRITE);
    if (file) {
      file.write(jpgBuf, jpgLen);
      file.close();
      showMessage("Saved!", path + 1);
    } else {
      showMessage("SD write failed");
    }
    free(jpgBuf);
  }

  delay(1200);
  tft.fillScreen(ST77XX_BLACK);
}

// ---------- Web server (photo download) ----------
// Only plain names like pic_0001.jpg are accepted (no paths).
bool validName(const String &n) {
  return n.startsWith("pic_") && n.endsWith(".jpg") &&
         n.indexOf('/') < 0 && n.indexOf("..") < 0;
}

void handleRoot() {
  std::vector<std::pair<String, size_t>> files;

  if (sdOK) {
    File root = SD.open("/");
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        String n = f.name();
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);   // older cores return the full path
        if (validName(n)) files.push_back({n, (size_t)f.size()});
      }
      f.close();
      f = root.openNextFile();
    }
    root.close();
  }

  // newest first
  std::sort(files.begin(), files.end(),
            [](const std::pair<String, size_t> &a, const std::pair<String, size_t> &b) {
              return a.first > b.first;
            });

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent(
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32-CAM</title><style>"
    "body{font-family:sans-serif;margin:0;padding:12px;background:#111;color:#eee}"
    "h2{margin:4px 0 12px}"
    ".row{background:#222;border-radius:10px;padding:10px;margin-bottom:8px}"
    ".name{font-weight:bold}.size{color:#999;font-size:13px}"
    "a{display:inline-block;margin:8px 8px 0 0;padding:8px 14px;border-radius:8px;"
    "background:#2d7ff9;color:#fff;text-decoration:none}"
    "a.del{background:#c0392b}a.dl{background:#27ae60}"
    "</style></head><body><h2>ESP32-CAM Fotograflar</h2>");

  if (!sdOK) {
    server.sendContent("<p>SD kart bulunamadi.</p>");
  } else if (files.empty()) {
    server.sendContent("<p>Henuz fotograf yok.</p>");
  } else {
    char line[512];
    for (auto &p : files) {
      snprintf(line, sizeof(line),
        "<div class='row'><div class='name'>%s</div><div class='size'>%u KB</div>"
        "<a href='/view?f=%s'>Goruntule</a>"
        "<a class='dl' href='/dl?f=%s'>Indir</a>"
        "<a class='del' href='/del?f=%s' onclick=\"return confirm('Silinsin mi?')\">Sil</a></div>",
        p.first.c_str(), (unsigned)(p.second / 1024),
        p.first.c_str(), p.first.c_str(), p.first.c_str());
      server.sendContent(line);
    }
  }
  server.sendContent("</body></html>");
  server.sendContent("");   // end of chunked response
}

void sendPhoto(bool download) {
  String n = server.arg("f");
  if (!sdOK || !validName(n)) {
    server.send(400, "text/plain", "Bad request");
    return;
  }
  File f = SD.open("/" + n, FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  if (download) server.sendHeader("Content-Disposition", "attachment; filename=\"" + n + "\"");
  server.streamFile(f, "image/jpeg");
  f.close();
}

void handleView() { sendPhoto(false); }
void handleDl()   { sendPhoto(true); }

void handleDelete() {
  String n = server.arg("f");
  if (sdOK && validName(n)) SD.remove("/" + n);
  server.sendHeader("Location", "/");
  server.send(303);
}

// ---------- Wi-Fi on / off ----------
void setupWebRoutes() {
  server.on("/", handleRoot);
  server.on("/view", handleView);
  server.on("/dl", handleDl);
  server.on("/del", handleDelete);
}

// Shown on the TFT while Wi-Fi is on (preview is paused)
void showWifiScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 25);
  tft.println("WiFi ACIK");
  tft.setCursor(10, 45);
  tft.println(AP_SSID);
  tft.setCursor(10, 60);
  tft.println("Sifre: " AP_PASS);
  tft.setCursor(10, 75);
  tft.println(WiFi.softAPIP().toString());
  tft.setCursor(10, 100);
  tft.println("Kapat: uzun bas");
}

void startWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 2);   // channel 1, visible, max 2 clients
  delay(200);
  server.begin();
  wifiActive = true;
  showWifiScreen();
}

void stopWiFi() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);                      // radio fully off, no preview slowdown
  wifiActive = false;
  showMessage("WiFi kapandi");
  delay(800);
  tft.fillScreen(ST77XX_BLACK);
}

// ---------- Setup / Loop ----------
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // disable brownout reset

  pinMode(BTN_PIN, INPUT_PULLUP);

  spiBus.begin(TFT_SCK, SD_MISO, TFT_MOSI, -1);

  tft.initR(INITR_BLACKTAB);                   // try INITR_GREENTAB if edges look wrong
  tft.setRotation(1);                          // landscape 160x128

  // Screen self-test: you should see red, green, blue
  tft.fillScreen(ST77XX_RED);   delay(250);
  tft.fillScreen(ST77XX_GREEN); delay(250);
  tft.fillScreen(ST77XX_BLUE);  delay(250);
  showMessage("Starting...");

  sdOK = SD.begin(SD_CS, spiBus, 10000000);

  if (!initCamera()) {
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%x", camErr);
    showMessage("Camera init failed", buf);
    while (true) delay(1000);
  }

  if (!sdOK) {
    showMessage("SD card not found");
    delay(1500);
  }

  setupWebRoutes();                            // Wi-Fi stays OFF until a long press
  tft.fillScreen(ST77XX_BLACK);
}

void loop() {
  static uint8_t noFrame = 0;

  if (wifiActive) {
    server.handleClient();
    delay(5);                       // preview is paused while Wi-Fi is on
  } else {
    if (drawPreview()) {
      noFrame = 0;
    } else {
      noFrame++;
      if (noFrame == 2 && previewSize != FRAMESIZE_VGA) {
        // this preview size gives no frames: fall back to VGA
        previewSize = FRAMESIZE_VGA;
        sensor_t *s = esp_camera_sensor_get();
        s->set_framesize(s, previewSize);
        noFrame = 0;
      } else if (noFrame == 3) {
        showMessage("No camera frame");
      }
    }
  }

  // Button: short press (on release) = photo, long press = Wi-Fi on/off
  static bool btnDown = false;
  static bool longHandled = false;
  static uint32_t pressStart = 0;
  bool pressed = (digitalRead(BTN_PIN) == LOW);

  if (pressed && !btnDown) {
    btnDown = true;
    longHandled = false;
    pressStart = millis();
  } else if (pressed && btnDown && !longHandled && millis() - pressStart >= LONG_PRESS_MS) {
    longHandled = true;
    if (wifiActive) stopWiFi(); else startWiFi();
    noFrame = 0;
  } else if (!pressed && btnDown) {
    btnDown = false;
    uint32_t held = millis() - pressStart;
    if (!longHandled && held >= 40) {            // ignore contact bounce
      capturePhoto();
      if (wifiActive) showWifiScreen();          // capturePhoto clears the screen
    }
  }
}
