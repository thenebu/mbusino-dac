# MBusino-DAC

Fork of [MBusino5S](https://github.com/Zeppelin500/MBusino) for the ESP32-S2 mini with an extra 0–10 V output.
The two channels of a DFRobot DFR0971 (GP8403, I2C) are controlled over MQTT and show up in Home Assistant as sliders. The original use is throttling a heat pump.

Everything else (M-Bus, DS18B20, BME280, web GUI, OTA) works as in MBusino5S. Version `1.2.0-dac`.

## Wiring (ESP32-S2 mini ↔ DFR0971)

```
 S2 mini                 DFR0971
 --------                --------
 3V3 ------------------- VCC (I2C side, 3.3 V logic)
 GND ------------------- GND
 GPIO33 (SDA) ---------- SDA
 GPIO35 (SCL) ---------- SCL

                         VIN / GND (power terminal) <- 12-36 V supply, see datasheet
                         VOUT0 / VOUT1              -> 0-10 V to the heat pump (0-10 V input + GND)
```

- Check the pin names and supply voltage against the DFRobot datasheet of your module, I wired this from the schematic description and have not verified it on hardware yet.
- The I2C address is 0x58 (default). The BME280 (0x76) can share the same bus.
- GPIO33/35 are also the OneWire buses 7 and 6. With `extension = 7` (7x DS18B20 without BME) the DAC is disabled. Use `extension` 5 or 0.
- The DAC is searched at boot and every 30 s afterwards, so it can also be connected later.

## MQTT

| Topic | Direction | Payload |
|---|---|---|
| `<name>/dac/1/set`, `<name>/dac/2/set` | in | volts, e.g. `8` or `5.5` (0–10, values above are limited to 10, non-numbers are ignored) |
| `<name>/dac/1/state`, `<name>/dac/2/state` | out, retained | current output in volts |
| `<name>/settings/dacFound` | out | `1` if the chip answered |

Home Assistant discovery creates two `number` entities (0–10 V, step 0.1) when auto discovery is on.

The last values are stored in EEPROM (5 s after the last change) and restored at boot. A fresh device starts at 0 V.
Check what 0 V means for your heat pump before connecting it.

## HTTP

- `/dac?ch=1&mv=5000` sets channel 1 to 5 V (millivolts).
- `/dac?failsafe=0&timeout=30` sets the output to 0 mV after 30 min without MQTT (`timeout=0` switches the fail-safe off, default: keep the last value). When MQTT is back the last commanded value returns.
- `/dac` without parameters shows the current values.

## Build

PlatformIO (the Arduino CLI ctags does not run on Apple Silicon without Rosetta):

```
pio run                 # build
pio run -t upload       # flash, board in boot mode: hold 0, tap RST
```

Libraries are taken from `~/Documents/Arduino/libraries`: ESP Async WebServer (ESP32Async), Async TCP, MBusCom, MBusinoLib, PubSubClient, ArduinoJson, OneWire, DallasTemperature, Adafruit BME280.

Code: `src/MBusino5S-DAC/dac.h` (driver, MQTT/HA, fail-safe), hooks in `MBusino5S-DAC.ino` and `mqtt.h`.
