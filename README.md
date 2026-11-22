# ESP32-CAM Pocket Camera

A compact DIY digital camera built with an **AI-Thinker ESP32-CAM**, an **ST7735 TFT display**, a microSD card, and two buttons/switches.

The device shows a live camera preview on the TFT, saves photos to the microSD card, and provides a Wi-Fi photo gallery that can be opened from a phone.

**Project status:** Prototype  
**Firmware:** Arduino IDE / ESP32  
**Interface language:** Turkish (web interface and device messages)

## Features

- Live camera preview on the ST7735 display.
- Preview image rotated 90° and fitted to the screen without cropping.
- Short press: capture a photo and save it as a JPEG file.
- Long press: turn the Wi-Fi access point on or off.
- Wi-Fi photo gallery: view, download, and delete saved photos from a phone.
- Wi-Fi is disabled at startup; the camera preview resumes after Wi-Fi is turned off.
- Camera is initialized once; the frame size is changed when taking a full-resolution photo.

## Hardware

| Component | Description |
|---|---|
| Main board | AI-Thinker ESP32-CAM |
| Display | ST7735 SPI TFT, 8-pin module (160 × 128 configured in firmware) |
| Storage | microSD card |
| Battery | 3.7 V Li-Po battery |
| Charger / protection board | TP4056 module with OUT+ / OUT- terminals |
| Power switch | Slide switch |
| Capture / Wi-Fi button | Tactile push button |

## Wiring

See the full wiring diagram: [`docs/wiring-diagram.png`](docs/wiring-diagram.png).

### TFT display

The firmware uses the following ESP32-CAM pins:

| TFT signal | ESP32-CAM GPIO |
|---|---:|
| SCK / CLK | GPIO 14 |
| MOSI / SDA / DIN | GPIO 15 |
| CS | GPIO 1 |
| DC / A0 | GPIO 3 |
| RST / RES | 3.3 V (tied high, as shown in the diagram) |
| VCC | Connect according to the TFT module's voltage specification |
| GND | GND |

### microSD card

| SD signal | ESP32-CAM GPIO |
|---|---:|
| MISO | GPIO 2 |
| CS | GPIO 13 |
| SCK | GPIO 14 (shared SPI clock) |
| MOSI | GPIO 15 (shared SPI MOSI) |
| VCC / GND | Board/module power and common GND |

The TFT and SD card share the HSPI bus. Their chip-select pins are separate.

### Push button

| Button terminal | Connection |
|---|---|
| One side | GPIO 12 |
| Other side | GND |

The firmware enables `INPUT_PULLUP`, so the button is active LOW.

### Camera

The camera pin mapping in the sketch is the standard AI-Thinker ESP32-CAM mapping. No additional camera wiring is required when using that board.

### Battery and power

Follow the polarity markings on the TP4056 board and the wiring diagram. The battery connects to `B+` / `B-`; the load is taken from `OUT+` / `OUT-` through the power switch as shown.

**Important:** Verify the voltage accepted by your exact ESP32-CAM and TFT modules before powering the circuit. Never connect the Li-Po cell directly to a 5 V input. Use a suitable regulated supply where required. All circuit grounds must be common.

## Arduino IDE setup

1. Install the ESP32 board package in Arduino IDE.
2. Select **AI Thinker ESP32-CAM**.
3. Enable **PSRAM**.
4. Install these libraries from Library Manager:
   - Adafruit GFX Library
   - Adafruit ST7735 and ST7789 Library
5. Open `src/ESP32_CAM_Pocket_Camera.ino`.
6. Select the correct serial port and upload the sketch.

The `esp_camera`, `WiFi`, `WebServer`, `FS`, `SD`, and `SPI` headers are provided by the ESP32 Arduino core.

## Using the camera

### Taking a photo

- Press and release the tactile button briefly.
- The photo is captured at VGA (640 × 480), rotated and saved as a portrait JPEG (480 × 640).
- Files are stored on the SD card as `pic_0001.jpg`, `pic_0002.jpg`, and so on.

### Opening the Wi-Fi gallery

1. Press and hold the button for approximately **1.5 seconds**.
2. Connect your phone to the Wi-Fi network:
   - **SSID:** `ESP32-CAM`
   - **Password:** `12345678`
3. Open `http://192.168.4.1` in a browser.
4. Use the web page to view, download, or delete photos.
5. Hold the button again to turn Wi-Fi off.

Wi-Fi is off by default. The live TFT preview is paused while the web server is active.

## Firmware settings

The main settings are near the top of the sketch:

| Setting | Default | Purpose |
|---|---:|---|
| `FLIP_V` | `0` | Flip image vertically |
| `FLIP_H` | `0` | Mirror image horizontally |
| `PREVIEW_SIZE` | `FRAMESIZE_QQVGA` | Live preview resolution |
| `PHOTO_SIZE` | `FRAMESIZE_VGA` | Photo capture resolution |
| `JPEG_QUALITY` | `85` | JPEG quality setting |
| `AP_SSID` | `ESP32-CAM` | Wi-Fi network name |
| `AP_PASS` | `12345678` | Wi-Fi password (minimum 8 characters) |
| `LONG_PRESS_MS` | `1500` | Long-press duration in milliseconds |

Change the Wi-Fi password before sharing or deploying the device.

## Important hardware notes

- **GPIO 12 is a boot-strapping pin** on ESP32. Avoid holding the button while powering up or resetting the board.
- GPIO 1 and GPIO 3 are also the ESP32 UART TX/RX pins. This design assigns them to TFT CS and DC; serial logging/programming behavior may therefore be affected while the display is connected.
- The exact TFT pin order and voltage requirements vary between display modules. Check the labels and datasheet for your module rather than relying only on wire colors.
- Use a properly rated Li-Po charger/protection circuit and suitable voltage regulation. Do not leave a Li-Po battery charging unattended.
- The firmware disables the brownout detector. This is retained from the supplied working sketch, but it does not solve an inadequate power supply. Use a stable supply capable of handling ESP32-CAM current peaks.

## Repository structure

```text
ESP32-CAM-Pocket-Camera/
├── src/
│   └── ESP32_CAM_Pocket_Camera.ino
├── docs/
│   └── wiring-diagram.png
├── .gitignore
├── LICENSE
└── README.md
```

## Contributing

Issues, wiring corrections, tested improvements, and pull requests are welcome. When reporting a problem, include your ESP32 board variant, Arduino-ESP32 core version, TFT module type, and SD card details.

## License

This project is distributed under the MIT License. See [LICENSE](LICENSE).
