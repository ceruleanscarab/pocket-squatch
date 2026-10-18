// The pet's save, in the board's flash (NVS), in the same format as the PC's save file.
#pragma once

#include "squatch/scene.hpp"
#include "squatch/sim.hpp"

namespace storage {

void init();  // NVS; call once before anything else touches flash or Wi-Fi
bool loadPet(squatch::State& out);
void savePet(const squatch::State& s);

// Brightness, screen timeout and volume, kept apart from the pet so a new egg doesn't reset them.
bool loadSettings(squatch::Scene::Settings& out);
void saveSettings(const squatch::Scene::Settings& s);

}  // namespace storage
