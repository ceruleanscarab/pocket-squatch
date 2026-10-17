#include "squatch/dna.hpp"

#include <cstdio>

namespace squatch {

namespace {

uint64_t next(uint64_t& x) {  // xorshift64*, as in the sim
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return x * 0x2545F4914F6CDD1Dull;
}

uint8_t nextByte(uint64_t& rng) { return static_cast<uint8_t>(next(rng) >> 56); }

float unit(uint8_t v) { return static_cast<float>(v) / 255.0f; }

// Rarity gene thresholds: about 60% Common, 25% Uncommon, 10% Rare, 4% Epic, 1% Mythic.
Rarity rarityOf(uint8_t v) {
    if (v < 154) return Rarity::Common;
    if (v < 218) return Rarity::Uncommon;
    if (v < 243) return Rarity::Rare;
    if (v < 253) return Rarity::Epic;
    return Rarity::Mythic;
}

}  // namespace

Traits decode(const Genome& gn) {
    const uint8_t* g = gn.g;
    Traits t{};
    t.rarity = rarityOf(g[kGeneRarity]);
    bool alt = (g[kGeneVariant] & 1) != 0;
    switch (t.rarity) {
        case Rarity::Rare: t.variant = alt ? Variant::Snowcoat : Variant::Swamp; break;
        case Rarity::Epic: t.variant = alt ? Variant::Snowcoat : Variant::Albino; break;
        case Rarity::Mythic: t.variant = Variant::Golden; break;
        default: t.variant = Variant::None; break;
    }

    // Ordinary coats are browns; Uncommon ones reach further, to charcoal and blond.
    t.hue = 16 + unit(g[kGeneHue]) * 22;  // 16..38 degrees
    float shade = unit(g[kGeneShade]);
    t.shade = (t.rarity == Rarity::Common) ? 0.25f + shade * 0.5f : shade;
    if (t.variant == Variant::Swamp) t.hue = 6 + unit(g[kGeneHue]) * 8;  // rusty red

    t.shag = static_cast<uint8_t>(g[kGeneShag] >> 6);  // 0..3
    t.build = 0.92f + unit(g[kGeneBuild]) * 0.18f;
    t.markings = static_cast<uint8_t>(g[kGeneMarkings] >> 6);
    t.markShade = 0.4f + unit(g[kGeneMarkShade]) * 0.6f;
    t.shy = unit(g[kGeneShy]);
    t.playful = unit(g[kGenePlayful]);
    t.appetite = unit(g[kGeneAppetite]);
    return t;
}

Genome randomGenome(uint64_t& rng) {
    Genome g;
    for (uint8_t& b : g.g) b = nextByte(rng);
    return g;
}

Genome inherit(const Genome& parent, uint64_t& rng, bool bestParent) {
    Genome child;
    for (int i = 0; i < kGeneCount; i++) {
        bool fromParent = (next(rng) >> 63) != 0;
        uint8_t fresh = nextByte(rng);
        if (i == kGeneRarity && bestParent) {
            uint8_t second = nextByte(rng);
            if (second > fresh) fresh = second;
        }
        child.g[i] = fromParent ? parent.g[i] : fresh;
    }
    return child;
}

int sharedGenes(const Genome& a, const Genome& b) {
    int n = 0;
    for (int i = 0; i < kGeneCount; i++) n += a.g[i] == b.g[i];
    return n;
}

void dnaCode(const Genome& g, char* out, size_t cap) {
    uint32_t h = 2166136261u;  // FNV-1a over all the genes
    for (uint8_t b : g.g) h = (h ^ b) * 16777619u;
    std::snprintf(out, cap, "%04X-%04X", static_cast<unsigned>(h >> 16), static_cast<unsigned>(h & 0xFFFF));
}

const char* toString(Rarity r) {
    switch (r) {
        case Rarity::Common: return "Common";
        case Rarity::Uncommon: return "Uncommon";
        case Rarity::Rare: return "Rare";
        case Rarity::Epic: return "Epic";
        case Rarity::Mythic: return "Mythic";
    }
    return "?";
}

const char* toString(Variant v) {
    switch (v) {
        case Variant::None: return "None";
        case Variant::Swamp: return "Swamp";
        case Variant::Snowcoat: return "Snowcoat";
        case Variant::Albino: return "Albino";
        case Variant::Golden: return "Golden";
    }
    return "?";
}

}  // namespace squatch
