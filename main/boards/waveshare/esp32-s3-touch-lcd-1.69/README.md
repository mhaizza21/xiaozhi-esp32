# Waveshare ESP32-S3-Touch-LCD-1.69

Board support for the Waveshare ESP32-S3-Touch-LCD-1.69 Mhaibot sensor bring-up path.

## Hardware Profile

| Function | Device | Interface | GPIO / Address |
|---|---|---|---|
| LCD | ST7789V2 240x280 | SPI | DC GPIO4, CS GPIO5, CLK GPIO6, DIN GPIO7, RST GPIO8, BL GPIO15 |
| Touch | CST816T | I2C | address 0x15, SCL GPIO10, SDA GPIO11, RST GPIO13, INT GPIO14 |
| IMU | QMI8658C | I2C | address 0x6B, INT1 GPIO38 |
| RTC | PCF85063ATL | I2C | address 0x51, INT GPIO39 |
| Buzzer | onboard buzzer | GPIO / PWM | GPIO42 |
| Battery ADC | voltage divider | ADC | GPIO1, VBAT = VADC x 3 |
| Power hold | SYS_EN | GPIO | GPIO41 |
| Power button state | SYS_OUT | GPIO | GPIO40 |

The current implementation registers the LCD and touch panel, then exposes read-only diagnostic MCP tools for onboard sensors.

## Build

```sh
python scripts/build.py waveshare/esp32-s3-touch-lcd-1.69 --name esp32-s3-touch-lcd-1.69
```

## Diagnostic Tools

| Tool | Purpose |
|---|---|
| `self.sensors.get_status` | Board-level status for touch, IMU, RTC, and future PIR/camera placeholders |
| `self.sensors.get_motion` | One QMI8658 accelerometer/gyroscope sample |
| `self.sensors.get_rtc_time` | PCF85063 time registers and clock integrity flag |
| `self.sensors.get_touch_last_event` | CST816T bring-up state and last board-observed event |

## Validation Boundary

Build success only proves the firmware compiles for the board profile. Touch, IMU, RTC, power behavior, battery ADC, buzzer, PIR, and camera require hardware validation on the physical board before they should be treated as passed.

PIR and camera are architecture placeholders only in this slice. Do not assign GPIOs for them until the actual module, wiring, and conflicts with the extension header are checked.
