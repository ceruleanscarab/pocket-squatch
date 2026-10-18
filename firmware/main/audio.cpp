#include "audio.hpp"

#include <cstring>

#include "board.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "power.hpp"

// The ES8311 start-up and power-down sequences are pocket-tank's (MIT licence,
// github.com/mediacutlet/pocket-tank, firmware/main/codec_port.c), which follow Everest's
// reference sequence as carried in Espressif's es8311 driver: slave mode, 16-bit I2S, MCLK
// at 256 x the sample rate, DAC only (the microphone stays off), output to the amplifier.

namespace audio {

namespace {

const char* TAG = "audio";
constexpr int kBlock = squatch::Synth::kRate / 100;  // 10 ms
constexpr int64_t kIdleUs = 2 * 1000000LL;          // down this long after the last sound
constexpr int kSettleCodecMs = 150, kSettleAmpMs = 100;

i2c_master_dev_handle_t gCodec = nullptr;
i2s_chan_handle_t gTx = nullptr;
SemaphoreHandle_t gLock = nullptr;
TaskHandle_t gTask = nullptr;
squatch::Synth gSynth;
int16_t gBuf[kBlock];
bool gUp = false, gRail = false;

bool wr(uint8_t reg, uint8_t v) {
    const uint8_t b[2] = {reg, v};
    return i2c_master_transmit(gCodec, b, 2, 100) == ESP_OK;
}

template <size_t N>
bool writeAll(const uint8_t (&seq)[N][2]) {
    bool ok = true;
    for (size_t i = 0; i < N; i++) ok &= wr(seq[i][0], seq[i][1]);
    return ok;
}

bool codecDown() {
    static const uint8_t seq[][2] = {
        {0x32, 0x00}, {0x17, 0x00}, {0x0E, 0xFF}, {0x12, 0x02}, {0x14, 0x00}, {0x0D, 0xFA},
        {0x15, 0x00}, {0x37, 0x08}, {0x02, 0x10}, {0x00, 0x00}, {0x00, 0x1F}, {0x01, 0x30},
        {0x01, 0x00}, {0x45, 0x00}, {0x0D, 0xF8}, {0x02, 0x00},
    };
    return writeAll(seq);
}

bool codecUp() {
    static const uint8_t seq[][2] = {
        {0x00, 0x1F},                // everything in reset while the clocks are set
        {0x01, 0x30},                // MCLK in, BCLK on
        {0x02, 0x00},                // internal MCLK = MCLK
        {0x03, 0x10}, {0x04, 0x10},  // ADC and DAC oversampling 64
        {0x05, 0x00},                // clock dividers 1: ratio 256
        {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F},
        {0x00, 0x80},                // state machine on, slave serial port
        {0x01, 0x3F},                // + ADC, DAC and analog clocks
        {0x09, 0x0C}, {0x0A, 0x0C},  // I2S, 16-bit
        {0x0D, 0x03},                // analog up, reference charging fast
        {0x0E, 0x02}, {0x12, 0x00},  // DAC up
        {0x13, 0x10},                // output stage on
        {0x1C, 0x6A}, {0x37, 0x08},  // DAC EQ bypassed
        {0x32, 0xBF},                // DAC at 0 dB: the synth sets the level
        {0x31, 0x00},                // unmuted
        {0x0F, 0x00}, {0x45, 0x00},
    };
    return writeAll(seq);
}

void silence(int ms) {
    std::memset(gBuf, 0, sizeof gBuf);
    size_t w;
    for (int i = 0; i < ms / 10; i++) i2s_channel_write(gTx, gBuf, sizeof gBuf, &w, 100);
}

// Power up in an order that keeps the pops inside: rail, clocks, codec, silence, amplifier.
void bringUp() {
    if (gRail) {
        power::audioRail(true);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    i2s_channel_enable(gTx);
    bool ok = codecUp();
    silence(kSettleCodecMs);
    wr(0x0D, 0x02);  // reference charged: normal operation
    board_speaker_amp(true);
    silence(kSettleAmpMs);
    gUp = true;
    if (!ok) ESP_LOGW(TAG, "codec writes failed");
}

void bringDown() {
    board_speaker_amp(false);
    silence(10);
    i2s_channel_disable(gTx);
    codecDown();
    if (gRail) power::audioRail(false);
    gUp = false;
}

void player(void*) {
    int64_t quietSince = 0;
    for (;;) {
        if (!gUp) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            xSemaphoreTake(gLock, portMAX_DELAY);
            bool any = gSynth.active();
            xSemaphoreGive(gLock);
            if (!any) continue;
            bringUp();
            quietSince = 0;
        }
        xSemaphoreTake(gLock, portMAX_DELAY);
        bool live = gSynth.render(gBuf, kBlock);
        xSemaphoreGive(gLock);
        size_t w;
        i2s_channel_write(gTx, gBuf, sizeof gBuf, &w, 100);  // blocks about 10 ms: the DMA sets the pace
        const int64_t now = esp_timer_get_time();
        if (live) quietSince = 0;
        else if (!quietSince) quietSince = now;
        else if (now - quietSince > kIdleUs) bringDown();
    }
}

}  // namespace

bool init(i2c_master_bus_handle_t bus, bool switchRail) {
    gRail = switchRail;
    if (gRail) power::audioRail(true);  // the codec answers on I2C either way; this is its analog side
    int addr = 0;
    for (int a = 0x18; a <= 0x19 && bus; a++) {
        if (i2c_master_probe(bus, a, 50) == ESP_OK) {
            addr = a;
            break;
        }
    }
    if (!addr) {
        ESP_LOGW(TAG, "no ES8311: no sound");
        if (gRail) power::audioRail(false);
        return false;
    }
    i2c_device_config_t dc{};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = static_cast<uint16_t>(addr);
    dc.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(bus, &dc, &gCodec) != ESP_OK) return false;
    codecDown();
    if (gRail) power::audioRail(false);
    board_speaker_amp(false);

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    cc.dma_desc_num = 4;
    cc.dma_frame_num = kBlock;
    if (i2s_new_channel(&cc, &gTx, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "no I2S channel");
        return false;
    }
    const board_audio_pins_t pins = board_audio_pins();
    i2s_std_config_t std{};
    std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(squatch::Synth::kRate);  // MCLK = 256 x 16 kHz
    std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    std.gpio_cfg.mclk = static_cast<gpio_num_t>(pins.mclk);
    std.gpio_cfg.bclk = static_cast<gpio_num_t>(pins.bclk);
    std.gpio_cfg.ws = static_cast<gpio_num_t>(pins.ws);
    std.gpio_cfg.dout = static_cast<gpio_num_t>(pins.dout);
    std.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(gTx, &std) != ESP_OK) {
        ESP_LOGE(TAG, "I2S set-up failed");
        return false;
    }
    gLock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(player, "audio", 4096, nullptr, 6, &gTask, 0);
    ESP_LOGI(TAG, "ES8311 at 0x%02x: sound ready", addr);
    return true;
}

void play(squatch::Cue c, int volume) {
    if (!gTask) return;
    xSemaphoreTake(gLock, portMAX_DELAY);
    gSynth.play(c, volume);
    xSemaphoreGive(gLock);
    xTaskNotifyGive(gTask);
}

}  // namespace audio
