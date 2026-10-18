// Pocket Squatch simulation core.
//
// Pure game logic: no display, touch, audio or clock access. The world only moves
// when tick(seconds) is called, so the same code runs on the ESP32-S3, in the PC
// scene, and in the fast-forward harness. Catching up after the device was off is
// just tick() with a large number.
#pragma once

#include <cstdint>
#include <cstddef>

#include "squatch/dna.hpp"
#include "squatch/trails.hpp"

namespace squatch {

// ---------------------------------------------------------------- enums

enum class Stage : uint8_t { Egg, Baby, Child, Teen, Adult, Elder, Away };

enum class AdultForm : uint8_t { None, Guardian, RidgeRunner, CouchSquatch, Hermit, Legend };

enum class Food : uint8_t { Berries, Fish, Roots };

enum class TrustLevel : uint8_t { Hiding, Wandering, Friendly };

enum class Result : uint8_t {
    Ok,
    NotNow,     // wrong stage, asleep, away, or nothing to do
    Refused,    // it said no (full, misbehaving)
};

enum class EventType : uint8_t {
    Hatched,
    StageChanged,       // a = new Stage
    BecameAdult,        // a = AdultForm
    CallStarted,        // a = 1 if genuine (a meter is empty), 0 if it just wants company
    CallIgnored,
    GotSick,
    Recovered,
    Misbehaving,
    RanAway,
    Returned,
    PhotoEarned,        // a = photo index 0..23
    LaidEgg,            // a new generation begins
    WentToSleep,
    WokeUp,
    TrailsScouted,      // a = trails on the map that it has never visited
    WentForaging,
    BackFromForaging,   // a = footprints it brought back
};

struct Event {
    EventType type;
    uint8_t a;
    int64_t at;  // epoch seconds
};

// ---------------------------------------------------------------- tuning

// Every number that decides how the pet feels to raise lives here, so the harness
// can try alternatives without touching the logic.
struct Tuning {
    // stage boundaries, in seconds of age
    int64_t eggEnd = 3600;  // an hour: long enough to wonder about it, short enough to see it hatch
    int64_t babyEnd = 3 * 86400;
    int64_t childEnd = 6 * 86400;
    int64_t teenEnd = 10 * 86400;
    int64_t adultEnd = 20 * 86400;
    int64_t elderEnd = 25 * 86400;  // the Elder lays the next egg

    // meter drain, seconds per heart (halved while asleep)
    float hungerSecsPerHeart = 2 * 3600;
    float happySecsPerHeart = 3 * 3600;

    // sleep window, local hours
    int sleepHour = 21;
    int wakeHour = 7;

    // attention calls
    float callChancePerHour = 0.15f;   // random "I want company" calls while awake
    int64_t callTimeout = 15 * 60;
    int64_t genuineCallCooldown = 2 * 3600;

    // misbehaviour (Child and Teen)
    float misbehaveChancePerHour = 0.06f;
    int64_t misbehaveTimeout = 30 * 60;

    // sickness
    float sickChancePerHourDirty = 0.05f;  // per hour with a mess older than an hour
    float sickChancePerHourStarving = 0.04f;
    int64_t sickRunAwayAfter = 24 * 3600;
    int64_t neglectRunAwayAfter = 24 * 3600;  // Trust 0 and starving this long
    int64_t messOldAfter = 3600;

    // coaxing it back: food left out on this many separate days
    int coaxDaysNeeded = 3;

    // Trust
    float trustPerWellCaredHour = 0.25f;  // awake, fed, content, clean and well
    float trustWandering = 30;
    float trustFriendly = 70;

    // adult form rules (checked in this order after Legend)
    float guardianTrust = 65;
    float guardianDiscipline = 40;
    float hermitTrust = 30;
    float couchSnackShare = 0.5f;   // share of meals that were berries
    float runnerDiscipline = 40;

    // temperament from the DNA: how far each gene can push its meter, either way
    float appetiteEffect = 0.15f;   // hunger drain x0.85 .. x1.15
    float playfulEffect = 0.15f;    // happiness drain
    float shyEffect = 0.15f;        // Trust from steady care x1.15 (bold) .. x0.85 (shy)

    // forage trips, by distance (near, a walk, far)
    int64_t forageSecs[3] = {30 * 60, 60 * 60, 2 * 3600};
    uint32_t forageFootprints[3] = {2, 4, 7};
    float foragePhotoChance[3] = {0.10f, 0.20f, 0.35f};
    uint32_t firstVisitFootprints = 5;
    float forageBurrChance = 0.4f;  // comes back with burrs (a mess to clean)
};

// ---------------------------------------------------------------- state

// Plain data: this whole struct is what gets saved. Bump kSaveVersion when it changes.
struct State {
    char name[24] = {};

    Stage stage = Stage::Egg;
    AdultForm adult = AdultForm::None;
    uint16_t generation = 1;

    int64_t now = 0;        // epoch seconds of the simulated present
    int32_t tzOffset = 0;   // seconds east of UTC, for the sleep window
    int64_t born = 0;       // egg laid
    int32_t pendingSecs = 0;  // leftover under one sim step

    // meters, 0..4 hearts
    float hunger = 4;
    float happiness = 4;
    uint8_t messes = 0;
    int64_t oldestMessAt = 0;

    // hidden traits, 0..100
    float trust = 15;
    float discipline = 50;

    bool asleep = false;
    bool lightsOn = true;
    bool penalizedSleepLights = false;  // once per night
    int32_t sleepLightSecs = 0;         // asleep with the light left on

    bool sick = false;
    int64_t sickSince = 0;
    int64_t starvingAndDistrustSince = 0;  // 0 = not currently

    bool calling = false;
    bool callGenuine = false;
    int64_t callSince = 0;
    int64_t lastCallEnd = 0;  // genuine calls wait genuineCallCooldown after the last one

    bool misbehaving = false;
    int64_t misbehaveSince = 0;
    int64_t lastScold = 0;

    // diet history for the Couch Squatch rule
    uint16_t snacks = 0;
    uint16_t meals = 0;

    // running away
    Stage stageBeforeAway = Stage::Baby;
    int64_t awaySince = 0;
    int64_t lastCoaxDay = -1;
    uint8_t coaxDays = 0;

    // rate limits for small Trust boosts
    int64_t lastPet = 0;
    int64_t lastBrush = 0;

    // collection and currency survive across generations
    uint32_t photos = 0;    // bitmask of 24 sighting photos
    uint32_t footprints = 0;

    uint16_t careMistakes = 0;
    uint64_t rng = 0x9E3779B97F4A7C15ull;

    // ---- added in save version 2. New fields go at the end, so an older save is a
    // prefix of this struct and load() can migrate it (see save.cpp).
    Genome genome{};

    // the trail map: the latest scan's strongest networks. Survives generations.
    static constexpr int kMaxTrails = 5;
    Trail trails[kMaxTrails] = {};
    uint8_t trailCount = 0;

    // landmarks ever visited (a ring of hashes), for "NEW" badges and first-visit rewards
    static constexpr int kVisitedCap = 32;
    uint8_t visitedHead = 0;
    uint16_t trailsVisited = 0;  // all-time count of first visits
    uint32_t visited[kVisitedCap] = {};

    // a forage trip in progress
    bool foraging = false;
    int8_t forageRssi = 0;
    uint32_t forageHash = 0;  // also the last trip's place, once it is back
    int64_t forageUntil = 0;
};

// ---------------------------------------------------------------- sim

class Sim {
public:
    static constexpr int kStepSecs = 60;
    static constexpr int kPhotoCount = 24;
    static constexpr size_t kEventCap = 64;

    Sim() = default;
    explicit Sim(const Tuning& t) : tune_(t) {}

    // Start a brand-new first-generation egg.
    void newGame(int64_t nowEpoch, int32_t tzOffsetSecs, uint64_t seed);

    // Advance the world. Any amount: a frame, an hour, a week powered off.
    void tick(int64_t seconds);

    // ---- player actions (all at the current sim time)
    Result setName(const char* name);
    Result feed(Food f);
    Result clean();
    Result brush();
    Result medicine();
    Result answerCall();
    Result scold();
    Result pet();
    Result lights(bool on);
    // Report a finished mini-game. A won photo game earns a new sighting photo.
    Result finishedGame(bool won, bool photoGame);
    // While it is away: leave food out. Counts once per local day.
    Result leaveFoodOut();
    // A Wi-Fi scan finished: replace the trail map with the strongest networks seen.
    void scoutTrails(const Sighting* seen, size_t n);
    // Send it off to trail i on the map. Refused while it's too shy, too young or unwell.
    Result sendForaging(int trail);

    // ---- queries
    const State& state() const { return s_; }
    State& mutableState() { return s_; }  // for loading saves and tests
    const Tuning& tuning() const { return tune_; }
    TrustLevel trustLevel() const;
    int64_t ageSecs() const { return s_.now - s_.born; }
    int localHour() const;
    int photoCount() const;
    Traits traits() const { return decode(s_.genome); }
    bool trailVisited(uint32_t hash) const;
    int64_t forageSecsLeft() const { return s_.foraging ? s_.forageUntil - s_.now : 0; }

    // Events since the last drain, oldest first.
    size_t drainEvents(Event* out, size_t max);

private:
    void step();  // one kStepSecs slice
    void updateStage();
    AdultForm chooseAdultForm() const;
    void layEgg();
    void emit(EventType t, uint8_t a = 0);
    bool awake() const { return !s_.asleep; }
    bool active() const;  // hatched, present, not an egg
    float rand01();
    bool chancePerHour(float p);
    void addTrust(float d);
    void addDiscipline(float d);
    void awardPhoto(int index);
    void awardRandomPhoto();
    void finishForaging();
    void markVisited(uint32_t hash);

    Tuning tune_{};
    State s_{};
    Event events_[kEventCap]{};
    size_t evHead_ = 0, evCount_ = 0;
};

const char* toString(Stage s);
const char* toString(AdultForm f);
const char* toString(EventType e);

}  // namespace squatch
