// Pocket Squatch on Android: the same sim and scene as the PC and the boards, in an SDL
// window that fills the phone's screen (the square clearing, letterboxed).
//
// What this front end owns, like pc/main.cpp and firmware/main/main.cpp do on theirs:
//   - the real clock: the pet lives in real time, and catches up on the time the app was
//     closed or in the background when it comes back
//   - touch: up to two fingers, mapped onto the 480x480 scene
//   - the save and the settings, in the app's private storage
//   - sound, through SDL audio
//   - the battery icon, from the phone's own battery
// The trail map's Wi-Fi scan is pretend for now, as on the PC.
#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "squatch/canvas.hpp"
#include "squatch/save.hpp"
#include "squatch/scene.hpp"
#include "squatch/sim.hpp"
#include "squatch/sounds.hpp"

using namespace squatch;

namespace {

constexpr int kSize = Scene::kSize;
constexpr uint32_t kAutosaveMs = 60000;
constexpr uint32_t kBatteryEveryMs = 5000;

int64_t wallNow() { return static_cast<int64_t>(std::time(nullptr)); }

int32_t localTzOffset() {
    std::time_t now = std::time(nullptr);
    std::tm utc = *std::gmtime(&now);
    utc.tm_isdst = -1;
    return static_cast<int32_t>(std::difftime(now, std::mktime(&utc)));
}

bool readSave(const std::string& path, State& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> buf(kSaveBytes + 1);
    size_t n = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    LoadResult r = load(buf.data(), n, out);
    if (r != LoadResult::Ok) SDL_Log("%s: %s, starting a new egg", path.c_str(), toString(r));
    return r == LoadResult::Ok;
}

void writeSave(const std::string& path, const State& s) {
    std::vector<uint8_t> buf(kSaveBytes);
    size_t n = save(s, buf.data(), buf.size());
    std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    bool ok = std::fwrite(buf.data(), 1, n, f) == n;
    ok = std::fclose(f) == 0 && ok;
    if (ok) std::rename(tmp.c_str(), path.c_str());
}

bool readSettings(const std::string& path, Scene::Settings& out) {
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    int b = 0, t = 0, v = out.volume;
    bool ok = std::fscanf(f, "brightness %d timeout %d volume %d", &b, &t, &v) >= 2;
    std::fclose(f);
    if (ok) {
        out.brightness = static_cast<uint8_t>(std::max(0, b));
        out.timeout = static_cast<uint8_t>(std::max(0, t));
        out.volume = static_cast<uint8_t>(std::max(0, v));
    }
    return ok;
}

void writeSettings(const std::string& path, const Scene::Settings& s) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "brightness %d timeout %d volume %d\n", s.brightness, s.timeout, s.volume);
    std::fclose(f);
}

const char* const kPretendNetworks[] = {
    "Squatchnet",   "Ranger Station 5G", "Pinecone Cafe Guest", "Old Mill Library",
    "Basecamp-2.4", "FBI Surveillance Van", "Trailhead Brewing", "Lookout Tower",
};

void pretendScan(Sim& sim) {
    Sighting seen[8];
    size_t n = 0;
    for (const char* name : kPretendNetworks) {
        if (std::rand() % 3 == 0) continue;
        seen[n++] = Sighting{trailHash(name), static_cast<int8_t>(-38 - std::rand() % 55)};
    }
    sim.scoutTrails(seen, n);
}

Synth gSynth;
void audioCallback(void*, Uint8* stream, int len) { gSynth.render(reinterpret_cast<int16_t*>(stream), len / 2); }

// SDL's finger ids are long numbers; the scene wants finger 0 and 1.
struct Fingers {
    SDL_FingerID ids[2] = {0, 0};
    bool down[2] = {false, false};
    int slotFor(SDL_FingerID id, bool claim) {
        for (int i = 0; i < 2; i++)
            if (down[i] && ids[i] == id) return i;
        if (!claim) return -1;
        for (int i = 0; i < 2; i++) {
            if (!down[i]) {
                down[i] = true;
                ids[i] = id;
                return i;
            }
        }
        return -1;  // a third finger: ignored
    }
};

}  // namespace

int main(int, char**) {
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");  // fingers only, no pretend mouse
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_EnableScreenSaver();  // let the phone's own screen timeout work

    char* pref = SDL_GetPrefPath("ceruleanscarab", "PocketSquatch");
    const std::string dir = pref ? pref : "";
    SDL_free(pref);
    const std::string savePath = dir + "squatch.sav", settingsPath = dir + "squatch.settings";

    // ---- the pet: load and catch up, or lay a first egg
    Sim sim;
    const int64_t now = wallNow();
    State loaded;
    if (readSave(savePath, loaded)) {
        sim.mutableState() = loaded;
        sim.mutableState().tzOffset = localTzOffset();
        if (now > loaded.now) sim.tick(now - loaded.now);
    } else {
        sim.newGame(now, localTzOffset(), static_cast<uint64_t>(now) ^ SDL_GetPerformanceCounter());
        writeSave(savePath, sim.state());
    }
    std::srand(static_cast<unsigned>(now));

    Scene scene(sim);
    Scene::Settings settings;
    if (readSettings(settingsPath, settings)) scene.setSettings(settings);

    // ---- screen and sound
    SDL_Window* window = SDL_CreateWindow("Pocket Squatch", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kSize,
                                          kSize, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)
                                    : nullptr;
    if (!renderer) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, kSize, kSize);
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, kSize, kSize);
    std::vector<uint16_t> fb(kSize * kSize);
    Canvas canvas(fb.data(), kSize, kSize);

    SDL_AudioDeviceID audio = 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want{}, got{};
        want.freq = Synth::kRate;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512;
        want.callback = audioCallback;
        audio = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
        if (audio) SDL_PauseAudioDevice(audio, 0);
    }

    Fingers fingers;
    int64_t lastWall = wallNow();
    uint32_t lastSave = SDL_GetTicks(), lastBattery = 0, scanDoneAt = 0;
    bool running = true, background = false;

    auto saveAll = [&]() {
        writeSave(savePath, sim.state());
        writeSettings(settingsPath, scene.settings());
        lastSave = SDL_GetTicks();
    };
    auto touch = [&](TouchPhase ph, int slot, float nx, float ny) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        float lx = 0, ly = 0;
        SDL_RenderWindowToLogical(renderer, static_cast<int>(nx * w), static_cast<int>(ny * h), &lx, &ly);
        int x = std::max(0, std::min(kSize - 1, static_cast<int>(lx)));
        int y = std::max(0, std::min(kSize - 1, static_cast<int>(ly)));
        scene.touch(TouchEvent{ph, static_cast<uint8_t>(slot), static_cast<int16_t>(x), static_cast<int16_t>(y),
                               SDL_GetTicks()});
    };

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                case SDL_APP_TERMINATING: running = false; break;
                case SDL_APP_WILLENTERBACKGROUND:
                    saveAll();
                    if (audio) SDL_PauseAudioDevice(audio, 1);
                    background = true;
                    break;
                case SDL_APP_DIDENTERFOREGROUND:
                    if (audio) SDL_PauseAudioDevice(audio, 0);
                    sim.mutableState().tzOffset = localTzOffset();
                    scene.wake();
                    background = false;
                    break;
                case SDL_FINGERDOWN: {
                    int slot = fingers.slotFor(e.tfinger.fingerId, true);
                    if (slot >= 0) touch(TouchPhase::Down, slot, e.tfinger.x, e.tfinger.y);
                    break;
                }
                case SDL_FINGERMOTION: {
                    int slot = fingers.slotFor(e.tfinger.fingerId, false);
                    if (slot >= 0) touch(TouchPhase::Move, slot, e.tfinger.x, e.tfinger.y);
                    break;
                }
                case SDL_FINGERUP: {
                    int slot = fingers.slotFor(e.tfinger.fingerId, false);
                    if (slot >= 0) {
                        touch(TouchPhase::Up, slot, e.tfinger.x, e.tfinger.y);
                        fingers.down[slot] = false;
                    }
                    break;
                }
                case SDL_KEYDOWN:
                    if (e.key.keysym.sym == SDLK_AC_BACK) {
                        saveAll();
                        SDL_AndroidBackButton();  // leave the app the usual way; the pet carries on
                    }
                    break;
                default: break;
            }
        }
        if (background) {
            SDL_Delay(100);
            continue;
        }

        const uint32_t ms = SDL_GetTicks();
        const int64_t wall = wallNow();
        if (wall > lastWall) sim.tick(wall - lastWall);  // real seconds, background time included
        lastWall = wall;

        if (ms - lastBattery >= kBatteryEveryMs || lastBattery == 0) {
            int secs = 0, pct = -1;
            SDL_PowerState st = SDL_GetPowerInfo(&secs, &pct);
            scene.setBattery(st == SDL_POWERSTATE_NO_BATTERY || st == SDL_POWERSTATE_UNKNOWN ? -1 : pct,
                             st == SDL_POWERSTATE_CHARGING);
            lastBattery = ms ? ms : 1;
        }

        if (scene.takeScanRequest()) scanDoneAt = ms + 1500;
        if (scanDoneAt && ms >= scanDoneAt) {
            pretendScan(sim);
            scanDoneAt = 0;
        }
        scene.update(ms);
        for (Cue c; scene.takeCue(c);) {
            if (!audio) continue;
            SDL_LockAudioDevice(audio);
            gSynth.play(c, scene.volume());
            SDL_UnlockAudioDevice(audio);
        }
        if (scene.takeSettingsChanged()) writeSettings(settingsPath, scene.settings());
        if (scene.takeSaveRequest() || ms - lastSave >= kAutosaveMs) saveAll();

        if (scene.screenOff()) std::fill(fb.begin(), fb.end(), 0);
        else scene.draw(canvas);
        const Uint8 level = static_cast<Uint8>(255 * scene.brightnessPercent() / 100);
        SDL_SetTextureColorMod(tex, level, level, level);
        SDL_UpdateTexture(tex, nullptr, fb.data(), kSize * 2);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, tex, nullptr, nullptr);
        SDL_RenderPresent(renderer);  // vsync paces the loop
    }

    saveAll();
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
