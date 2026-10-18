// Plain tests with no framework, so they build anywhere (including later on-device).
#include <algorithm>
#include <cstddef>
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

static void testEggHatches() {
    std::printf("egg hatches when the egg stage ends (an hour)\n");
    Sim sim = fresh();
    sim.tick(sim.tuning().eggEnd - 60);
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

// ---------------------------------------------------------------- DNA

static void testDnaDecodesAndVaries() {
    std::printf("every egg gets its own DNA, decoded the same way every time\n");
    Sim a = fresh(1), b = fresh(2);
    CHECK(sharedGenes(a.state().genome, b.state().genome) < 6);
    Traits t1 = decode(a.state().genome), t2 = decode(a.state().genome);
    CHECK(t1.hue == t2.hue && t1.rarity == t2.rarity && t1.shag == t2.shag);
    CHECK(t1.build >= 0.92f && t1.build <= 1.10f + 1e-4f);
    char code[16];
    dnaCode(a.state().genome, code, sizeof code);
    CHECK(std::strlen(code) == 9 && code[4] == '-');

    std::printf("rarity tiers come out at roughly the planned odds, each with its coat\n");
    int counts[5] = {};
    uint64_t rng = 12345;
    for (int i = 0; i < 20000; i++) {
        Traits t = decode(randomGenome(rng));
        counts[static_cast<int>(t.rarity)]++;
        if (t.rarity == Rarity::Mythic) CHECK(t.variant == Variant::Golden);
        if (t.rarity == Rarity::Rare) CHECK(t.variant == Variant::Swamp || t.variant == Variant::Snowcoat);
        if (t.rarity == Rarity::Epic) CHECK(t.variant == Variant::Albino || t.variant == Variant::Snowcoat);
        if (t.rarity <= Rarity::Uncommon) CHECK(t.variant == Variant::None);
    }
    std::printf("    common %d  uncommon %d  rare %d  epic %d  mythic %d\n", counts[0], counts[1], counts[2],
                counts[3], counts[4]);
    CHECK(counts[0] > 11000 && counts[0] < 13000);
    CHECK(counts[2] > 1500 && counts[2] < 2500);
    CHECK(counts[4] > 50 && counts[4] < 450);
}

static void testChildInheritsHalf() {
    std::printf("the next generation inherits about half its genes\n");
    uint64_t rng = 99;
    Genome parent = randomGenome(rng);
    int total = 0;
    for (int i = 0; i < 200; i++) total += sharedGenes(parent, inherit(parent, rng, false));
    float avg = total / 200.0f;
    std::printf("    %.1f of 16 genes shared on average\n", avg);
    CHECK(avg > 7 && avg < 9.5f);

    std::printf("a Guardian or Legend parent breeds rarer eggs\n");
    int rareNormal = 0, rareBest = 0;
    Genome common = parent;
    common.g[kGeneRarity] = 0;
    for (int i = 0; i < 4000; i++) {
        rareNormal += decode(inherit(common, rng, false)).rarity >= Rarity::Rare;
        rareBest += decode(inherit(common, rng, true)).rarity >= Rarity::Rare;
    }
    CHECK(rareBest > rareNormal * 3 / 2);
}

static void testEggCarriesFamilyDna() {
    std::printf("an Elder's egg shares DNA with it, and the map carries over\n");
    Sim sim = fresh(5);
    Genome parent = sim.state().genome;
    Sighting seen[] = {{trailHash("Home"), -45}};
    sim.scoutTrails(seen, 1);
    for (int h = 0; h < 26 * 24; h++) {
        keepComfortable(sim);
        sim.tick(3600);
    }
    CHECK(sim.state().generation == 2);
    int shared = sharedGenes(parent, sim.state().genome);
    CHECK(shared >= 3 && shared <= 14);
    CHECK(sim.state().trailCount == 1);
}

static void testTemperamentMatters() {
    std::printf("a hungry-natured pet gets hungry faster than a light eater\n");
    Sim big = fresh(3), small = fresh(3);
    big.mutableState().genome.g[kGeneAppetite] = 255;
    small.mutableState().genome.g[kGeneAppetite] = 0;
    big.tick(kDay + 3600);
    small.tick(kDay + 3600);
    keepComfortable(big);
    keepComfortable(small);
    big.tick(3 * 3600);
    small.tick(3 * 3600);
    CHECK(big.state().hunger < small.state().hunger);
}

static void testOldSaveMigrates() {
    std::printf("a version 1 save (from before DNA) still loads, and gets DNA\n");
    Sim sim = fresh(8);
    sim.setName("Bigs");
    sim.tick(2 * kDay);
    const size_t v1Size = offsetof(State, genome);
    std::vector<uint8_t> buf(sizeof(SaveHeader) + v1Size);
    std::memcpy(buf.data() + sizeof(SaveHeader), &sim.state(), v1Size);
    SaveHeader h{kSaveMagic, 1, 0, static_cast<uint32_t>(v1Size), crc32(buf.data() + sizeof(SaveHeader), v1Size)};
    std::memcpy(buf.data(), &h, sizeof h);
    State loaded{};
    CHECK(load(buf.data(), buf.size(), loaded) == LoadResult::Ok);
    CHECK(std::strcmp(loaded.name, "Bigs") == 0);
    CHECK(loaded.stage == sim.state().stage && loaded.trust == sim.state().trust);
    Genome zero{};
    CHECK(sharedGenes(loaded.genome, zero) < 8);
    CHECK(loaded.trailCount == 0 && !loaded.foraging);
}

// ---------------------------------------------------------------- trails

static void testTrailNames() {
    std::printf("a network is always the same place, and never stored by name\n");
    uint32_t a = trailHash("Caffeine Hut"), b = trailHash("Caffeine Hut"), c = trailHash("Caffeine Hut 5G");
    CHECK(a == b && a != c && a != 0);
    char n1[32], n2[32];
    landmarkName(a, n1, sizeof n1);
    landmarkName(b, n2, sizeof n2);
    CHECK(std::strcmp(n1, n2) == 0);
    CHECK(std::strchr(n1, ' ') != nullptr);
    CHECK(trailDistance(-40) == TrailDistance::Near);
    CHECK(trailDistance(-70) == TrailDistance::Mid);
    CHECK(trailDistance(-90) == TrailDistance::Far);
    int kinds[kLandmarkKinds] = {};
    char ssid[16];
    for (int i = 0; i < 800; i++) {
        std::snprintf(ssid, sizeof ssid, "net%d", i);
        kinds[static_cast<int>(landmarkKind(trailHash(ssid)))]++;
    }
    for (int k : kinds) CHECK(k > 60);  // every kind turns up
}

static void testScoutKeepsStrongest() {
    std::printf("a scan keeps the five strongest networks, once each, nearest first\n");
    Sim sim = fresh();
    Sighting seen[] = {{11, -80}, {12, -50}, {13, -90}, {14, -60}, {12, -40}, {15, -70}, {16, -65}, {0, -30}};
    sim.scoutTrails(seen, sizeof seen / sizeof seen[0]);
    const State& s = sim.state();
    CHECK(s.trailCount == 5);
    CHECK(s.trails[0].hash == 12 && s.trails[0].rssi == -40);
    for (int i = 1; i < s.trailCount; i++) CHECK(s.trails[i - 1].rssi >= s.trails[i].rssi);
    for (int i = 0; i < s.trailCount; i++) CHECK(s.trails[i].hash != 13 && s.trails[i].hash != 0);
    Event ev[8];
    size_t n = sim.drainEvents(ev, 8);
    CHECK(n == 1 && ev[0].type == EventType::TrailsScouted && ev[0].a == 5);

    sim.scoutTrails(nullptr, 0);  // somewhere with no Wi-Fi at all
    CHECK(sim.state().trailCount == 0);
}

// A Child at 11:00 local that trusts you, fed and well.
static Sim readyToExplore() {
    Sim sim = fresh(21);
    for (int h = 0; h < 3 * 24 + 18; h++) {  // looked after, so it doesn't wander off
        keepComfortable(sim);
        sim.tick(3600);
    }
    keepComfortable(sim);
    State& s = sim.mutableState();
    s.trust = 50;
    s.misbehaving = s.calling = false;
    Sighting seen[] = {{trailHash("Ranger Station"), -45}, {trailHash("Far Away Diner"), -88}};
    sim.scoutTrails(seen, 2);
    Event ev[16];
    while (sim.drainEvents(ev, 16)) {
    }
    return sim;
}

static void testForageTrip() {
    std::printf("sent down a near trail, it is gone for half an hour and comes back with footprints\n");
    Sim sim = readyToExplore();
    CHECK(sim.state().stage == Stage::Child);
    uint32_t before = sim.state().footprints;
    CHECK(sim.sendForaging(0) == Result::Ok);
    CHECK(sim.state().foraging);
    CHECK(sim.feed(Food::Fish) == Result::NotNow);  // it isn't here
    CHECK(sim.sendForaging(1) == Result::NotNow);
    sim.tick(29 * 60);
    CHECK(sim.state().foraging);
    sim.tick(2 * 60);
    CHECK(!sim.state().foraging);
    uint32_t first = sim.state().footprints - before;
    CHECK(first >= sim.tuning().forageFootprints[0] + sim.tuning().firstVisitFootprints);
    CHECK(sim.trailVisited(sim.state().trails[0].hash));
    CHECK(sim.state().trailsVisited == 1);
    bool back = false;
    Event ev[16];
    for (size_t n; (n = sim.drainEvents(ev, 16)) > 0;)
        for (size_t i = 0; i < n; i++) back |= ev[i].type == EventType::BackFromForaging;
    CHECK(back);

    std::printf("a second visit to the same place pays less\n");
    keepComfortable(sim);
    before = sim.state().footprints;
    CHECK(sim.sendForaging(0) == Result::Ok);
    sim.tick(31 * 60);
    CHECK(sim.state().footprints - before < first);

    std::printf("a far trail takes longer\n");
    keepComfortable(sim);
    CHECK(sim.sendForaging(1) == Result::Ok);
    sim.tick(90 * 60);
    CHECK(sim.state().foraging);
    sim.tick(31 * 60);
    CHECK(!sim.state().foraging);
}

static void testForageRefusals() {
    std::printf("a shy, sick, hungry or baby Squatch won't go\n");
    Sim shy = readyToExplore();
    shy.mutableState().trust = 10;
    CHECK(shy.sendForaging(0) == Result::Refused);
    Sim sick = readyToExplore();
    sick.mutableState().sick = true;
    CHECK(sick.sendForaging(0) == Result::Refused);
    Sim hungry = readyToExplore();
    hungry.mutableState().hunger = 0.5f;
    CHECK(hungry.sendForaging(0) == Result::Refused);
    Sim baby = fresh();
    baby.tick(kDay + 18 * 3600);
    baby.mutableState().trust = 80;
    Sighting seen[] = {{trailHash("x"), -40}};
    baby.scoutTrails(seen, 1);
    CHECK(baby.sendForaging(0) == Result::Refused);
}

int main() {
    testEggHatches();
    testCatchUpMatchesRealTime();
    testSaveRoundTrip();
    testRunsAwayAndComesBack();
    testElderLaysEgg();
    testFullGalleryMakesALegend();
    testScoldingForNothingHurts();
    testEachPlayerGetsTheirForm();
    testDnaDecodesAndVaries();
    testChildInheritsHalf();
    testEggCarriesFamilyDna();
    testTemperamentMatters();
    testOldSaveMigrates();
    testTrailNames();
    testScoutKeepsStrongest();
    testForageTrip();
    testForageRefusals();
    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
