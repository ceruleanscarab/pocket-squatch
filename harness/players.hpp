// Scripted players: each one checks in on its own schedule and cares for the pet
// in its own style. The harness and the tests use them to check that every adult
// form can actually be reached, and roughly how often.
#pragma once

#include <cstdint>
#include <cstring>

#include "squatch/sim.hpp"

namespace squatch {

struct Player {
    const char* name;
    const char* blurb;
    int checkEveryMins;      // how often they look at it
    int dayStartHour;        // they only check between these local hours
    int dayEndHour;          //   (0 and 24 = any time)
    bool answersFakeCalls;   // gives in to every "I want company" call
    bool scolds;             // scolds misbehaviour
    bool snacker;            // feeds berries instead of meals
    bool tidy;               // cleans, treats sickness, turns the light off
    bool playful;            // plays games and pets it
    int photoGamesPerCheck;  // Blurry Photo rounds per check-in, from Teen on
};

// The cast. Each is meant to steer toward one adult form.
inline const Player kPlayers[] = {
    {"devoted", "hourly, fair, healthy food", 60, 7, 22, false, true, false, true, true, 0},
    {"pushover", "hourly, never scolds, answers every whim", 60, 7, 22, true, false, false, true, true, 0},
    {"snacker", "hourly, but mostly berries", 60, 7, 22, false, true, true, true, true, 0},
    {"busy", "three times a day, basics only", 8 * 60, 0, 24, false, false, false, false, false, 0},
    {"photographer", "devoted, plus Blurry Photo every check-in", 60, 7, 22, false, true, false, true, true, 2},
};

inline const Player* findPlayer(const char* name) {
    for (const auto& p : kPlayers)
        if (std::strcmp(p.name, name) == 0) return &p;
    return nullptr;
}

// One check-in. Returns nothing; the sim records what happened as events.
inline void checkIn(Sim& sim, const Player& p, uint32_t& counter) {
    const State& s = sim.state();
    counter++;

    if (s.stage == Stage::Away) {
        sim.leaveFoodOut();
        return;
    }
    if (s.stage == Stage::Egg) return;

    if (s.asleep) {
        if (p.tidy && s.lightsOn) sim.lights(false);
        return;
    }

    if (s.calling && (s.callGenuine || p.answersFakeCalls)) sim.answerCall();
    if (s.misbehaving && p.scolds) sim.scold();
    if (s.sick) sim.medicine();
    if (s.messes > 0 && (p.tidy || counter % 3 == 0)) sim.clean();

    for (int i = 0; i < 3 && sim.state().hunger < 3.0f; i++) {
        Food f = p.snacker ? Food::Berries : (counter % 2 ? Food::Fish : Food::Roots);
        if (sim.feed(f) != Result::Ok) break;
    }

    if (p.playful) {
        if (sim.state().happiness < 3.0f) sim.finishedGame(true, false);
        sim.pet();
        sim.brush();
    }
    bool photoAge = s.stage == Stage::Teen || s.stage == Stage::Adult || s.stage == Stage::Elder;
    for (int i = 0; photoAge && i < p.photoGamesPerCheck; i++) sim.finishedGame(true, true);
}

inline bool isCheckTime(const Sim& sim, const Player& p, int64_t minuteOfRun) {
    if (minuteOfRun % p.checkEveryMins != 0) return false;
    int h = sim.localHour();
    if (p.dayStartHour == 0 && p.dayEndHour == 24) return true;
    return h >= p.dayStartHour && h < p.dayEndHour;
}

struct RunResult {
    AdultForm form = AdultForm::None;
    float trust = 0, discipline = 0;
    int photos = 0, mistakes = 0, timesRanAway = 0, timesSick = 0;
};

// Raise one pet from egg to its adult form (day 10) plus a day, under one player.
inline RunResult raise(const Player& p, uint64_t seed, int days = 11,
                       void (*onDay)(const Sim&, int) = nullptr,
                       void (*onEvent)(const Event&) = nullptr) {
    Sim sim;
    sim.newGame(1798761600 /* 2027-01-01 00:00 UTC */, -7 * 3600, seed);
    RunResult r;
    uint32_t counter = 0;
    Event evs[Sim::kEventCap];
    for (int64_t minute = 0; minute < int64_t(days) * 1440; minute++) {
        if (isCheckTime(sim, p, minute)) checkIn(sim, p, counter);
        sim.tick(60);
        size_t n = sim.drainEvents(evs, Sim::kEventCap);
        for (size_t i = 0; i < n; i++) {
            if (evs[i].type == EventType::RanAway) r.timesRanAway++;
            if (evs[i].type == EventType::GotSick) r.timesSick++;
            if (onEvent) onEvent(evs[i]);
        }
        if (onDay && (minute + 1) % 1440 == 0) onDay(sim, static_cast<int>((minute + 1) / 1440));
    }
    const State& s = sim.state();
    r.form = s.adult;
    r.trust = s.trust;
    r.discipline = s.discipline;
    r.photos = sim.photoCount();
    r.mistakes = s.careMistakes;
    return r;
}

}  // namespace squatch
