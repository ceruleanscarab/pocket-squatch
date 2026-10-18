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

// Where things go on the phone's screen, in real pixels: the clearing as large as fits in
// a portrait screen, drawn at 2x or 3x for sharpness, inside a wooden handheld frame.
struct Layout {
    int w = 0, h = 0;            // the whole screen
    int sx = 0, sy = 0, sq = 0;  // the clearing's square
    int k = 1;                   // the scene's drawing scale
    int bx = 0, by = 0, bw = 0, bh = 0, titleH = 0;

    void fit(int width, int height) {
        w = width;
        h = height;
        sq = std::min(static_cast<int>(w * 0.9f), static_cast<int>(h * 0.62f));
        if (w > h) sq = std::min(static_cast<int>(h * 0.8f), static_cast<int>(w * 0.6f));  // landscape, just in case
        k = std::max(1, std::min(3, (sq + kSize - 1) / kSize));
        const int m = sq / 20;
        titleH = w > h ? h / 10 : static_cast<int>(h * 0.14f);
        bw = sq + 2 * m;
        bh = sq + 2 * m;
        bx = (w - bw) / 2;
        by = w > h ? titleH : std::max(titleH, (h - bh) / 2 - h / 40);  // a little above the middle
        if (w <= h) titleH = by;
        sx = bx + m;
        sy = by + m;
    }
    // A screen point to the scene's 480x480; false outside the clearing.
    bool toScene(float px, float py, int& x, int& y) const {
        const float lx = (px - sx) * kSize / sq, ly = (py - sy) * kSize / sq;
        x = std::max(0, std::min(kSize - 1, static_cast<int>(lx)));
        y = std::max(0, std::min(kSize - 1, static_cast<int>(ly)));
        return lx >= 0 && ly >= 0 && lx < kSize && ly < kSize;
    }
};

// The handheld around the clearing: a forest-night backdrop, a wooden case with a dark
// bezel, the title above and a row of pines along the bottom. Drawn once per screen size.
// A smooth vertical gradient straight into the 16-bit pixels, dithered so it doesn't band.
void ditheredGradient(uint16_t* px, int w, int h, Rgb top, Rgb bottom) {
    static const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int y = 0; y < h; y++) {
        const float t = static_cast<float>(y) / std::max(1, h - 1);
        const float r = ((top >> 16) & 255) + (static_cast<float>((bottom >> 16) & 255) - ((top >> 16) & 255)) * t;
        const float g = ((top >> 8) & 255) + (static_cast<float>((bottom >> 8) & 255) - ((top >> 8) & 255)) * t;
        const float b = (top & 255) + (static_cast<float>(bottom & 255) - (top & 255)) * t;
        for (int x = 0; x < w; x++) {
            const float d = (kBayer[y & 3][x & 3] + 0.5f) / 16.0f;
            const int r5 = std::min(31, static_cast<int>(r / 255 * 31 + d));
            const int g6 = std::min(63, static_cast<int>(g / 255 * 63 + d));
            const int b5 = std::min(31, static_cast<int>(b / 255 * 31 + d));
            px[y * w + x] = static_cast<uint16_t>(r5 << 11 | g6 << 5 | b5);
        }
    }
}

void drawFrame(Canvas& c, const Layout& L) {
    ditheredGradient(c.pixels(), L.w, L.h, 0x1E3A2B, 0x0C1A13);
    for (int i = 0; i < 40; i++) {  // a few stars
        const float x = static_cast<float>((i * 977) % L.w), y = static_cast<float>((i * 613) % std::max(1, L.titleH));
        c.fillCircle(x, y, 1.5f + (i % 3), 0xF3EBD9, 0.35f);
    }
    // Pines along the bottom.
    const float base = static_cast<float>(L.h), unit = L.w / 9.0f;
    for (int i = -1; i < 11; i++) {
        const float x = i * unit + (i % 2) * unit * 0.4f, tall = unit * (1.4f + 0.5f * ((i * 7) % 3));
        c.fillTriangle(x - unit * 0.55f, base, x + unit * 0.55f, base, x, base - tall, i % 2 ? 0x0F2219 : 0x132A1F);
    }
    // The case: wood, a lighter edge on top, grain, and the bezel.
    const float r = L.bw * 0.07f;
    c.fillRoundRect(L.bx + 6, L.by + 10, L.bw, L.bh, r, 0x000000, 0.35f);  // its shadow
    c.fillRoundRect(L.bx, L.by, L.bw, L.bh, r, 0x6B4A2F);
    c.fillRoundRect(L.bx + 4, L.by + 4, L.bw - 8, L.bh * 0.5f, r * 0.9f, 0x7E5836, 0.6f);
    for (int i = 1; i < 9; i++) {
        const float y = L.by + L.bh * i / 9.0f;
        c.line(L.bx + r * 0.6f, y, L.bx + L.bw - r * 0.6f, y + ((i % 3) - 1) * 4.0f, 2, 0x5A3D26, 0.45f);
    }
    const float pad = L.sq * 0.012f;
    c.fillRoundRect(L.sx - pad, L.sy - pad, L.sq + 2 * pad, L.sq + 2 * pad, pad * 2, 0x0B0F0C);
    // The title, as big as fits.
    const char* title = "POCKET SQUATCH";
    int ts = std::max(2, static_cast<int>(L.w * 0.78f) / std::max(1, Canvas::textWidth(title, 1)));
    ts = std::min(ts, std::max(2, L.titleH / 12));
    const float ty = (L.titleH - Canvas::textHeight(ts)) / 2.0f;
    c.text(L.w / 2.0f + ts * 0.5f, ty + ts * 0.5f, title, ts, 0x000000, Align::Center, 0.5f);
    c.text(L.w / 2.0f, ty, title, ts, 0xF2C14E, Align::Center);
    // Footprints walking off under the case.
    const float fy = L.by + L.bh + (L.h - L.by - L.bh) * 0.35f, step = L.w / 7.0f, fs = L.w / 90.0f;
    for (int i = 0; i < 5; i++) {
        const float x = step * (1.2f + i), y = fy + (i % 2 ? -fs * 2.5f : fs * 2.5f);
        c.fillEllipse(x, y, fs * 1.6f, fs * 2.4f, 0x3B2A1E, 0.8f);
        for (int t = -1; t <= 1; t++) c.fillCircle(x + t * fs * 1.1f, y - fs * 3.2f, fs * 0.55f, 0x3B2A1E, 0.8f);
    }
}

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
    // The layout, the frame and the scene's texture, rebuilt whenever the screen size changes.
    Layout L;
    SDL_Texture* tex = nullptr;
    SDL_Texture* frameTex = nullptr;
    std::vector<uint16_t> fb, frame;
    auto relayout = [&]() {
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        if (w == L.w && h == L.h && tex) return;
        L.fit(w, h);
        if (tex) SDL_DestroyTexture(tex);
        if (frameTex) SDL_DestroyTexture(frameTex);
        const int px = kSize * L.k;
        tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, px, px);
        fb.assign(static_cast<size_t>(px) * px, 0);
        frame.assign(static_cast<size_t>(w) * h, 0);
        Canvas fc(frame.data(), w, h);
        drawFrame(fc, L);
        frameTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC, w, h);
        SDL_UpdateTexture(frameTex, nullptr, frame.data(), w * 2);
        SDL_Log("screen %dx%d: the clearing at %d px, drawn at %dx", w, h, L.sq, L.k);
    };
    relayout();

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
    // A finger in screen terms to the scene's; a press outside the clearing isn't a touch.
    auto touch = [&](TouchPhase ph, int slot, float nx, float ny) -> bool {
        int x = 0, y = 0;
        const bool inside = L.toScene(nx * L.w, ny * L.h, x, y);
        if (ph == TouchPhase::Down && !inside) return false;
        scene.touch(TouchEvent{ph, static_cast<uint8_t>(slot), static_cast<int16_t>(x), static_cast<int16_t>(y),
                               SDL_GetTicks()});
        return true;
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
                    if (slot >= 0 && !touch(TouchPhase::Down, slot, e.tfinger.x, e.tfinger.y))
                        fingers.down[slot] = false;  // on the frame, not the clearing
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

        relayout();
        Canvas canvas(fb.data(), kSize * L.k, kSize * L.k, L.k);
        if (scene.screenOff()) std::fill(fb.begin(), fb.end(), 0);
        else scene.draw(canvas);
        const Uint8 level = static_cast<Uint8>(255 * scene.brightnessPercent() / 100);
        SDL_SetTextureColorMod(tex, level, level, level);
        SDL_UpdateTexture(tex, nullptr, fb.data(), kSize * L.k * 2);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, frameTex, nullptr, nullptr);
        const SDL_Rect dst{L.sx, L.sy, L.sq, L.sq};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_RenderPresent(renderer);  // vsync paces the loop
    }

    saveAll();
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyTexture(frameTex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
