# ESP32-CAM WiFi Streamer

A lightweight HTTP-based live video streaming system for the AI Thinker ESP32-CAM board. Stream camera video to any device on your network via a web browser.

## What It Does

- **Live JPEG streaming** over HTTP using multipart/x-mixed-replace (M-JPEG)
- **Simple web interface** — just open `http://<board-ip>/` in a browser
- **Dual-core architecture** — WiFi stack runs independently from your application code
- **Frame buffering** — uses PSRAM (4 MB) for smooth, high-resolution streaming when available
- **Configurable WiFi** — DHCP or static IP, defined in a simple header file

## Hardware

- **AI Thinker ESP32-CAM** board with OV2640 camera
- **USB-to-TTL UART adapter** (CH340, CP2102, or FTDI) for flashing and serial debug
- **5V power supply** (stable; USB power may cause stalls)
- **WiFi router** (2.4 GHz 802.11b/g/n)

## Quick Start

### 1. Configure WiFi Credentials

Copy the template and fill in your own values:

```bash
cp include/wifi_config.h.example include/wifi_config.h
```

Then edit `include/wifi_config.h`:

```cpp
#define WIFI_SSID     "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
```

`wifi_config.h` is gitignored, so your real credentials never get committed.

Optional: Enable static IP by uncommenting `STATIC_IP_ADDR` and related lines.

The same file also holds the stream's login credentials (see [Authentication](#authentication) below):

```cpp
#define STREAM_USERNAME "admin"
#define STREAM_PASSWORD "changeme"
```

### 2. Build and Upload

```bash
# Install PlatformIO if you haven't already
pip install platformio

# Build for the ESP32-CAM
cd /path/to/ESP32-WiFi
pio run

# Upload to the board (USB-TTL adapter connected to /dev/ttyUSB0)
pio run -t upload

# Watch serial output
pio device monitor
```

### 3. Access the Stream

After the board boots and connects to WiFi, the serial monitor will print:

```
Connected. IP address: <board-ip>
Stream ready at http://<board-ip>/stream
```

Open your browser and visit `http://<board-ip>/` — you'll be redirected to a login
page. Sign in with the `STREAM_USERNAME`/`STREAM_PASSWORD` you set in
`include/wifi_config.h` to reach the viewer (embedded `<img>` pointing to `/stream`).

## How It Works

### Hardware

- **ESP32 SoC:** Dual-core 240 MHz processor with integrated WiFi
- **OV2640 Camera:** 2MP sensor connected via parallel I2C/DVP interface
- **PSRAM:** 4 MB external RAM for frame buffers


### Frame Pipeline

1. **Camera captures** raw pixels from OV2640 sensor
2. **DMA engine** writes compressed JPEG to frame buffer in PSRAM (background, no CPU needed)
3. **handleStream()** calls `esp_camera_fb_get()` to grab a ready frame
4. **HTTP response** writes the JPEG bytes to the client socket
5. **handleStream()** calls `esp_camera_fb_return()` to release the buffer for reuse
6. Loop continues at step 3 (~1-10 FPS depending on resolution and network load)

### Dual-Core Note

The **WiFi stack runs on Core 1** automatically (part of the Arduino framework). Your `loop()` runs on **Core 0**. This is why we use flags and the `volatile` keyword for safe communication between them — see [src/main.cpp](src/main.cpp) for details.

## Configuration

### Resolution & Quality

Edit `src/main.cpp` in `initCamera()`:

```cpp
if (psramFound()) {
  config.frame_size = FRAMESIZE_SVGA;      // 1024×768, high resolution
  config.jpeg_quality = 10;                 // 0-63, lower = better quality
  config.fb_count = 2;                      // Double-buffering
  config.fb_location = CAMERA_FB_IN_PSRAM;  // Use external RAM
} else {
  config.frame_size = FRAMESIZE_CIF;        // 352×288, low resolution (no PSRAM)
  config.jpeg_quality = 12;
  config.fb_count = 1;                      // Single buffer
  config.fb_location = CAMERA_FB_IN_DRAM;   // Use internal RAM only
}
```

### Authentication

The stream is gated behind a login page (`/login`) backed by a session cookie. Set
credentials in `include/wifi_config.h`:

```cpp
#define STREAM_USERNAME "admin"
#define STREAM_PASSWORD "changeme"
```

Notes on how it works:
- **Single active session** — logging in from another device/browser replaces the
  current session. This is a one-viewer home device, not a multi-user server.
  Visit `/logout` to end the current session manually.
- **Sessions don't survive a reboot** — the session token lives only in RAM.
- **Plaintext HTTP, no TLS** — fine on a trusted LAN. If you ever expose this stream
  to the internet (e.g. via router port forwarding), put it behind a VPN back to your
  home network rather than forwarding the login directly — the credentials and
  session cookie are not encrypted in transit.

### Memory Usage

- **Flash:** ~836 KB of 3 MB app partition (~27%), leaving plenty of room for growth
- **RAM:** ~49 KB static (~15% of 320 KB), plus frame buffer and WiFi stack allocated at runtime

Check current usage yourself after building:
```bash
pio run
~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-size -B .pio/build/esp32cam/firmware.elf
```

See [CLAUDE.md](CLAUDE.md) for detailed memory layout.

## Troubleshooting

### "Device not found" on upload

Your USB-to-TTL adapter isn't being detected. Check:
- Is it plugged in? (look for power LED on the adapter)
- Try a different USB cable (many are charge-only, not data-capable)
- Try a different USB port on your computer
- Confirm `/dev/ttyUSB0` exists: `ls /dev/ttyUSB*`

### Slow FPS or stalling stream

Common causes:
1. **Weak WiFi signal** — move closer to router (but not too close; -40 to -60 dBm is ideal)
2. **Marginal 5V power supply** — use a dedicated phone charger, not PC USB
3. **Channel congestion** — switch to a less-crowded WiFi channel in your router settings
4. **High resolution/quality** — lower frame size or increase `jpeg_quality` (higher = more compression)

### Camera init fails

Check UART connection and baud rate (115200):
```bash
pio device monitor --baud 115200
```

If still failing, the camera ribbon cable may be loose. Reseat it on the ESP32-CAM board.

## Next Steps

Potential improvements:
- **Quality selector** — runtime JPEG quality control
- **Camera settings** — brightness, contrast, saturation UI
- **Remote firmware update** — flash new firmware over WiFi (OTA) instead of USB

## References

- [AI Thinker ESP32-CAM wiki](https://wiki.ai-thinker.com/esp32-cam)
- [PlatformIO documentation](https://docs.platformio.org/)
- [Arduino ESP32 core](https://github.com/espressif/arduino-esp32)
- [esp32-camera library](https://github.com/espressif/esp32-camera)

## License

This project is provided as-is for educational and personal use.
