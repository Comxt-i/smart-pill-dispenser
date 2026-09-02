# Smart Pill Dispenser

Starter Arduino project for an ESP32-based three-lane smart medicine dispenser.

## Firmware folders

- `ESP32_Main` — Wi-Fi/web interface, DS1307, two I2C LCDs, buttons, buzzer, and I2C commands to the Nano controllers.
- `Nano_Lane1` — lane 1 controller, I2C address `0x10`.
- `Nano_Lane2` — lane 2 controller, I2C address `0x11`.
- `Nano_Lane3` — lane 3 controller, I2C address `0x12`.
- `ESP32_CAM` — separate ESP32-CAM starter sketch.

Open the `.ino` file inside the matching folder in Arduino IDE. Test one board and one module at a time before connecting motors and servos.

## Required libraries

- `LiquidCrystal_I2C`
- `TimeLib`
- `DS1307RTC`
- `Servo` (included with Arduino AVR boards)

## Important

- Replace the placeholder Wi-Fi values in `ESP32_Main/secrets.h` and `ESP32_CAM/secrets.h`.
- Confirm LCD addresses with an I2C scanner. Defaults are `0x27` and `0x25`.
- Power servos and motors from the external 5 V power bus, not through a Nano or ESP32 pin.
- All boards and drivers must share GND.
- The Nano lane files contain safe command-handling starters. Calibrate servo angles, IR logic, and motor timing before using real medicine.

