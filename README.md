# Pocket Squatch

A virtual pet for the ESP32-S3: a baby Bigfoot in a forest clearing on a 4" touchscreen. It hatches from a mossy egg, grows up over about ten days, and its adult form depends on how you raised it. The heart of the game is **Trust**: a neglected Squatch hides behind the trees, a well-cared-for one comes out and waves.

Design doc: [Bigfoot Pet — Design](https://claude.ai/code/artifact/85ce4636-3c76-49ca-9444-d65b9a6a3e2e)

## Status

Step 1 of the build plan: the **simulation core** and a **fast-forward harness**, both pure C++ that runs on a PC. No display or board code yet.

| Folder | What's in it |
| --- | --- |
| `sim/` | The game: meters, Trust, discipline, life stages, adult forms, sickness, running away, generations, save files. No hardware code; later it builds as an ESP-IDF component unchanged. |
| `harness/` | `squatch-harness`: raises pets at high speed under scripted players to tune the balance. |
| `tests/` | Framework-free tests, including "every adult form is reachable". |

## Build and run

Needs CMake 3.16+ and a C++17 compiler (in WSL: `sudo apt install build-essential cmake`).

```sh
cmake -S . -B build
cmake --build build -j
./build/squatch-tests
./build/squatch-harness
```

The harness raises 20 pets per scripted player and shows which adult forms came out:

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
- All randomness comes from a seeded generator stored in the save, so a run can be replayed.
- Every balance number is in `Tuning` (`sim/include/squatch/sim.hpp`). Change one, rerun the harness, and see what it does to the adult-form spread.
- Actions (`feed`, `clean`, `medicine`, `answerCall`, `scold`, `pet`, `brush`, `lights`, `finishedGame`, `leaveFoodOut`) return `Ok`, `NotNow` or `Refused`, so the UI can react.
- The sim queues `Event`s (hatched, got sick, ran away, photo earned…) for the UI and sound to drain each frame.

## Life cycle

Egg (1 day) → Baby (to day 3) → Child (to day 6) → Teen (to day 10) → Adult → Elder (day 20) → the Elder lays the next egg (day 25). The sighting-photo gallery and footprints carry over between generations.

Adult form at day 10, checked in order: full photo gallery → **Legend**; Trust under 30 → **Hermit**; mostly berries → **Couch Squatch**; discipline under 40 → **Ridge Runner**; Trust 65+ and discipline 40+ → **Guardian**; otherwise **Ridge Runner**. An adult that completes the gallery later also becomes a Legend.

It never dies: untreated sickness or long neglect makes it run away, and leaving food out on three separate days brings it back.

## Next steps

1. **Step 2:** an SDL scene on the PC: the clearing, Squatch idling, the care tray, touch via mouse.
2. **Step 3:** port to the Waveshare ESP32-S3-Touch-LCD-4 (display, touch, flash saves, NTP clock).
