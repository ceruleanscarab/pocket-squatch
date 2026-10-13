// squatch-harness: fast-forward Pocket Squatch under scripted players.
//
//   squatch-harness                     every player, 20 pets each: which adult forms come out
//   squatch-harness --runs 100          same, more pets
//   squatch-harness --player busy       one pet, day by day
//   squatch-harness --player busy --events --seed 7 --days 26
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "players.hpp"

using namespace squatch;

namespace {

void printDay(const Sim& sim, int day) {
    const State& s = sim.state();
    std::printf("day %2d  %-5s %-13s  food %.1f  joy %.1f  trust %5.1f  discipline %5.1f  photos %2d  mistakes %u%s\n",
                day, toString(s.stage), toString(s.adult), s.hunger, s.happiness, s.trust, s.discipline,
                sim.photoCount(), s.careMistakes, s.sick ? "  (sick)" : "");
}

bool gShowEvents = false;

void printEvent(const Event& e) {
    if (!gShowEvents) return;
    if (e.type == EventType::WentToSleep || e.type == EventType::WokeUp) return;
    int64_t local = e.at - 7 * 3600;
    int day = static_cast<int>((local - (1798761600 - 7 * 3600)) / 86400);
    int hh = static_cast<int>(((local % 86400) + 86400) % 86400 / 3600);
    int mm = static_cast<int>(((local % 3600) + 3600) % 3600 / 60);
    std::printf("        d%-2d %02d:%02d  %s", day, hh, mm, toString(e.type));
    if (e.type == EventType::StageChanged) std::printf(" -> %s", toString(static_cast<Stage>(e.a)));
    if (e.type == EventType::BecameAdult) std::printf(" -> %s", toString(static_cast<AdultForm>(e.a)));
    if (e.type == EventType::CallStarted) std::printf(e.a ? " (needs something)" : " (wants company)");
    if (e.type == EventType::PhotoEarned) std::printf(" #%d", e.a);
    std::printf("\n");
}

void summary(int runs) {
    std::printf("%d pets per player, raised to day 11\n\n", runs);
    std::printf("%-13s %-42s %-9s %-9s %-9s %-9s %-9s  %6s %6s %5s %5s\n", "player", "style", "Guardian", "Runner",
                "Couch", "Hermit", "Legend", "trust", "disc", "sick", "fled");
    for (const auto& p : kPlayers) {
        int forms[6] = {};
        double trust = 0, disc = 0, sick = 0, fled = 0;
        for (int i = 0; i < runs; i++) {
            RunResult r = raise(p, 1000 + i);
            forms[static_cast<int>(r.form)]++;
            trust += r.trust;
            disc += r.discipline;
            sick += r.timesSick;
            fled += r.timesRanAway;
        }
        std::printf("%-13s %-42s %-9d %-9d %-9d %-9d %-9d  %6.1f %6.1f %5.1f %5.1f\n", p.name, p.blurb,
                    forms[int(AdultForm::Guardian)], forms[int(AdultForm::RidgeRunner)],
                    forms[int(AdultForm::CouchSquatch)], forms[int(AdultForm::Hermit)],
                    forms[int(AdultForm::Legend)], trust / runs, disc / runs, sick / runs, fled / runs);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const char* player = nullptr;
    uint64_t seed = 1;
    int days = 11, runs = 20;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--player") && i + 1 < argc) player = argv[++i];
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--days") && i + 1 < argc) days = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--runs") && i + 1 < argc) runs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--events")) gShowEvents = true;
        else {
            std::fprintf(stderr, "usage: %s [--runs N] | --player NAME [--seed N] [--days N] [--events]\nplayers:", argv[0]);
            for (const auto& p : kPlayers) std::fprintf(stderr, " %s", p.name);
            std::fprintf(stderr, "\n");
            return 2;
        }
    }

    if (!player) {
        summary(runs);
        return 0;
    }
    const Player* p = findPlayer(player);
    if (!p) {
        std::fprintf(stderr, "unknown player '%s'\n", player);
        return 2;
    }
    std::printf("%s: %s (seed %llu)\n", p->name, p->blurb, static_cast<unsigned long long>(seed));
    RunResult r = raise(*p, seed, days, printDay, printEvent);
    std::printf("\nadult form: %s   trust %.1f   discipline %.1f   photos %d   sick %d   ran away %d\n",
                toString(r.form), r.trust, r.discipline, r.photos, r.timesSick, r.timesRanAway);
    return 0;
}
