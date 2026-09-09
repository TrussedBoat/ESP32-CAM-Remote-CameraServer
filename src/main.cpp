#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include "wifi_config.h"
#include "camera_pins.h"

static const char *STREAM_BOUNDARY = "esp32camframe";
static const char *STREAM_PART_HEADER = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

WiFiServer server(80);

bool initCamera() {
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

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
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

void handleRoot(WiFiClient &client) {
  String html =
      "<!DOCTYPE html><html><head><title>ESP32-CAM</title></head>"
      "<body style=\"margin:0;background:#111;text-align:center\">"
      "<img src=\"/stream\" style=\"max-width:100%;height:auto\">"
      "</body></html>";

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

  if (request.indexOf("GET /stream") >= 0) {
    handleStream(client);
  } else if (request.indexOf("GET / ") >= 0) {
    handleRoot(client);
  } else {
    handleNotFound(client);
  }

  client.stop();
}
