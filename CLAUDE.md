# Pocket Squatch — notes for Claude Code

Virtual-pet desk toy: a baby Bigfoot on an ESP32-S3 with a 4" 480×480 touchscreen
(Waveshare ESP32-S3-Touch-LCD-4), battery powered, touch only. Design doc:
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

## Layout and rules
- `sim/` is the game and must stay hardware-free: no display, touch, audio, file or clock calls.
  Time only enters through `Sim::tick(seconds)`. It builds as a plain library on the PC and as an
  ESP-IDF component on the device (`sim/CMakeLists.txt`).
- Balance numbers live in `Tuning`. After changing any of them, run `./build/squatch-harness --runs 50`
  and `./build/squatch-tests`; every scripted player should still mostly reach its adult form.
- Changing `State` means bumping `kSaveVersion` in `save.hpp`.
- Tests are plain C++ with a `CHECK` macro, no external dependencies.

## Build plan
1. Sim core + harness — done.
2. SDL scene on the PC (placeholder art): clearing, idle animation, care tray, mouse as touch.
3. Port to the board: LovyanGFX or LVGL, GT911 touch, NVS/flash saves, NTP + DS3231 clock.
4. Real art for egg, baby, child; then the other stages.
5. Mini-games (Hide and Seek, Berry Catch, Blurry Photo) and the photo gallery.
6. Sound (MAX98357A over I2S), battery and sleep, then the 3D-printed stump case.
