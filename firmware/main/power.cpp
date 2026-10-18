#include "power.hpp"

#include "esp_log.h"

// Registers, as pocket-tank uses them on the same chip (github.com/mediacutlet/pocket-tank):
//   0x00 STATUS1: bit 3 battery present, bit 5 USB (VBUS) good
//   0x01 STATUS2: bits 7:5 == 1 while charging
//   0x61 precharge current, 0x63 end-of-charge current (bits 3:0, 25 mA steps)
//   0x62 constant charge current (bits 4:0: 25 mA steps to 200, then 100 mA steps to 1000)
//   0xA4 the fuel gauge's state of charge, in percent
//   0x10 bit 0: power the board off
//   0x27 PWR key timing: 5:4 long-press report (1/1.5/2/2.5 s), 3:2 the chip's own forced
//        power-off hold (4/6/8/10 s), 1:0 the hold that powers on (128/512 ms, 1/2 s)
//   0x41 IRQ enables, 0x48-0x4A IRQ status (write 1 to clear); 0x49 bit 3 short press,
//        bit 2 long press

namespace power {

namespace {

const char* TAG = "power";
constexpr uint8_t kAddress = 0x34;
i2c_master_dev_handle_t gDev = nullptr;

bool rd(uint8_t reg, uint8_t& v) {
    return gDev && i2c_master_transmit_receive(gDev, &reg, 1, &v, 1, 100) == ESP_OK;
}

bool wr(uint8_t reg, uint8_t v) {
    const uint8_t b[2] = {reg, v};
    return gDev && i2c_master_transmit(gDev, b, 2, 100) == ESP_OK;
}

int chargeCode(int ma) {
    if (ma < 25) ma = 25;
    if (ma <= 200) return ma / 25;
    int c = ma / 100 + 6;
    return c > 16 ? 16 : c;
}

int chargeMaOf(int code) { return code <= 8 ? code * 25 : (code <= 16 ? (code - 6) * 100 : 1000); }

}  // namespace

bool init(i2c_master_bus_handle_t bus, int chargeMa, int trickleMa) {
    if (!bus || i2c_master_probe(bus, kAddress, 50) != ESP_OK) {
        ESP_LOGW(TAG, "no AXP2101: no battery reading");
        return false;
    }
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = kAddress;
    cfg.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(bus, &cfg, &gDev) != ESP_OK) return false;

    uint8_t v = 0;
    if (rd(0x62, v)) {
        int code = chargeCode(chargeMa);
        if (wr(0x62, static_cast<uint8_t>((v & 0xE0) | code)))
            ESP_LOGI(TAG, "charge current %d mA (was code %d)", chargeMaOf(code), v & 0x1F);
    }
    uint8_t trickle = static_cast<uint8_t>(trickleMa / 25);
    if (trickle < 1) trickle = 1;
    if (trickle > 8) trickle = 8;
    const uint8_t trickleRegs[] = {0x61, 0x63};  // precharge, end of charge
    for (uint8_t reg : trickleRegs)
        if (rd(reg, v)) wr(reg, static_cast<uint8_t>((v & 0xF0) | trickle));
    return true;
}

bool read(Battery& out) {
    uint8_t st1, st2, pct;
    if (!rd(0x00, st1) || !rd(0x01, st2)) return false;
    out.present = (st1 & 0x08) != 0;
    out.usb = (st1 & 0x20) != 0;
    out.charging = out.present && ((st2 >> 5) & 0x07) == 1;
    out.percent = -1;
    if (out.present && rd(0xA4, pct) && pct <= 100) out.percent = pct;
    return true;
}

void keyInit() {
    uint8_t v = 0;
    // Long press reported at 1.5 s, the chip's own cut at 10 s (so the firmware saves first),
    // and power-on after a 128 ms press, so a plain tap turns the board on.
    if (rd(0x27, v)) wr(0x27, static_cast<uint8_t>((v & 0xC0) | 0x10 | 0x0C | 0x00));
    if (rd(0x41, v)) wr(0x41, static_cast<uint8_t>(v | 0x0C));
    wr(0x48, 0xFF);  // forget the press that turned it on
    wr(0x49, 0xFF);
    wr(0x4A, 0xFF);
}

Key pollKey() {
    uint8_t st = 0;
    if (!rd(0x49, st) || !(st & 0x0C)) return Key::None;
    wr(0x49, static_cast<uint8_t>(st & 0x0C));
    return (st & 0x04) ? Key::Long : Key::Short;
}

void powerOff() {
    uint8_t v = 0;
    if (rd(0x10, v)) wr(0x10, static_cast<uint8_t>(v | 0x01));
}

}  // namespace power
