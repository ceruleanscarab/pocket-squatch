// Plain tests with no framework, so they build anywhere (including later on-device).
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "players.hpp"
#include "squatch/save.hpp"
#include "squatch/sim.hpp"

using namespace squatch;

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        gChecks++;                                                         \
        if (!(cond)) {                                                     \
            gFailures++;                                                   \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)

static constexpr int64_t kStart = 1798761600;  // 2027-01-01 00:00 UTC
static constexpr int32_t kTz = -7 * 3600;       // Mountain time
static constexpr int64_t kDay = 86400;

static Sim fresh(uint64_t seed = 42) {
    Sim sim;
    sim.newGame(kStart, kTz, seed);
    return sim;
}

static bool sameState(const State& a, const State& b) {
    // Compare field by field (padding bytes may differ).
    return a.stage == b.stage && a.adult == b.adult && a.now == b.now && a.hunger == b.hunger &&
           a.happiness == b.happiness && a.trust == b.trust && a.discipline == b.discipline &&
           a.messes == b.messes && a.sick == b.sick && a.calling == b.calling &&
           a.misbehaving == b.misbehaving && a.rng == b.rng && a.photos == b.photos &&
           a.careMistakes == b.careMistakes && a.pendingSecs == b.pendingSecs;
}

static void testEggHatchesAfterADay() {
    std::printf("egg hatches after one day\n");
    Sim sim = fresh();
    sim.tick(kDay - 60);
    CHECK(sim.state().stage == Stage::Egg);
    sim.tick(60);
    CHECK(sim.state().stage == Stage::Baby);
    CHECK(sim.state().photos & 1u);  // hatching photo
}

static void testCatchUpMatchesRealTime() {
    std::printf("one big catch-up tick equals many small ticks\n");
    const int64_t span = 30 * 3600 + 37;
    Sim a = fresh(7), b = fresh(7), c = fresh(7);
    a.tick(span);
    for (int i = 0; i < 30 * 60; i++) b.tick(60);
    b.tick(37);
    CHECK(sameState(a.state(), b.state()));

    // Odd-sized ticks carry their remainder instead of losing it.
    for (int64_t done = 0; done < span; done += 45) c.tick(std::min<int64_t>(45, span - done));
    CHECK(sameState(a.state(), c.state()));
}

static void testSaveRoundTrip() {
    std::printf("save and load round-trip, corruption is caught\n");
    Sim sim = fresh();
    sim.setName("Harry");
    sim.tick(2 * kDay);
    std::vector<uint8_t> buf(kSaveBytes);
    CHECK(save(sim.state(), buf.data(), buf.size()) == kSaveBytes);

    State loaded{};
    CHECK(load(buf.data(), buf.size(), loaded) == LoadResult::Ok);
    CHECK(sameState(sim.state(), loaded));
    CHECK(std::strcmp(loaded.name, "Harry") == 0);

    buf[sizeof(SaveHeader) + 5] ^= 0xFF;
    State untouched{};
    CHECK(load(buf.data(), buf.size(), untouched) == LoadResult::BadChecksum);
    CHECK(load(buf.data(), 3, untouched) == LoadResult::TooShort);
}

static void testRunsAwayAndComesBack() {
    std::printf("an untreated sickness makes it run away; food on three days brings it back\n");
    Sim sim = fresh();
    sim.tick(kDay + 3600);
    CHECK(sim.state().stage == Stage::Baby);
    sim.mutableState().sick = true;
    sim.mutableState().sickSince = sim.state().now;
    sim.tick(25 * 3600);
    CHECK(sim.state().stage == Stage::Away);

    CHECK(sim.leaveFoodOut() == Result::Ok);
    CHECK(sim.leaveFoodOut() == Result::NotNow);  // once per day
    sim.tick(kDay);
    CHECK(sim.leaveFoodOut() == Result::Ok);
    CHECK(sim.state().stage == Stage::Away);
    sim.tick(kDay);
    CHECK(sim.leaveFoodOut() == Result::Ok);
    CHECK(sim.state().stage != Stage::Away);
    CHECK(!sim.state().sick);
}

static void keepComfortable(Sim& sim) {
    State& s = sim.mutableState();
    s.hunger = 4;
    s.happiness = 4;
    s.sick = false;
    s.messes = 0;
}

static void testElderLaysEgg() {
    std::printf("an Elder lays the next generation's egg and the gallery carries over\n");
    Sim sim = fresh();
    sim.mutableState().footprints = 99;
    for (int h = 0; h < 26 * 24; h++) {
        keepComfortable(sim);
        sim.tick(3600);
    }
    CHECK(sim.state().generation == 2);
    CHECK(sim.state().stage == Stage::Egg || sim.state().stage == Stage::Baby);
    CHECK(sim.state().footprints >= 99);
    CHECK(sim.state().photos & 1u);
}

static void testFullGalleryMakesALegend() {
    std::printf("completing the gallery turns an adult into a Legend\n");
    Sim sim = fresh();
    for (int h = 0; h < 10 * 24 + 1; h++) {
        keepComfortable(sim);
        if (h == 10 * 24 - 1) {  // raised well right up to adulthood
            sim.mutableState().trust = 90;
            sim.mutableState().discipline = 60;
            sim.mutableState().misbehaving = false;
        }
        sim.tick(3600);
    }
    CHECK(sim.state().stage == Stage::Adult);
    CHECK(sim.state().adult == AdultForm::Guardian);
    sim.mutableState().photos = (1u << Sim::kPhotoCount) - 1;
    sim.tick(60);
    CHECK(sim.state().adult == AdultForm::Legend);
}

static void testScoldingForNothingHurts() {
    std::printf("scolding when it did nothing wrong costs Trust\n");
    Sim sim = fresh();
    sim.tick(kDay + 18 * 3600);  // a Baby, 11:00 local
    sim.mutableState().trust = 50;
    sim.mutableState().misbehaving = false;
    CHECK(sim.scold() == Result::Ok);
    CHECK(sim.state().trust < 50);
}

static void testEachPlayerGetsTheirForm() {
    std::printf("each scripted player mostly raises the adult form they steer toward\n");
    struct Expect {
        const char* player;
        AdultForm form;
    } expects[] = {
        {"devoted", AdultForm::Guardian},     {"pushover", AdultForm::RidgeRunner},
        {"snacker", AdultForm::CouchSquatch}, {"busy", AdultForm::Hermit},
        {"photographer", AdultForm::Legend},
    };
    for (const auto& e : expects) {
        const Player* p = findPlayer(e.player);
        CHECK(p != nullptr);
        if (!p) continue;
        int hits = 0;
        for (int i = 0; i < 20; i++) hits += raise(*p, 500 + i).form == e.form;
        std::printf("    %-13s %-13s %2d/20\n", e.player, toString(e.form), hits);
        CHECK(hits >= 16);
    }
}

int main() {
    testEggHatchesAfterADay();
    testCatchUpMatchesRealTime();
    testSaveRoundTrip();
    testRunsAwayAndComesBack();
    testElderLaysEgg();
    testFullGalleryMakesALegend();
    testScoldingForNothingHurts();
    testEachPlayerGetsTheirForm();
    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
