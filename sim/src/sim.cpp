#include "squatch/sim.hpp"

#include <algorithm>
#include <cstring>

namespace squatch {

namespace {

// Milestone photos; the rest (5..23) come from the Blurry Photo mini-game.
constexpr int kPhotoHatch = 0;
constexpr int kPhotoChild = 1;
constexpr int kPhotoTeen = 2;
constexpr int kPhotoAdult = 3;
constexpr int kPhotoFirstWave = 4;
constexpr int kFirstGamePhoto = 5;  // a full gallery is reachable by adulthood

constexpr uint32_t kAllPhotos = (1u << Sim::kPhotoCount) - 1;

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

}  // namespace

// ---------------------------------------------------------------- setup

void Sim::newGame(int64_t nowEpoch, int32_t tzOffsetSecs, uint64_t seed) {
    s_ = State{};
    s_.now = nowEpoch;
    s_.born = nowEpoch;
    s_.tzOffset = tzOffsetSecs;
    s_.rng = seed ? seed : 0x9E3779B97F4A7C15ull;
    evHead_ = evCount_ = 0;
}

// ---------------------------------------------------------------- time

void Sim::tick(int64_t seconds) {
    if (seconds <= 0) return;
    int64_t total = s_.pendingSecs + seconds;
    while (total >= kStepSecs) {
        s_.now += kStepSecs;
        step();
        total -= kStepSecs;
    }
    s_.pendingSecs = static_cast<int32_t>(total);
}

int Sim::localHour() const {
    int64_t local = s_.now + s_.tzOffset;
    int64_t secOfDay = ((local % 86400) + 86400) % 86400;
    return static_cast<int>(secOfDay / 3600);
}

bool Sim::active() const {
    return s_.stage != Stage::Egg && s_.stage != Stage::Away;
}

void Sim::step() {
    const float dt = static_cast<float>(kStepSecs);

    if (s_.stage == Stage::Away) {
        updateStage();  // it keeps growing up out in the woods
        return;
    }

    updateStage();
    if (!active()) return;

    // ---- sleep
    int h = localHour();
    bool shouldSleep = (tune_.sleepHour > tune_.wakeHour)
                           ? (h >= tune_.sleepHour || h < tune_.wakeHour)
                           : (h >= tune_.sleepHour && h < tune_.wakeHour);
    if (shouldSleep && !s_.asleep) {
        s_.asleep = true;
        s_.sleepLightSecs = 0;
        s_.penalizedSleepLights = false;
        emit(EventType::WentToSleep);
    } else if (!shouldSleep && s_.asleep) {
        s_.asleep = false;
        s_.lightsOn = true;
        emit(EventType::WokeUp);
    }
    if (s_.asleep && s_.lightsOn) {
        s_.sleepLightSecs += kStepSecs;
        if (s_.sleepLightSecs >= 30 * 60 && !s_.penalizedSleepLights) {
            s_.penalizedSleepLights = true;
            s_.happiness = clampf(s_.happiness - 0.5f, 0, 4);
            s_.careMistakes++;
        }
    }

    // ---- meters
    float drain = s_.asleep ? 0.5f : 1.0f;
    float sickMult = s_.sick ? 2.0f : 1.0f;
    s_.hunger = clampf(s_.hunger - drain * dt / tune_.hungerSecsPerHeart, 0, 4);
    s_.happiness = clampf(s_.happiness - drain * sickMult * dt / tune_.happySecsPerHeart, 0, 4);

    bool starving = s_.hunger <= 0.0f;
    bool miserable = s_.happiness <= 0.0f;
    if (starving || miserable) addTrust(-1.0f * dt / (6 * 3600));  // -1 per 6 h

    // Steady, ordinary care is what builds Trust most: awake, fed, content, clean, well.
    bool wellCared = !s_.asleep && s_.hunger >= 2.0f && s_.happiness >= 2.0f && !s_.sick &&
                     !(s_.messes > 0 && (s_.now - s_.oldestMessAt) >= tune_.messOldAfter);
    if (wellCared) addTrust(tune_.trustPerWellCaredHour * dt / 3600.0f);

    // ---- sickness
    bool oldMess = s_.messes > 0 && (s_.now - s_.oldestMessAt) >= tune_.messOldAfter;
    if (!s_.sick) {
        if ((oldMess && chancePerHour(tune_.sickChancePerHourDirty)) ||
            (starving && chancePerHour(tune_.sickChancePerHourStarving))) {
            s_.sick = true;
            s_.sickSince = s_.now;
            s_.careMistakes++;
            addTrust(-5);
            emit(EventType::GotSick);
        }
    }

    // ---- running away
    if (s_.trust <= 0.0f && starving) {
        if (s_.starvingAndDistrustSince == 0) s_.starvingAndDistrustSince = s_.now;
    } else {
        s_.starvingAndDistrustSince = 0;
    }
    bool sickTooLong = s_.sick && (s_.now - s_.sickSince) >= tune_.sickRunAwayAfter;
    bool neglectTooLong = s_.starvingAndDistrustSince != 0 &&
                          (s_.now - s_.starvingAndDistrustSince) >= tune_.neglectRunAwayAfter;
    if (sickTooLong || neglectTooLong) {
        s_.stageBeforeAway = s_.stage;
        s_.stage = Stage::Away;
        s_.awaySince = s_.now;
        s_.coaxDays = 0;
        s_.lastCoaxDay = -1;
        s_.calling = s_.misbehaving = false;
        emit(EventType::RanAway);
        return;
    }

    if (s_.asleep) return;  // no calls or mischief while it sleeps

    // ---- attention calls
    if (s_.calling) {
        if (s_.now - s_.callSince >= tune_.callTimeout) {
            s_.calling = false;
            s_.lastCallEnd = s_.now;
            if (s_.callGenuine) {
                addTrust(-2);
                s_.careMistakes++;
            }  // ignoring a call for company costs nothing
            emit(EventType::CallIgnored);
        }
    } else {
        bool needs = starving || miserable || s_.sick;
        bool cooled = (s_.now - s_.lastCallEnd) >= tune_.genuineCallCooldown;
        if (needs && cooled) {
            s_.calling = true;
            s_.callGenuine = true;
            s_.callSince = s_.now;
            emit(EventType::CallStarted, 1);
        } else if (!needs && chancePerHour(tune_.callChancePerHour)) {
            s_.calling = true;
            s_.callGenuine = false;
            s_.callSince = s_.now;
            emit(EventType::CallStarted, 0);
        }
    }

    // ---- misbehaviour (Child and Teen)
    bool unruly = s_.stage == Stage::Child || s_.stage == Stage::Teen;
    if (s_.misbehaving) {
        if (s_.now - s_.misbehaveSince >= tune_.misbehaveTimeout) {
            s_.misbehaving = false;  // got away with it
            addDiscipline(-3);
        }
    } else if (unruly && chancePerHour(tune_.misbehaveChancePerHour)) {
        s_.misbehaving = true;
        s_.misbehaveSince = s_.now;
        emit(EventType::Misbehaving);
    }

    // ---- first wave
    if (trustLevel() == TrustLevel::Friendly) awardPhoto(kPhotoFirstWave);
}

void Sim::updateStage() {
    int64_t age = ageSecs();
    Stage cur = (s_.stage == Stage::Away) ? s_.stageBeforeAway : s_.stage;
    Stage next = cur;

    if (age >= tune_.elderEnd && cur == Stage::Elder) {
        if (s_.stage != Stage::Away) layEgg();
        return;
    }
    if (age >= tune_.adultEnd) next = Stage::Elder;
    else if (age >= tune_.teenEnd) next = Stage::Adult;
    else if (age >= tune_.childEnd) next = Stage::Teen;
    else if (age >= tune_.babyEnd) next = Stage::Child;
    else if (age >= tune_.eggEnd) next = Stage::Baby;
    else next = Stage::Egg;

    if (next == cur) {
        // A full gallery turns any adult into a Legend.
        if (cur == Stage::Adult && s_.adult != AdultForm::Legend && s_.photos == kAllPhotos) {
            s_.adult = AdultForm::Legend;
            emit(EventType::BecameAdult, static_cast<uint8_t>(s_.adult));
        }
        return;
    }

    // While away, remember how grown-up it is but don't celebrate yet.
    if (s_.stage == Stage::Away) {
        if (next == Stage::Adult && s_.adult == AdultForm::None) s_.adult = chooseAdultForm();
        s_.stageBeforeAway = next;
        return;
    }

    s_.stage = next;
    switch (next) {
        case Stage::Baby:
            emit(EventType::Hatched);
            awardPhoto(kPhotoHatch);
            break;
        case Stage::Child: awardPhoto(kPhotoChild); break;
        case Stage::Teen: awardPhoto(kPhotoTeen); break;
        case Stage::Adult:
            if (s_.adult == AdultForm::None) s_.adult = chooseAdultForm();
            awardPhoto(kPhotoAdult);
            emit(EventType::BecameAdult, static_cast<uint8_t>(s_.adult));
            break;
        default: break;
    }
    emit(EventType::StageChanged, static_cast<uint8_t>(next));
}

AdultForm Sim::chooseAdultForm() const {
    if (s_.photos == kAllPhotos) return AdultForm::Legend;
    if (s_.trust < tune_.hermitTrust) return AdultForm::Hermit;
    int fed = s_.snacks + s_.meals;
    if (fed > 0 && static_cast<float>(s_.snacks) / fed > tune_.couchSnackShare) return AdultForm::CouchSquatch;
    if (s_.discipline < tune_.runnerDiscipline) return AdultForm::RidgeRunner;
    if (s_.trust >= tune_.guardianTrust && s_.discipline >= tune_.guardianDiscipline) return AdultForm::Guardian;
    return AdultForm::RidgeRunner;  // decent but not devoted care: a wild one
}

void Sim::layEgg() {
    State keep = s_;
    s_ = State{};
    s_.generation = static_cast<uint16_t>(keep.generation + 1);
    s_.now = keep.now;
    s_.born = keep.now;
    s_.tzOffset = keep.tzOffset;
    s_.photos = keep.photos;
    s_.footprints = keep.footprints;
    s_.rng = keep.rng;
    emit(EventType::LaidEgg, static_cast<uint8_t>(s_.generation & 0xFF));
}

// ---------------------------------------------------------------- actions

Result Sim::setName(const char* name) {
    if (!name) return Result::NotNow;
    std::strncpy(s_.name, name, sizeof(s_.name) - 1);
    s_.name[sizeof(s_.name) - 1] = '\0';
    return Result::Ok;
}

Result Sim::feed(Food f) {
    if (!active() || s_.asleep) return Result::NotNow;
    if (s_.misbehaving) return Result::Refused;
    if (s_.hunger >= 3.75f) return Result::Refused;  // full
    if (s_.hunger < 2.0f) addTrust(1);                // fed when it was actually hungry
    switch (f) {
        case Food::Berries:
            s_.hunger = clampf(s_.hunger + 1, 0, 4);
            s_.happiness = clampf(s_.happiness + 0.5f, 0, 4);
            s_.snacks++;
            break;
        case Food::Fish:
            s_.hunger = clampf(s_.hunger + 2, 0, 4);
            s_.meals++;
            break;
        case Food::Roots:
            s_.hunger = clampf(s_.hunger + 2, 0, 4);
            s_.happiness = clampf(s_.happiness - 0.25f, 0, 4);
            s_.meals++;
            break;
    }
    if (f != Food::Berries) {
        if (s_.messes == 0) s_.oldestMessAt = s_.now;
        if (s_.messes < 255) s_.messes++;
    }
    return Result::Ok;
}

Result Sim::clean() {
    if (!active()) return Result::NotNow;
    if (s_.messes == 0) return Result::NotNow;
    s_.messes = 0;
    return Result::Ok;
}

Result Sim::brush() {
    if (!active() || s_.asleep) return Result::NotNow;
    s_.happiness = clampf(s_.happiness + 0.5f, 0, 4);
    if (s_.now - s_.lastBrush >= 4 * 3600) {
        addTrust(0.5f);
        s_.lastBrush = s_.now;
    }
    return Result::Ok;
}

Result Sim::medicine() {
    if (!active()) return Result::NotNow;
    if (!s_.sick) return Result::NotNow;
    s_.sick = false;
    addTrust(2);
    emit(EventType::Recovered);
    return Result::Ok;
}

Result Sim::answerCall() {
    if (!active() || !s_.calling) return Result::NotNow;
    s_.calling = false;
    s_.lastCallEnd = s_.now;
    if (s_.callGenuine) {
        addTrust(3);
        s_.footprints += 1;
    } else {
        addTrust(1);
        addDiscipline(-1);  // giving in to every whim
    }
    return Result::Ok;
}

Result Sim::scold() {
    if (!active() || s_.asleep) return Result::NotNow;
    bool tooSoon = s_.lastScold != 0 && (s_.now - s_.lastScold) < 6 * 3600;
    s_.lastScold = s_.now;
    if (s_.misbehaving) {
        s_.misbehaving = false;
        addDiscipline(10);
        if (tooSoon) addTrust(-2);
        return Result::Ok;
    }
    // Scolding for nothing hurts.
    addTrust(-5);
    s_.careMistakes++;
    return Result::Ok;
}

Result Sim::pet() {
    if (!active() || s_.asleep) return Result::NotNow;
    if (trustLevel() != TrustLevel::Friendly) return Result::Refused;  // it flinches away
    s_.happiness = clampf(s_.happiness + 0.5f, 0, 4);
    if (s_.now - s_.lastPet >= 3600) {
        addTrust(0.5f);
        s_.lastPet = s_.now;
    }
    return Result::Ok;
}

Result Sim::lights(bool on) {
    s_.lightsOn = on;
    return Result::Ok;
}

Result Sim::finishedGame(bool won, bool photoGame) {
    if (!active() || s_.asleep) return Result::NotNow;
    s_.happiness = clampf(s_.happiness + (won ? 1.0f : 0.5f), 0, 4);
    s_.footprints += won ? 5 : 2;
    if (won && photoGame) awardRandomPhoto();
    return Result::Ok;
}

Result Sim::leaveFoodOut() {
    if (s_.stage != Stage::Away) return Result::NotNow;
    int64_t day = (s_.now + s_.tzOffset) / 86400;
    if (day == s_.lastCoaxDay) return Result::NotNow;
    s_.lastCoaxDay = day;
    if (++s_.coaxDays < tune_.coaxDaysNeeded) return Result::Ok;

    s_.stage = s_.stageBeforeAway;
    s_.trust = 10;
    s_.hunger = 2;
    s_.happiness = 2;
    s_.sick = false;
    s_.messes = 0;
    s_.starvingAndDistrustSince = 0;
    s_.lastCallEnd = s_.now;
    emit(EventType::Returned);
    updateStage();
    return Result::Ok;
}

// ---------------------------------------------------------------- queries

TrustLevel Sim::trustLevel() const {
    if (s_.trust >= tune_.trustFriendly) return TrustLevel::Friendly;
    if (s_.trust >= tune_.trustWandering) return TrustLevel::Wandering;
    return TrustLevel::Hiding;
}

int Sim::photoCount() const {
    int n = 0;
    for (uint32_t p = s_.photos; p; p &= p - 1) n++;
    return n;
}

size_t Sim::drainEvents(Event* out, size_t max) {
    size_t n = 0;
    while (evCount_ > 0 && n < max) {
        size_t idx = (evHead_ + kEventCap - evCount_) % kEventCap;
        out[n++] = events_[idx];
        evCount_--;
    }
    return n;
}

// ---------------------------------------------------------------- helpers

void Sim::emit(EventType t, uint8_t a) {
    events_[evHead_] = Event{t, a, s_.now};
    evHead_ = (evHead_ + 1) % kEventCap;
    if (evCount_ < kEventCap) evCount_++;  // oldest is overwritten when full
}

float Sim::rand01() {
    // xorshift64*
    uint64_t x = s_.rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    s_.rng = x;
    return static_cast<float>((x * 0x2545F4914F6CDD1Dull) >> 40) / static_cast<float>(1ull << 24);
}

bool Sim::chancePerHour(float p) { return rand01() < p * kStepSecs / 3600.0f; }

void Sim::addTrust(float d) { s_.trust = clampf(s_.trust + d, 0, 100); }

void Sim::addDiscipline(float d) { s_.discipline = clampf(s_.discipline + d, 0, 100); }

void Sim::awardPhoto(int index) {
    uint32_t bit = 1u << index;
    if (s_.photos & bit) return;
    s_.photos |= bit;
    emit(EventType::PhotoEarned, static_cast<uint8_t>(index));
}

void Sim::awardRandomPhoto() {
    int missing[kPhotoCount];
    int n = 0;
    for (int i = kFirstGamePhoto; i < kPhotoCount; i++)
        if (!(s_.photos & (1u << i))) missing[n++] = i;
    if (n == 0) return;
    int pick = static_cast<int>(rand01() * n);
    awardPhoto(missing[std::min(pick, n - 1)]);
}

// ---------------------------------------------------------------- names

const char* toString(Stage s) {
    switch (s) {
        case Stage::Egg: return "Egg";
        case Stage::Baby: return "Baby";
        case Stage::Child: return "Child";
        case Stage::Teen: return "Teen";
        case Stage::Adult: return "Adult";
        case Stage::Elder: return "Elder";
        case Stage::Away: return "Away";
    }
    return "?";
}

const char* toString(AdultForm f) {
    switch (f) {
        case AdultForm::None: return "None";
        case AdultForm::Guardian: return "Guardian";
        case AdultForm::RidgeRunner: return "Ridge Runner";
        case AdultForm::CouchSquatch: return "Couch Squatch";
        case AdultForm::Hermit: return "Hermit";
        case AdultForm::Legend: return "Legend";
    }
    return "?";
}

const char* toString(EventType e) {
    switch (e) {
        case EventType::Hatched: return "hatched";
        case EventType::StageChanged: return "stage";
        case EventType::BecameAdult: return "adult form";
        case EventType::CallStarted: return "call";
        case EventType::CallIgnored: return "call ignored";
        case EventType::GotSick: return "got sick";
        case EventType::Recovered: return "recovered";
        case EventType::Misbehaving: return "misbehaving";
        case EventType::RanAway: return "ran away";
        case EventType::Returned: return "came back";
        case EventType::PhotoEarned: return "photo";
        case EventType::LaidEgg: return "laid an egg";
        case EventType::WentToSleep: return "asleep";
        case EventType::WokeUp: return "awake";
    }
    return "?";
}

}  // namespace squatch
