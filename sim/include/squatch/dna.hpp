// DNA: every Squatch carries a genome of 16 one-byte genes, rolled when its egg is laid.
// The genes decide its looks (fur, shagginess, build, markings, rare variants) and a
// little of its temperament, so no two pets are quite alike. When an Elder lays the
// next egg, each gene comes from the parent or is rolled fresh, half and half.
//
// Pure data and arithmetic, like the rest of sim/: colours are left to the scene,
// which turns hue and shade into pixels.
#pragma once

#include <cstddef>
#include <cstdint>

namespace squatch {

enum class Rarity : uint8_t { Common, Uncommon, Rare, Epic, Mythic };

// Special coats. Rare pets get Swamp or Snowcoat, Epic ones Albino or Snowcoat, and a
// Mythic is always Golden.
enum class Variant : uint8_t { None, Swamp, Snowcoat, Albino, Golden };

// Gene positions. 11..15 are unused for now: they still make each genome unique and
// are inherited like the rest, so later features can give them meaning.
enum Gene : uint8_t {
    kGeneHue = 0,
    kGeneShade,
    kGeneShag,
    kGeneBuild,
    kGeneMarkings,
    kGeneMarkShade,
    kGeneShy,
    kGenePlayful,
    kGeneAppetite,
    kGeneRarity,
    kGeneVariant,
    kGeneCount = 16,
};

struct Genome {
    uint8_t g[kGeneCount] = {};
};

// What the genome means.
struct Traits {
    float hue;        // fur hue in degrees (browns, or the variant's own colour)
    float shade;      // 0 dark .. 1 light
    uint8_t shag;     // 0 neat .. 3 wild
    float build;      // width, 0.92 slim .. 1.10 stocky
    uint8_t markings; // 0 none, 1 pale chest blaze, 2 dark eye mask, 3 speckles
    float markShade;  // how strong the markings are, 0..1
    float shy;        // 0..1: shy pets take longer to trust you
    float playful;    // 0..1: playful pets get bored (lose happiness) faster
    float appetite;   // 0..1: hungrier pets need feeding more often
    Rarity rarity;
    Variant variant;
};

Traits decode(const Genome& g);

// Roll a fresh genome from a xorshift64 state (advanced in place).
Genome randomGenome(uint64_t& rng);

// The next generation: each gene from the parent or fresh, 50/50. A parent that grew
// into a Guardian or a Legend (bestParent) rolls a fresh rarity gene twice and keeps
// the better one, so good care slowly breeds rarer pets.
Genome inherit(const Genome& parent, uint64_t& rng, bool bestParent);

// How many genes two genomes share, 0..16.
int sharedGenes(const Genome& a, const Genome& b);

// A short code for the stats page, like "3FA2-1C9B". Writes up to cap bytes.
void dnaCode(const Genome& g, char* out, size_t cap);

const char* toString(Rarity r);
const char* toString(Variant v);

}  // namespace squatch
