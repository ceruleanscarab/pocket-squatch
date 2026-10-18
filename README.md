# Pocket Squatch

A virtual pet for the ESP32-S3: a baby Bigfoot in a forest clearing on a 4" touchscreen. It hatches from a mossy egg, grows up over about ten days, and its adult form depends on how you raised it. The heart of the game is **Trust**: a neglected Squatch hides behind the trees, a well-cared-for one comes out and waves.

Design doc: [Bigfoot Pet — Design](https://claude.ai/code/artifact/85ce4636-3c76-49ca-9444-d65b9a6a3e2e)

## Status

Steps 1 to 3 of the build plan: the **simulation core** with a **fast-forward harness**, the **clearing scene** running on the PC in an SDL window with placeholder art, and **firmware** for the Waveshare ESP32-S3-Touch-LCD-4B. The firmware builds but hasn't run on a board yet.

| Folder | What's in it |
| --- | --- |
| `sim/` | The game: meters, Trust, discipline, life stages, adult forms, sickness, running away, generations, DNA and rarity, the trail map, save files. No hardware code; it builds as an ESP-IDF component unchanged. |
| `scene/` | The screen: an anti-aliased software renderer into an RGB565 framebuffer, touch-to-gesture recognition, and the clearing with Squatch, the status strip and the care tray. Also hardware-free, so the board will draw the same pixels. |
| `pc/` | `squatch-pc`: shows the scene in an SDL window, with the mouse as a finger, the real clock and a save file. |
| `firmware/` | The ESP-IDF project for the board: the same sim and scene, with the panel, touch, flash saves and the clock. |
| `harness/` | `squatch-harness`: raises pets at high speed under scripted players to tune the balance. |
| `tests/` | Framework-free tests: the sim (including "every adult form is reachable") and the scene, driven by touch events. |

## Build and run

Needs CMake 3.16+ and a C++17 compiler, plus SDL2 for the PC scene (in WSL: `sudo apt install build-essential cmake libsdl2-dev`). Without SDL2 everything else still builds.

```sh
cmake -S . -B build
cmake --build build -j
./build/squatch-tests
./build/squatch-scene-tests
./build/squatch-pc
```

## The PC scene

`squatch-pc` loads `squatch.sav` from the current folder, catches up on the time it was closed, and runs in real time. With no save it lays a new egg, which hatches after a day (use the speed keys to skip ahead).

| You do | With the mouse | It does |
| --- | --- | --- |
| Open or close the care tray | click the tab at the bottom, or drag up from the bottom edge (down to close) | Feed, Clean, Heal, Play, Light, More (Photos and Trails) |
| Give something | click a tray item, then click Squatch, or drag the item onto it | food, brush, poultice, a pinecone to play catch |
| Pet it | drag slowly across it | only once it trusts you |
| Clean a mess | drag across it, or click it | |
| Answer a call | click the pulsing footprints (top right) or Squatch | |
| Scold | right-click (a two-finger tap) | only fair while it's misbehaving |
| Stats | click or hold the status strip | including its coat, rarity and DNA code |
| Trail map | More, then Trails | nearby Wi-Fi as places to explore; click one to send it foraging |
| Leave food out while it's away | drop food on the stump | three separate days bring it back |

Dev keys: `]` and `[` change the speed of time (1x, 60x, 600x, 3600x), `P` saves a screenshot. `--new` starts over, `--save FILE` picks another save, `--zoom 2` makes the window bigger, and `--shot FILE.bmp` draws one frame without opening a window.

Where Squatch stands follows Trust: hiding behind the left tree with only its eyes showing, wandering the back of the clearing, or out front, waving. The sky follows the clock, from dawn to stars and a campfire in the evening. The art is placeholder shapes at roughly the planned 192×192 sprite size, ready to be swapped for real sprites in step 4. Play is a stand-in game of catch until the mini-games arrive in step 5.

## DNA and rarity

Every egg gets a genome of 16 one-byte genes (`sim/include/squatch/dna.hpp`), rolled when it's laid. They decide:

| Gene | What it does |
| --- | --- |
| Hue and shade | Fur colour, from light tan to dark brown |
| Shag | Neat, or tufts down the sides and a mop on top (0 to 3) |
| Build | Slim to stocky |
| Markings | None, a pale chest blaze, a dark eye mask, or speckles |
| Shy, playful, appetite | Up to ±15% on how fast it learns to trust you, gets bored, and gets hungry |
| Rarity | Common 60%, Uncommon 25% (charcoal and blond coats too), Rare 10%, Epic 4%, Mythic 1% |

Rare pets are a rusty **Swamp** or a white **Snowcoat**, Epic ones an **Albino** or a Snowcoat, and a Mythic is always **Golden**, with a sparkle. A rare egg glows faintly before it hatches, and the hatching toast says what came out. How it's raised still shows: the adult form tints an ordinary coat a little, and Elders go grey.

When an Elder lays the next egg, each gene comes from the parent or is rolled fresh, half and half, so families look alike. A parent that grew into a Guardian or a Legend rolls the rarity gene twice and keeps the better one, so good care slowly breeds rarer pets.

## The trail map

**More → Trails** opens the map and listens for Wi-Fi networks, without joining any. Each network name is hashed into a fixed forest place, like *Whispering Falls* or *Old Ranger Cabin* (eight kinds), and its signal strength sets how far away it is. Only the hashes are stored, never the names. Up to five of the strongest show at a time, with a **NEW** badge on places it has never been, and **LOOK AGAIN** scans again. Taking the board somewhere new finds new trails.

Tap a trail to send Squatch foraging: half an hour for a near one, an hour for a walk, two hours for a far one. It won't go while it's a Baby, still hiding from you, sick, misbehaving or hungry. While it's out, the clearing shows its footprints and a countdown, and it comes back with footprints (more the first time it visits a place), a little happiness, sometimes a sighting photo (best from caves and lookouts), berries in its belly from a berry patch, and now and then burrs to clean up. The map and the places it has been carry over to the next generation, like the photos.

On the PC the scan is pretend: a handful of made-up networks with signals that change from scan to scan.

## On the board

The firmware targets the [Waveshare ESP32-S3-Touch-LCD-4B](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B) and ESP-IDF v5.4 (the version Waveshare's demos use). It uses the same `sim/` and `scene/` folders as the PC, so the board shows exactly the pixels `squatch-pc` does.

```sh
. ~/esp/esp-idf/export.sh
idf.py -C firmware menuconfig        # Pocket Squatch: Wi-Fi for the clock, and your time zone
idf.py -C firmware build flash monitor
```

**Two boards.** In menuconfig, *Pocket Squatch > Board* picks the [ESP32-S3-Touch-LCD-4B](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B) (the default, the scene pixel for pixel) or the [ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8), which shows the 480x480 scene scaled to 368x368 in the middle of its 368x448 screen, with black bars above and below. Both of its revisions are told apart at boot; the V2 (CO5300 + CST816) touch chip only sees one finger, so there's no two-finger scold on it. The AMOLED code follows [pocket-tank](https://github.com/mediacutlet/pocket-tank) (MIT), which runs on that board. To build for it without menuconfig: `idf.py -C firmware -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.amoled18" build`.

In WSL the board's USB port has to be passed through first (`usbipd attach --wsl` from Windows); otherwise flash from Windows with the same ESP-IDF version.

| Part | How |
| --- | --- |
| Display | ST7701 over the 16-bit RGB bus, two frame buffers in PSRAM: the scene draws into one while the other is on screen, then they swap at the next refresh. |
| Touch | GT911, read every frame (its interrupt line isn't wired to the ESP32). Fingers become the scene's touch events, so gestures work exactly as on the PC. |
| Saves | One NVS blob in flash, in the same format as `squatch.sav`, written after every care action and every 5 minutes. |
| Clock | The PCF85063 RTC keeps time on the battery. With Wi-Fi configured, the board fetches the time over NTP at start-up, stores it in the RTC and turns Wi-Fi off. The time zone is a POSIX TZ string (default US Mountain). |
| Battery | The AXP2101 power chip is read every 2 seconds: a battery icon in the status strip (green, gold under 30%, red and pulsing at 15% or less, a bolt while charging), the percentage on the stats page, a one-time "plug me in" warning, and the charge level in the serial log. At start-up it sets a gentle charge current for the board's usual cell (150 mA on the 1.8" AMOLED, 300 mA on the 4" board); the chip's own default is over an amp. Nothing shows without a battery. On the PC, `--battery 64c` pretends one (c = charging). |
| Naming | A couple of seconds after hatching (or at start-up, for any pet without a name) a keyboard opens: big letter keys, a space, delete, SUGGEST (woodsy names, the first picked by its DNA) and DONE. Up to 10 letters. Tap the name on the stats page to change it. Messages then use it: "MOSS'S FULL", "MOSS RAN OFF INTO THE WOODS". |
| Settings | Tray > More > Settings: brightness (20–100%, default 80%), screen off after 15 s, 30 s, 1, 2, 5 or 10 min, or never (default 1 min), and volume (off, quiet, medium, loud; default medium). They're kept in flash apart from the pet, so a new egg doesn't reset them. When the screen times out the panel goes dark and drawing stops; the pet's time, saves and the battery check carry on. The tap that wakes it does nothing else. News wakes it too: hatching, a knock, getting sick, running off or coming back, a new egg, the end of a forage trip, low battery. On the PC the window dims and goes black the same way, and the settings live in `squatch.sav.settings`. |
| Sound | Short synthesized cues (`scene/src/sounds.cpp`, no sound files): a tick for buttons, eating, brushing and cleaning, healing, play, the light, happy chirps, sneezes, a sulk, knocks when it calls, hatching, growing up, a new egg, getting sick, running off and coming back, back from a trail, a new photo, and a low-battery warning. The scene queues cues (`Scene::takeCue`); the PC plays them through SDL, the board through I2S to the ES8311 and its amplifier (`firmware/main/audio.cpp`, register sequences from pocket-tank). The codec and amplifier are powered only while sounds play, and down 2 s after the last. With the screen off it stays quiet except for a knock. |
| Weather | With a Wi-Fi network and a location set in menuconfig (Pocket Squatch > Weather: latitude and longitude), the board asks Open-Meteo (free, no account) for the current weather about once an hour, joining Wi-Fi for a few seconds each time. The clearing shows it: clouds, fog, drizzle, rain, snow (snow cover and snowy trees) and storms (lightning and thunder). A change of sky gets a message, and the stats page shows the sky and temperature (Fahrenheit by default, a menuconfig option). The PC pretends with `--weather snow` (or clear, cloudy, fog, drizzle, rain, storm). |
| Sleep | With the screen off, the chip slows down and naps between touch checks (ESP-IDF automatic light sleep, switched on by the Pocket Squatch > Sleep option), and the AMOLED panel goes into its own sleep mode. While a computer's serial monitor is connected over USB it stays awake so the log keeps working: the savings show on battery. The 4-inch LCD board's panel needs constant feeding, so there the chip only slows down. |
| Power key | The PWR key is read through the AXP2101. A press turns the screen off, another turns it back on. Holding it about 1.5 s saves the pet and settings, shows "SEE YOU SOON" and switches the board off; a tap turns it on again (the chip is set to power on after 128 ms rather than its default hold). The chip's own forced power-off is moved to a 10 s hold so the firmware always saves first. On USB power the board stays on. |
| Trails | Opening the trail map starts a background Wi-Fi scan (about two seconds, then Wi-Fi goes off again). The clock sync and scans take turns with the radio (`radio.cpp`). Turn it off in menuconfig with *Trail map: listen for nearby Wi-Fi networks*. |

Pins and the panel's start-up commands come from Waveshare's board support package (Apache-2.0). That package also brings LVGL, which the scene doesn't need, so `firmware/main/board.c` is a small copy of just the display, backlight and touch parts.

The serial log reports frames per second and milliseconds per draw every 10 seconds. That's the first thing to check on real hardware.

## Balance harness

`./build/squatch-harness` raises 20 pets per scripted player and shows which adult forms came out:

```
player        style                                      Guardian  Runner    Couch     Hermit    Legend
devoted       hourly, fair, healthy food                 19        1         0         0         0
pushover      hourly, never scolds, answers every whim   1         19        0         0         0
snacker       hourly, but mostly berries                 0         0         20        0         0
busy          three times a day, basics only             0         0         0         20        0
photographer  devoted, plus Blurry Photo every check-in  0         0         0         0         20
```

Follow one pet day by day, with every event:

```sh
./build/squatch-harness --player busy --events --seed 7
./build/squatch-harness --player devoted --days 27    # through Elder and the next egg
```

## How the sim works

- `Sim::tick(seconds)` moves the world forward in fixed one-minute steps. Catching up after the device was off is the same call with a bigger number, and gives exactly the same result as running in real time (there's a test for that).
- All randomness comes from a seeded generator stored in the save, so a run can be replayed. The DNA comes from it too.
- `scoutTrails()` takes a scan's results and `sendForaging()` sends it off; while it's out it gets hungry but nothing else happens to it until it's back.
- Saves are version 2. A version 1 save (from before DNA) still loads: new fields only ever go at the end of `State`, so an old save is a prefix, and the pet gets DNA when it loads.
- Every balance number is in `Tuning` (`sim/include/squatch/sim.hpp`). Change one, rerun the harness, and see what it does to the adult-form spread.
- Actions (`feed`, `clean`, `medicine`, `answerCall`, `scold`, `pet`, `brush`, `lights`, `finishedGame`, `leaveFoodOut`) return `Ok`, `NotNow` or `Refused`, so the UI can react.
- The sim queues `Event`s (hatched, got sick, ran away, photo earned…) for the UI and sound to drain each frame.

## Life cycle

Egg (1 hour) → Baby (to day 3) → Child (to day 6) → Teen (to day 10) → Adult → Elder (day 20) → the Elder lays the next egg (day 25). The sighting-photo gallery and footprints carry over between generations.

Adult form at day 10, checked in order: full photo gallery → **Legend**; Trust under 30 → **Hermit**; mostly berries → **Couch Squatch**; discipline under 40 → **Ridge Runner**; Trust 65+ and discipline 40+ → **Guardian**; otherwise **Ridge Runner**. An adult that completes the gallery later also becomes a Legend.

It never dies: untreated sickness or long neglect makes it run away, and leaving food out on three separate days brings it back.

## Next steps

1. **Finish step 3 on a real board:** check the picture, touch orientation, frame rate, saves surviving a power cycle, and the clock.
2. **Step 4:** real art for the egg, baby and child.
3. Not yet in the scene: the naming screen at hatching and the half-speed meter setting.
4. Later: Bluetooth visitors, so two boards' Squatches can meet (needs a second board).
4. **Step 6:** deep sleep through the AXP2101 (battery level, screen-off and sound are done), then the case.

## Android

`android/` builds the same game as a phone app: SDL2 (fetched by `android/fetch-sdl.sh`) runs
`android/app/src/main/cpp/main.cpp`, the Android counterpart of `pc/main.cpp`. GitHub builds it on every
push and keeps the newest `pocket-squatch.apk` on the repository's **android** release, so nothing needs
installing on the PC. On the phone, download the APK from that release page and open it (Android asks once
to allow installs from your browser). Each build installs over the last and keeps the pet, provided the
`SQUATCH_KEYSTORE_B64` and `SQUATCH_KEYSTORE_PASSWORD` repository secrets hold the signing key.

The phone version uses real time and catches up after being closed, two-finger touch, sound, the phone's
battery for the icon, and its own screen timeout. Still to come: real Wi-Fi trails (pretend scans for now)
and notifications when the pet needs you while the app is closed.
