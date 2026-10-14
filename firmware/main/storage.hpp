// The pet's save, in the board's flash (NVS), in the same format as the PC's save file.
#pragma once

#include "squatch/sim.hpp"

namespace storage {

void init();  // NVS; call once before anything else touches flash or Wi-Fi
bool loadPet(squatch::State& out);
void savePet(const squatch::State& s);

}  // namespace storage
