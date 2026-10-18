#include "power.hpp"

#include "esp_log.h"

// Registers, as pocket-tank uses them on the same chip (github.com/mediacutlet/pocket-tank):
//   0x00 STATUS1: bit 3 battery present, bit 5 USB (VBUS) good
//   0x01 STATUS2: bits 7:5 == 1 while charging
//   0x61 precharge current, 0x63 end-of-charge current (bits 3:0, 25 mA steps)
//   0x62 constant charge current (bits 4:0: 25 mA steps to 200, then 100 mA steps to 1000)
//   0xA4 the fuel gauge's state of charge, in percent

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
    for (uint8_t reg : {static_cast<uint8_t>(0x61), static_cast<uint8_t>(0x63)})
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

}  // namespace power
