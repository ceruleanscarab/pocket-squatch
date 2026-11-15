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
- Every pet has DNA (`sim/include/squatch/dna.hpp`): looks, temperament and rarity, half inherited by the
  next egg. Temperament effects stay small (`Tuning::*Effect`) so care, not luck, decides the adult form.
- Trails come from Wi-Fi scans: only hashes of network names are stored, never the names, and the radio is
  on only for the scan itself. Bluetooth visitors come later, once there's a second board.
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
  Only `pc/`, `android/` and `firmware/` touch SDL or ESP-IDF, files, and the real clock.
- `firmware/` is the ESP-IDF 5.4 project for the board (`idf.py -C firmware build`). `main/board.c` holds the
  pins and panel start-up copied from Waveshare's BSP; keep it close to that source rather than tidying it.
  Wi-Fi credentials go in `firmware/sdkconfig` via menuconfig, which is git-ignored.
- Every scene touch target is checked at 64 px or more in `tests/scene_tests.cpp`; add new ones there.
- Balance numbers live in `Tuning`. After changing any of them, run `./build/squatch-harness --runs 50`
  and `./build/squatch-tests`; every scripted player should still mostly reach its adult form.
- Changing `State` means bumping `kSaveVersion` in `save.hpp`. Add new fields only at the end of `State`, so
  older saves stay a prefix and `load()` can migrate them (see the version 1 case in `save.cpp`).
- On the board, anything using Wi-Fi brackets its work with `radio::take()` / `radio::give()`.
- Tests are plain C++ with a `CHECK` macro, no external dependencies. Run `./build/squatch-scene-tests`
  after scene changes.

## Build plan
1. Sim core + harness — done.
2. SDL scene on the PC (placeholder art): clearing, idle animation, care tray, mouse as touch — done.
3. Port to the board: ST7701 RGB panel via esp_lcd (the scene draws its own frames, so no LVGL), GT911 touch,
   flash saves, NTP + PCF85063 clock — code done and building; not yet run on hardware. AXP2101 moved to step 6.
4. Real art for egg, baby, child; then the other stages.
5. Mini-games (Hide and Seek, Berry Catch, Blurry Photo) and the photo gallery — done (`Scene::Game`, from Play);
   Quick Catch stays as the no-timing option.
6. Sound (ES8311 over I2S), battery level and sleep via the AXP2101, then the 3D-printed stump case.
- DNA and rarity, and the Wi-Fi trail map with forage trips — done (firmware scan not yet run on hardware).
- Battery level via the AXP2101 (`firmware/main/power.cpp`, `Scene::setBattery`) — done.
- Settings page (brightness, screen timeout, volume; `Scene::Settings`, kept outside the pet's save) and screen-off — done;
  with the screen off the chip uses automatic light sleep (`sleepInit`/`screenSleep` in `main.cpp`, power-management
  locks held while the screen is on) and the AMOLED panel sleeps (`board_display_power`).
- Weather: `scene/` `weather.hpp` (Open-Meteo parser, `Scene::setWeather`; clouds, fog and snow cover are part of the
  cached scenery, rain and snow fall each frame); `firmware/main/weatherfetch.cpp` fetches hourly over
  `netjoin` (shared with the clock sync). Not yet run on hardware.
- Sound: `Synth` cues in `scene/` (`sounds.hpp`), played by `firmware/main/audio.cpp` (ES8311) and SDL on the PC — done,
  not yet heard on hardware.
- The Waveshare ESP32-S3-Touch-AMOLED-1.8 is a second board target (`board_amoled18.c`, menuconfig Board); it ran
  first on 2026-10-17 at 7-9 fps. Since then: the scenery is cached once a minute (`Scene::drawScenery`; anything
  that moves must be drawn in `Scene::draw` after it), shading and text are cheaper (about 3x less work per frame),
  and the AMOLED scales and sends each frame on core 0 while core 1 draws the next. Not yet measured on hardware.
  Its text is too small at that size, so the AMOLED builds turn on big text (`Scene::setBigText`; `squatch-pc --big-text`):
  text one size up via `ts()`, wrapped with `para()`/`label()`, and some pages laid out again. The 4" board is unchanged;
  `testBigText` checks the pages and their buttons. Tray labels and the status numbers stay at the old size (no room).
- Android app (`android/`, SDL2, built and published to the "android" release by CI) — basic version done;
  real Wi-Fi scans (MainActivity.startWifiScan) and "needs you" notifications (PhonePet/NudgeReceiver, using
  `Wrist::nextNeed` via the `forecast` JNI export in main.cpp) — done, not yet run on a phone. It draws at 2x/3x (`Canvas` scale) inside a handheld frame.
- Galaxy Watch companion (`android/wear`, Java + the sim/scene via JNI; `scene/` `wrist.hpp` is its screen): the save
  syncs over the Wear OS Data Layer (item `/pet`, newest `at` wins), a tile, and predicted nudges. Not yet run on a watch.
- Play and talk (`scene/`): boop, tickle and pat the pet (its eyes follow your finger), shake a tree for a pinecone
  and play fetch (tap or flick to toss), light the campfire; speech bubbles (`Scene::speak`) for replies, a hello when
  the screen comes back on, and chatter about needs, weather and seasons. All optional; none of it is timing-critical.
- Horse edition (menuconfig Pet = Horse; `Scene::setSpecies`): its own drawing, paddock, foods, real sounds
  (`horse_sounds.cpp`, CC0 from BigSoundBank, prepared by `tools/horse_sounds_*.py`; more takes of each in
  `horse_takes.cpp` by `tools/horse_takes.py`, and the Synth picks a different take each time), words, a birth instead of an
  egg, and five games including Grooming (`Game::Groom`: brush, hooves, mane, a ribbon; no clock). Fence Jump was removed.
  It uses `Tuning::quickToTrust()` (out from behind the tree in hours, friendly by about day 3; check with
  `squatch-harness --quick-trust`); the Bigfoot keeps the slower default. Voices are higher the younger the pet (`Scene::voicePitch`).
- Wi-Fi setup from a phone (`firmware/main/wifisetup.cpp`: open hotspot, captive page, DNS; saved in NVS by
  `storage::saveWifi`, used by the clock and weather via `storage::wifiCredentials`; Kconfig is only the default).
  Settings > Wi-Fi in `scene/`. Not yet run on hardware.
  The horse build also serves the illustrated guide at `/guide.pdf` (`firmware/main/guide/`, linked from the setup page).
  First start (no Wi-Fi saved, NVS `welcomed` unset): `Scene::openWelcome` shows the Wi-Fi page as a welcome with the
  hotspot up and a Wi-Fi QR code (`scene/src/qrcodegen.cpp`, Nayuki, MIT); the Wi-Fi page shows the same code whenever
  setup is running. Joining or LATER sets `welcomed`. The hotspot sends DHCP option 114 (the page's address) so phones
  open it at once; once a phone joins (`wifisetup::phonesJoined`) the screen shows a second code for http://192.168.4.1/.
  Start over on the boards has a fourth choice, "like new" (`setFactoryResetAvailable`, confirmed by a second tap):
  `storage::eraseAll` erases NVS, sets `nodefaults` so menuconfig's Wi-Fi and town aren't copied back, and restarts.
- Boarding stable (More > Stable; "Sitter" for the Bigfoot): `Sim::board`, save version 3. While boarded nothing
  happens and it doesn't age (`born` and the rate-limit clocks shift by the time away); it comes home fed and clean.
- Animal visitors (`Scene::Visitor`, both editions): every 4-10 minutes with the screen on, one comes by for a while,
  chosen by hour, month and weather (bunny, bird, deer at dawn/dusk, ducks with spring ducklings, butterfly in
  warm months, owl at night). Tap one to send it off. Scene-only: nothing in the save.
- Updates over the air (`firmware/main/ota.cpp`, menuconfig Automatic updates): two app slots (`partitions.csv`, nvs kept
  where it was), rollback if a new build doesn't run a minute. Nightly about 3 am (screen off, battery 40%+ or charging)
  or Settings > Wi-Fi > the version row. Builds come from the "firmware" release, which CI keeps current on main
  (`squatch-lcd4b.bin`, `squatch-amoled18.bin`, `pony-amoled18.bin`; version = CI run number, hand builds are 0).
  menuconfig Wi-Fi and town are copied into NVS at boot (`storage::keepBuiltInDefaults`) since CI builds have neither.
- Cloud backups (`cloud/` Cloudflare Worker + KV; `firmware/main/cloud.cpp`; menuconfig Cloud backups address, empty =
  off): nightly about 2 am and after Wi-Fi setup, PUT the save under the board's 16-character backup code (NVS
  `bk_code`; the Worker keys by its hash, keeps latest + a week of dailies). The phone setup page shows the code and
  takes one to restore; a restored pet comes back as from the boarding stable. Code shown on Settings > Wi-Fi.
- Later: Bluetooth visitors between two boards.
