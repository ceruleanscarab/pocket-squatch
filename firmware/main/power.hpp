// The battery, through the board's AXP2101 power chip (I2C address 0x34, on the same bus
// as the clock). Both boards have one.
//
// It reads the charge (the chip's own fuel gauge, in percent), whether a battery is
// connected, and whether it's charging. At start-up it also sets a gentle charge current
// for the board's battery: the chip's default is far too strong for a small cell.
#pragma once

#include "driver/i2c_master.h"

namespace power {

struct Battery {
    bool present;   // a battery is connected
    int percent;    // 0..100, or -1 if unknown
    bool charging;
    bool usb;       // USB power is plugged in
};

// chargeMa: constant charge current; trickleMa: precharge and end-of-charge current.
// Returns false if no AXP2101 answers.
bool init(i2c_master_bus_handle_t bus, int chargeMa, int trickleMa);

// One read of the chip (a few I2C transfers). False if it didn't answer.
bool read(Battery& out);

}  // namespace power
