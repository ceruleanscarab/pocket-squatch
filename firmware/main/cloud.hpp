// Cloud backups (cloud/worker.js): once a night, about 2 am, and right after the Wi-Fi is set
// up, the board puts its pet's save on a small Cloudflare Worker under the board's backup
// code. Typed into the Wi-Fi setup page on another (or a reset) board, the code brings the
// pet back. Off unless menuconfig's "Cloud backups address" is set.
#pragma once

#include <cstddef>
#include <cstdint>

#include "squatch/sim.hpp"

namespace cloud {

bool enabled();
// This board's code, as shown: XXXX-XXXX-XXXX-XXXX (made, and kept, the first time it's asked for).
void code(char* out, size_t n);
// A typed code, tidied: 16 characters of Crockford base32 (O read as 0, I and L as 1, dashes,
// spaces and case ignored). False if it isn't one.
bool tidy(const char* typed, char out[17]);

// Back up now, in the background (takes the radio). Does nothing if one is already going.
void backupNow(const squatch::State& s);
// Every minute or so from the game loop: backs up once a night, about 2 am.
void tick(const squatch::State& s, bool batteryOk);

// For wifisetup, while it already has the radio and is joined to the network.
bool fetchJoined(const char* code16, squatch::State& out);
// A pet fetched by the setup page, for the game loop to put in place (and this board then
// takes that code as its own).
void offerRestored(const squatch::State& s, const char* code16);
bool takeRestored(squatch::State& out);

}  // namespace cloud
