# Freenove ESP32-S3 ESP32 S3 Capacitive Touch Display CYD WiFi BT, 2.8 Inch 240x320 IPS Screen

[product](https://store.freenove.com/products/fnk0104)

Official github: [freenove-esp32s3-display-2.8-lcd](https://github.com/Freenove/Freenove_ESP32_S3_Display)

Likely the same hardware design as [LCD wiki ES3C28P/ES3N28P](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display)

## MhaiBot face (V1)

This board uses a board-local `MhaiBotDisplay` subclass that draws two neutral rounded eyes with randomized blinking (about 2.5–6 s open, 100–180 ms closed) via a single LVGL timer.

- Emotions `neutral` and `robot_2` show the face and hide the stock center emoji.
- All other emotions keep the existing emoji / GIF path.
- Status bar, notifications, subtitle bar, preview image, Wi-Fi provisioning, touch, audio, wake word, and push-to-talk are unchanged.
- After a preview image times out, face vs emoji visibility is restored by the board override of `SetPreviewImage(nullptr)` (the preview timer calls this virtually).

## MhaiBot head (ServoBridge)

This board drives a separate ESP32-C3 (`mhaibot-servo-c3` project) that
controls two SG90 servos (pan/tilt) over a dedicated hardware UART, using the
board's onboard 4-pin "UART" connector (silkscreened RXD/TXD/GND/5V).

**Hardware evidence:** per the official Freenove schematic
(`Schematic/2.8inch_ESP32-S3_Display_Schematic.pdf` in
[Freenove/Freenove_ESP32_S3_Display](https://github.com/Freenove/Freenove_ESP32_S3_Display)),
this connector is wired directly to `U0TXD`/`U0RXD`, i.e. **UART0**
(GPIO43 TXD / GPIO44 RXD) — the same controller ESP-IDF uses for the
console/log/flash UART by default. The onboard USB-C port is native USB
(GPIO18/19), wired independently with no USB-UART bridge chip on this
board, so firmware upload and monitoring over USB-C are unaffected by
repurposing UART0.

To free UART0 for this link, `config.json` sets
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, moving `esp_log`/stdout/stderr to the
native-USB Serial/JTAG controller instead. Flash/monitor via `idf.py
flash monitor` over the onboard USB-C as usual; there is no more console
output over the UART connector.

### Wiring

```text
S3 UART connector TXD (GPIO43) -> C3 GPIO20 (RX)
S3 UART connector RXD (GPIO44) <- C3 GPIO21 (TX)
S3 GND                         -> C3 GND
UART connector 5V              -> not connected (do not tie to any GPIO)

C3 GPIO4  -> SG90 pan servo signal
C3 GPIO10 -> SG90 tilt servo signal
External regulated 5V -> both SG90 red wires (not from either board's 5V/3.3V pin)
Common GND -> SG90, C3, S3 all tied together
```

UART is 3.3V logic on both sides. See `mhaibot-servo-c3/README.md` for the
full protocol and the C3-side hardware test order.

### Software

- `servo_bridge.{h,cc}`: non-blocking UART bridge (dedicated FreeRTOS task,
  READY/PING-PONG/OK/ERR parsing, link timeout + reconnect, non-blocking nod
  gesture state machine). Board-scoped for now; has no board-specific
  dependency and can move to a common component later.
- MCP tools: `self.head.center`, `self.head.move`, `self.head.look_left`,
  `self.head.look_right`, `self.head.look_up`, `self.head.look_down`,
  `self.head.nod`. All raise a readable error if the C3 hasn't announced
  ready yet, or if `pan`/`tilt` fall outside the C3's configured safe range
  (pan 30-150, tilt 50-120).

### Not yet verified on hardware

- Whether GPIO43/44 are actually reachable at the physical "UART" connector
  pin order assumed here (RXD/TXD/GND/5V) — confirm with a continuity check
  before powering anything.
- Physical pan/tilt direction: `look_left`/`look_right`/`look_up`/`look_down`
  assume a servo mounting direction that has not been confirmed on a real
  unit. Adjust the +/- offsets in `InitializeTools()` if they come out
  mirrored.
- End-to-end link behavior (READY, timeout/reconnect, nod timing) has only
  been reviewed in source, not run on real hardware.
