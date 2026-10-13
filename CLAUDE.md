# Pocket Squatch — notes for Claude Code

Virtual-pet desk toy: a baby Bigfoot on an ESP32-S3 with a 4" 480×480 touchscreen
(Waveshare ESP32-S3-Touch-LCD-4B, "Smart 86 Box" version), battery powered, touch only. Design doc:
https://claude.ai/code/artifact/85ce4636-3c76-49ca-9444-d65b9a6a3e2e

## Decisions already made
- The owner names each pet at hatching; "Squatch" is only the working name.
- Smooth, anti-aliased sprites at about 192×192 (not chunky pixel art), stored compressed.
- It never dies: it runs away and can be coaxed back.
- Elders lay an egg; the photo gallery and footprints carry across generations.
- Battery from day one: screen-off sleep, touch to wake.
- Fresh codebase (not a fork of pocket-tank / Pocket Trubbles).
- Accessibility matters: touch targets of 64 px or more, no fast double-taps, no timing-critical
  gestures outside optional mini-games, and a half-speed meter option.

## Board: Waveshare ESP32-S3-Touch-LCD-4B
Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B
- ESP32-S3 (WROOM-1 N16R8): 16 MB flash, 8 MB PSRAM
- 4" 480×480 IPS, ST7701 over RGB; GT911 capacitive touch (I2C)
- Audio: ES8311 codec over I2S, MX1.25 speaker connector (8 Ω 2 W), onboard mic (ES7210 echo cancel)
- Power: AXP2101 PMIC (LiPo charging, battery level, power-off), PH2.0 battery connector
- PCF85063 RTC (backed by the main battery), QMI8658 6-axis IMU, TCA9554 I/O expander
- No separate amp or DS3231 needed. The IMU is for optional extras only (shake to wake, tilt
  in Berry Catch, face-down to sleep); never make it required to play.

## Layout and rules
- `sim/` is the game and must stay hardware-free: no display, touch, audio, file or clock calls.
  Time only enters through `Sim::tick(seconds)`. It builds as a plain library on the PC and as an
  ESP-IDF component on the device (`sim/CMakeLists.txt`).
- `scene/` (renderer, gestures, the clearing) follows the same rule: touch events and a millisecond
  clock in, pixels out to an RGB565 `Canvas`. It reads the sim and calls actions but never ticks it.
  Only `pc/` (and later the board code) touches SDL, files and the real clock.
- Every scene touch target is checked at 64 px or more in `tests/scene_tests.cpp`; add new ones there.
- Balance numbers live in `Tuning`. After changing any of them, run `./build/squatch-harness --runs 50`
  and `./build/squatch-tests`; every scripted player should still mostly reach its adult form.
- Changing `State` means bumping `kSaveVersion` in `save.hpp`.
- Tests are plain C++ with a `CHECK` macro, no external dependencies. Run `./build/squatch-scene-tests`
  after scene changes.

## Build plan
1. Sim core + harness — done.
2. SDL scene on the PC (placeholder art): clearing, idle animation, care tray, mouse as touch — done.
3. Port to the board: LovyanGFX or LVGL (ST7701 RGB panel), GT911 touch, flash saves, NTP + PCF85063 clock, AXP2101 power.
4. Real art for egg, baby, child; then the other stages.
5. Mini-games (Hide and Seek, Berry Catch, Blurry Photo) and the photo gallery.
6. Sound (ES8311 over I2S), battery level and sleep via the AXP2101, then the 3D-printed stump case.
