#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_random.h>
#include "wifi_config.h"
#include "camera_pins.h"
#include "pages/login_html.h"
#include "pages/root_html.h"

static const char *STREAM_BOUNDARY = "esp32camframe";
static const char *STREAM_PART_HEADER = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

WiFiServer server(80);

String activeSessionToken = "";
unsigned long sessionCreatedAt = 0;
const unsigned long SESSION_DURATION_MS = 3600000UL; // 1 hour

String currentUsername = STREAM_USERNAME;
String currentPassword = STREAM_PASSWORD;

framesize_t currentFrameSize;
int currentJpegQuality;

camera_config_t buildCameraConfig(framesize_t frameSize, int quality) {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.frame_size = frameSize;
  config.jpeg_quality = quality;

  if (psramFound()) {
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  return config;
}

bool initCamera() {
  // FRAMESIZE_SVGA = 800x600, FRAMESIZE_CIF (this library's value) = 400x296.
  currentFrameSize = psramFound() ? FRAMESIZE_SVGA : FRAMESIZE_CIF;
  currentJpegQuality = psramFound() ? 10 : 12;

  camera_config_t config = buildCameraConfig(currentFrameSize, currentJpegQuality);
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

// Only safe to call when no client is actively mid-stream: this server handles
// one connection at a time in loop(), so as long as the caller (handleSettings)
// runs from that same loop, the camera is guaranteed idle here.
bool reconfigureCamera(framesize_t frameSize, int quality) {
  esp_camera_deinit();

  camera_config_t config = buildCameraConfig(frameSize, quality);
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera reconfigure failed: 0x%x\n", err);
    camera_config_t fallback = buildCameraConfig(currentFrameSize, currentJpegQuality);
    esp_camera_init(&fallback);
    return false;
  }

  currentFrameSize = frameSize;
  currentJpegQuality = quality;
  return true;
}

void connectToWiFi() {
#ifdef STATIC_IP_ADDR
  IPAddress localIP(STATIC_IP_ADDR);
  IPAddress gateway(STATIC_GATEWAY);
  IPAddress subnet(STATIC_SUBNET);
  IPAddress dns1(STATIC_DNS1);
  IPAddress dns2(STATIC_DNS2);

  if (WiFi.config(localIP, gateway, subnet, dns1, dns2)) {
    Serial.println("Using static IP configuration");
  } else {
    Serial.println("Static IP configuration failed, falling back to DHCP");
  }
#else
  Serial.println("Using DHCP");
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected. IP address: ");
  Serial.println(WiFi.localIP());
}

String generateSessionToken() {
  String token = "";
  for (int i = 0; i < 4; i++) {
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", (unsigned int)esp_random());
    token += buf;
  }
  return token;
}

bool isSessionValid(const String &cookieHeader) {
  if (activeSessionToken.length() == 0) {
    return false;
  }

  int idx = cookieHeader.indexOf("session=");
  if (idx < 0) {
    return false;
  }

  String token = cookieHeader.substring(idx + 8);
  int semi = token.indexOf(';');
  if (semi >= 0) {
    token = token.substring(0, semi);
  }
  token.trim();

  if (token != activeSessionToken) {
    return false;
  }

  return (unsigned long)(millis() - sessionCreatedAt) < SESSION_DURATION_MS;
}

String urlDecode(const String &s) {
  String out = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < s.length()) {
      char hex[3] = {s[i + 1], s[i + 2], '\0'};
      out += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

String extractFormField(const String &body, const String &name) {
  String key = name + "=";
  int idx = body.indexOf(key);
  if (idx < 0) {
    return "";
  }
  int valueStart = idx + key.length();
  int end = body.indexOf('&', valueStart);
  return urlDecode(body.substring(valueStart, end < 0 ? body.length() : end));
}

void redirectTo(WiFiClient &client, const String &path) {
  client.println("HTTP/1.1 302 Found");
  client.print("Location: ");
  client.println(path);
  client.println("Connection: close");
  client.println();
}

void handleLoginPage(WiFiClient &client, bool showError) {
  String html = LOGIN_HTML_HEAD;
  if (showError) {
    html += LOGIN_HTML_ERROR;
  }
  html += LOGIN_HTML_FORM;

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.printf("Content-Length: %u\r\n", html.length());
  client.println("Connection: close");
  client.println();
  client.print(html);
}

void handleLoginSubmit(WiFiClient &client, const String &body) {
  String username = extractFormField(body, "username");
  String password = extractFormField(body, "password");

  if (username == currentUsername && password == currentPassword) {
    activeSessionToken = generateSessionToken();
    sessionCreatedAt = millis();

    client.println("HTTP/1.1 302 Found");
    client.println("Location: /");
    client.print("Set-Cookie: session=");
    client.println(activeSessionToken);
    client.println("Connection: close");
    client.println();
  } else {
    redirectTo(client, "/login?error=1");
  }
}

void handleLogout(WiFiClient &client) {
  activeSessionToken = "";
  redirectTo(client, "/login");
}

void handleSettings(WiFiClient &client, const String &body) {
  String usernameParam = extractFormField(body, "username");
  String passwordParam = extractFormField(body, "password");
  String resolutionParam = extractFormField(body, "resolution");
  String qualityParam = extractFormField(body, "quality");

  if (usernameParam.length() > 0) {
    currentUsername = usernameParam;
  }
  if (passwordParam.length() > 0) {
    currentPassword = passwordParam;
  }

  framesize_t newFrameSize = currentFrameSize;
  int newQuality = currentJpegQuality;
  bool cameraChanged = false;

  if (resolutionParam == "vga") {
    newFrameSize = FRAMESIZE_VGA; // 640x480
    cameraChanged = true;
  } else if (resolutionParam == "svga") {
    newFrameSize = FRAMESIZE_SVGA; // 800x600
    cameraChanged = true;
  } else if (resolutionParam == "xga") {
    newFrameSize = FRAMESIZE_XGA; // 1024x768
    cameraChanged = true;
  }

  if (qualityParam == "high") {
    newQuality = 10;
    cameraChanged = true;
  } else if (qualityParam == "medium") {
    newQuality = 20;
    cameraChanged = true;
  } else if (qualityParam == "low") {
    newQuality = 35;
    cameraChanged = true;
  }

  bool cameraOk = true;
  if (cameraChanged) {
    cameraOk = reconfigureCamera(newFrameSize, newQuality);
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain");
  client.println("Connection: close");
  client.println();
  client.println(cameraOk ? "OK" : "Camera reconfigure failed");
}

void handleRoot(WiFiClient &client) {
  String html = ROOT_HTML;

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.printf("Content-Length: %u\r\n", html.length());
  client.println("Connection: close");
  client.println();
  client.print(html);
}

void handleStream(WiFiClient &client) {
  client.setNoDelay(true);

  client.println("HTTP/1.1 200 OK");
  client.printf("Content-Type: multipart/x-mixed-replace;boundary=%s\r\n", STREAM_BOUNDARY);
  client.println("Access-Control-Allow-Origin: *");
  client.println();

  char partHeader[64];

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      break;
    }

    client.printf("--%s\r\n", STREAM_BOUNDARY);
    size_t headerLen = snprintf(partHeader, sizeof(partHeader), STREAM_PART_HEADER, fb->len);
    client.write(reinterpret_cast<const uint8_t *>(partHeader), headerLen);
    client.write(fb->buf, fb->len);
    client.println();

    esp_camera_fb_return(fb);

    if (!client.connected()) {
      break;
    }
  }
}

void handleNotFound(WiFiClient &client) {
  client.println("HTTP/1.1 404 Not Found");
  client.println("Content-Type: text/plain");
  client.println("Connection: close");
  client.println();
  client.println("Not found. Try / or /stream");
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  if (!initCamera()) {
    Serial.println("Halting: camera init failed");
    while (true) {
      delay(1000);
    }
  }

  connectToWiFi();

  server.begin();
  Serial.print("Stream ready at http://");
  Serial.print(WiFi.localIP());
  Serial.println("/stream");
}

void loop() {
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  String request = client.readStringUntil('\r');
  client.readStringUntil('\n');

  String cookieHeader;
  int contentLength = 0;

  while (client.connected()) {
    String line = client.readStringUntil('\r');
    client.readStringUntil('\n');
    if (line.length() == 0) {
      break;
    }
    if (line.startsWith("Cookie:")) {
      cookieHeader = line;
    } else if (line.startsWith("Content-Length:")) {
      contentLength = line.substring(15).toInt();
    }
  }

  bool authed = isSessionValid(cookieHeader);

  if (request.indexOf("POST /login") >= 0) {
    contentLength = min(contentLength, 255);
    char body[256] = {0};
    client.readBytes(body, contentLength);
    handleLoginSubmit(client, String(body));
  } else if (request.indexOf("POST /settings") >= 0) {
    if (authed) {
      contentLength = min(contentLength, 255);
      char body[256] = {0};
      client.readBytes(body, contentLength);
      handleSettings(client, String(body));
    } else {
      client.println("HTTP/1.1 401 Unauthorized");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("Login required");
    }
  } else if (request.indexOf("GET /login") >= 0) {
    handleLoginPage(client, request.indexOf("error=1") >= 0);
  } else if (request.indexOf("GET /logout") >= 0) {
    handleLogout(client);
  } else if (request.indexOf("GET /stream") >= 0) {
    if (authed) {
      handleStream(client);
    } else {
      client.println("HTTP/1.1 401 Unauthorized");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("Login required");
    }
  } else if (request.indexOf("GET / ") >= 0) {
    if (authed) {
      handleRoot(client);
    } else {
      redirectTo(client, "/login");
    }
  } else {
    handleNotFound(client);
  }

  client.stop();
}
