// squatch-pc: the clearing in an SDL window, with the mouse standing in for a finger.
//
//   squatch-pc                   load squatch.sav (or lay a new egg) and play in real time
//   squatch-pc --new             start over with a new egg
//   squatch-pc --save FILE       use another save file
//   squatch-pc --zoom 2          bigger window (it can also be resized)
//   squatch-pc --shot FILE.bmp   draw one frame to a BMP and exit, without a window
//   squatch-pc --battery 80      show a pretend battery at 80% (add c for charging: 80c)
//   squatch-pc --weather snow    pretend weather: clear, cloudy, fog, drizzle, rain, snow, storm
//   squatch-pc --date 10-31      pretend it's this month and day, to see the seasons
//
// Sound plays through the PC's speakers, at the volume chosen on the settings page.
// Settings (brightness, screen timeout, volume) are kept next to the save, in FILE.settings.
// Brightness dims the window, and when the screen times out it goes black until a click.
//
// Mouse: the left button is a finger. A right click is a two-finger tap (scold).
// The trail map's Wi-Fi scan is pretend on the PC: a few made-up networks with signals
// that change from scan to scan.
// Dev keys: ] and [ change the speed of time (1x, 60x, 600x, 3600x); P saves a screenshot.
//
// The front end owns everything the scene must not touch: the window, the real clock,
// and the save file. On the board, step 3 swaps these for the panel, GT911 touch,
// the RTC and flash.
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

using namespace squatch;

namespace {

constexpr int kSize = Scene::kSize;
constexpr int kSpeeds[] = {1, 60, 600, 3600};

int64_t wallNow() { return static_cast<int64_t>(std::time(nullptr)); }

// Seconds east of UTC for this machine, daylight saving included.
int32_t localTzOffset() {
    std::time_t now = std::time(nullptr);
    std::tm utc = *std::gmtime(&now);
    utc.tm_isdst = -1;
    return static_cast<int32_t>(std::difftime(now, std::mktime(&utc)));
}

bool readSave(const char* path, State& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<uint8_t> buf(kSaveBytes + 1);
    size_t n = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    LoadResult r = load(buf.data(), n, out);
    if (r != LoadResult::Ok) std::fprintf(stderr, "%s: %s, starting a new egg\n", path, toString(r));
    return r == LoadResult::Ok;
}

void writeSave(const char* path, const State& s) {
    std::vector<uint8_t> buf(kSaveBytes);
    size_t n = save(s, buf.data(), buf.size());
    std::string tmp = std::string(path) + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "can't write %s\n", tmp.c_str());
        return;
    }
    bool ok = std::fwrite(buf.data(), 1, n, f) == n;
    ok = std::fclose(f) == 0 && ok;
    if (!ok) return;
    std::remove(path);  // rename won't replace an existing file on Windows
    std::rename(tmp.c_str(), path);
}

bool readSettings(const std::string& path, Scene::Settings& out) {
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    int b = 0, t = 0, v = out.volume;
    bool ok = std::fscanf(f, "brightness %d timeout %d volume %d", &b, &t, &v) >= 2;  // older files have no volume
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

// SDL pulls samples from the synth on its own thread.
Synth gSynth;
void audioCallback(void*, Uint8* stream, int len) {
    gSynth.render(reinterpret_cast<int16_t*>(stream), len / 2);
}

bool saveBmp(std::vector<uint16_t>& fb, const char* path) {
    SDL_Surface* surf =
        SDL_CreateRGBSurfaceWithFormatFrom(fb.data(), kSize, kSize, 16, kSize * 2, SDL_PIXELFORMAT_RGB565);
    if (!surf) return false;
    bool ok = SDL_SaveBMP(surf, path) == 0;
    SDL_FreeSurface(surf);
    return ok;
}

// Stand-ins for the networks a real scan would find around the house.
const char* const kPretendNetworks[] = {
    "Squatchnet",   "Ranger Station 5G", "Pinecone Cafe Guest", "Old Mill Library",
    "Basecamp-2.4", "FBI Surveillance Van", "Trailhead Brewing", "Lookout Tower",
};

void pretendScan(Sim& sim) {
    Sighting seen[8];
    size_t n = 0;
    for (const char* name : kPretendNetworks) {
        if (std::rand() % 3 == 0) continue;  // not every network answers every time
        seen[n++] = Sighting{trailHash(name), static_cast<int8_t>(-38 - std::rand() % 55)};
    }
    sim.scoutTrails(seen, n);
}

void usage() {
    std::printf("usage: squatch-pc [--new] [--save FILE] [--zoom N] [--shot FILE.bmp] [--battery N[c]] [--weather SKY] [--date MM-DD]\n");
}

}  // namespace

int main(int argc, char** argv) {
    const char* savePath = "squatch.sav";
    const char* shotPath = nullptr;
    bool fresh = false;
    int zoom = 1;
    int battery = -1;  // the PC has no battery unless one is pretended
    bool charging = false;
    Weather weather;
    int month = 0, day = 0;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--new")) fresh = true;
        else if (!std::strcmp(argv[i], "--save") && i + 1 < argc) savePath = argv[++i];
        else if (!std::strcmp(argv[i], "--zoom") && i + 1 < argc) zoom = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shotPath = argv[++i];
        else if (!std::strcmp(argv[i], "--date") && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d-%d", &month, &day) != 2) {
                usage();
                return 2;
            }
        }
        else if (!std::strcmp(argv[i], "--weather") && i + 1 < argc) {
            if (!parseSky(argv[++i], weather.sky)) {
                usage();
                return 2;
            }
            weather.tempC = weather.sky == Sky::Snow ? -2.0f : 12.0f;
            weather.windKmh = 15;
        }
        else if (!std::strcmp(argv[i], "--battery") && i + 1 < argc) {
            const char* v = argv[++i];
            battery = std::atoi(v);
            charging = std::strchr(v, 'c') != nullptr;
        }
        else {
            usage();
            return 2;
        }
    }

    // ---- the pet: load and catch up on the time we were away, or lay a new egg
    Sim sim;
    const int64_t now = wallNow();
    State loaded;
    if (!fresh && readSave(savePath, loaded)) {
        sim.mutableState() = loaded;
        sim.mutableState().tzOffset = localTzOffset();
        if (now > loaded.now) sim.tick(now - loaded.now);  // if a dev speed-up ran ahead, just carry on
        std::printf("loaded %s: %s, generation %u\n", savePath, toString(sim.state().stage),
                    static_cast<unsigned>(sim.state().generation));
    } else {
        sim.newGame(now, localTzOffset(), static_cast<uint64_t>(now) ^ SDL_GetPerformanceCounter());
        std::printf("a new egg\n");
    }

    std::srand(static_cast<unsigned>(now));
    Scene scene(sim);
    scene.setBattery(battery, charging);
    if (weather.sky != Sky::Unknown) scene.setWeather(weather);
    if (month) scene.previewDate(month, day);
    const std::string settingsPath = std::string(savePath) + ".settings";
    Scene::Settings settings;
    if (readSettings(settingsPath, settings)) scene.setSettings(settings);
    std::vector<uint16_t> fb(kSize * kSize);
    Canvas canvas(fb.data(), kSize, kSize);

    if (shotPath) {
        for (uint32_t ms = 0; ms <= 1000; ms += 33) scene.update(ms);
        scene.draw(canvas);
        if (!saveBmp(fb, shotPath)) {
            std::fprintf(stderr, "can't write %s: %s\n", shotPath, SDL_GetError());
            return 1;
        }
        return 0;
    }

    // ---- window
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
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
    if (!audio) std::fprintf(stderr, "no sound: %s\n", SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_Window* window = SDL_CreateWindow("Pocket Squatch", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          kSize * zoom, kSize * zoom, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!renderer) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, kSize, kSize);  // mouse coordinates come back in screen pixels
    SDL_Texture* tex =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, kSize, kSize);

    int speed = 0;
    double carry = 0;  // sim seconds not yet ticked
    uint32_t last = SDL_GetTicks(), lastSave = last, lastTitle = 0;
    uint32_t scanDoneAt = 0;  // a pretend scan in progress finishes at this time
    bool running = true, mouseDown = false;

    auto touch = [&](TouchPhase ph, uint8_t id, int x, int y) {
        scene.touch(TouchEvent{ph, id, static_cast<int16_t>(x), static_cast<int16_t>(y), SDL_GetTicks()});
    };

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        mouseDown = true;
                        SDL_CaptureMouse(SDL_TRUE);
                        touch(TouchPhase::Down, 0, e.button.x, e.button.y);
                    } else if (e.button.button == SDL_BUTTON_RIGHT && !mouseDown) {
                        // two fingers side by side, down and up together
                        touch(TouchPhase::Down, 0, e.button.x - 24, e.button.y);
                        touch(TouchPhase::Down, 1, e.button.x + 24, e.button.y);
                        touch(TouchPhase::Up, 0, e.button.x - 24, e.button.y);
                        touch(TouchPhase::Up, 1, e.button.x + 24, e.button.y);
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (mouseDown) touch(TouchPhase::Move, 0, e.motion.x, e.motion.y);
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT && mouseDown) {
                        mouseDown = false;
                        SDL_CaptureMouse(SDL_FALSE);
                        touch(TouchPhase::Up, 0, e.button.x, e.button.y);
                    }
                    break;
                case SDL_KEYDOWN:
                    if (e.key.keysym.sym == SDLK_RIGHTBRACKET && speed < 3) speed++;
                    if (e.key.keysym.sym == SDLK_LEFTBRACKET && speed > 0) speed--;
                    if (e.key.keysym.sym == SDLK_p) {
                        char name[64];
                        std::snprintf(name, sizeof name, "squatch-%lld.bmp", static_cast<long long>(wallNow()));
                        if (saveBmp(fb, name)) std::printf("saved %s\n", name);
                    }
                    break;
                default: break;
            }
        }

        uint32_t ms = SDL_GetTicks();
        carry += (ms - last) / 1000.0 * kSpeeds[speed];
        last = ms;
        if (carry >= 1) {
            int64_t whole = static_cast<int64_t>(carry);
            sim.tick(whole);
            carry -= static_cast<double>(whole);
        }
        if (scene.takeScanRequest()) scanDoneAt = ms + 1500;  // a real scan takes a second or two
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
        if (scene.takeSaveRequest() || ms - lastSave >= 60000) {
            writeSave(savePath, sim.state());
            lastSave = ms;
        }

        if (scene.takeSettingsChanged()) writeSettings(settingsPath, scene.settings());

        if (scene.screenOff()) std::fill(fb.begin(), fb.end(), 0);
        else scene.draw(canvas);
        const Uint8 level = static_cast<Uint8>(255 * scene.brightnessPercent() / 100);
        SDL_SetTextureColorMod(tex, level, level, level);  // a stand-in for the panel's brightness
        SDL_UpdateTexture(tex, nullptr, fb.data(), kSize * 2);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, tex, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        if (ms - lastTitle >= 500) {
            const State& s = sim.state();
            int64_t local = ((s.now + s.tzOffset) % 86400 + 86400) % 86400;
            char title[128];
            std::snprintf(title, sizeof title, "Pocket Squatch  -  %s, day %d  -  %02d:%02d  -  %dx", toString(s.stage),
                          static_cast<int>(sim.ageSecs() / 86400) + 1, static_cast<int>(local / 3600),
                          static_cast<int>(local % 3600 / 60), kSpeeds[speed]);
            SDL_SetWindowTitle(window, title);
            lastTitle = ms;
        }

        uint32_t spent = SDL_GetTicks() - ms;  // about 30 fps, like the board
        if (spent < 33) SDL_Delay(33 - spent);
    }

    writeSave(savePath, sim.state());
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_Quit();
    return 0;
}
