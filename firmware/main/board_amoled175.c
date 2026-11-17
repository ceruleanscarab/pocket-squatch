// Waveshare ESP32-S3-Touch-AMOLED-1.75C (round, in the aluminium case): the display, brightness and
// touch, behind the same board.h as the other boards, so main.cpp, the sim and the scene don't change.
//
// The scene is drawn 480x480. This panel is round, 466x466: each frame is scaled to 400x400 (bilinear)
// and centred, so the circle shows Scene::kRoundR (278) scene px around the middle and only the
// scene's corners are lost; its round layout (Scene::setRound) keeps everything that matters inside.
// Touches are mapped back into the scene's 480x480.
//
// Panel: CO5300 over QSPI (the same commands as the SH8601, so the sh8601 driver runs it), with a
// 6 px column offset. Touch: CST9217 at 0x5A, read directly (no esp_lcd driver for it). The resets
// are plain GPIOs on this board (no IO expander). Pins and the column offset are from Waveshare's
// 1.75C examples (github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C, board_config/pin_config.h);
// the touch protocol is SensorLib's TouchDrvCST92xx (MIT), as those examples use.
#include "board.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "board";

// ---------------------------------------------------------------- pins (Waveshare's pin_config.h for the 1.75C)

#define PIN_LCD_CS 12
#define PIN_LCD_PCLK 38
#define PIN_LCD_D0 4
#define PIN_LCD_D1 5
#define PIN_LCD_D2 6
#define PIN_LCD_D3 7
#define PIN_LCD_RST 1
#define PIN_TP_RST 2
#define PIN_TP_INT 11
#define PIN_I2C_SDA 15
#define PIN_I2C_SCL 14
#define LCD_HOST SPI2_HOST

#define PIN_I2S_MCLK 16
#define PIN_I2S_BCLK 9
#define PIN_I2S_WS 45
#define PIN_I2S_DOUT 8
#define PIN_AMP_EN 46  // the speaker amplifier's enable ("PA")
#define ADDR_CST9217 0x5A

#define PANEL 466
#define X_GAP 6  // the CO5300's first visible column

#define OUT 400                    // the scene's 480, as shown
#define OFF 32  // where it starts, across and down (even: these panels want even window edges)

#define STRIPE_ROWS 32
#define STRIPE_BYTES (PANEL * STRIPE_ROWS * 2)

// ---------------------------------------------------------------- state

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_tp;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;

static uint16_t* s_scene[2];
static int s_draw;
static const uint16_t* s_showing;
static SemaphoreHandle_t s_frame_ready, s_frame_done;
static SemaphoreHandle_t s_io_lock;
static uint16_t* s_stripe[2];
static SemaphoreHandle_t s_stripe_free;
static bool s_cleared;

static uint16_t s_src[OUT];
static uint8_t s_frac[OUT];

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t* ev, void* ctx) {
    (void)io;
    (void)ev;
    (void)ctx;
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(s_stripe_free, &woke);
    return woke == pdTRUE;
}

// ---------------------------------------------------------------- setup

// As the 1.8" board's CO5300 (V2) start-up, which runs on hardware, with the window set to 466x466.
// (The column offset is applied per frame by esp_lcd_panel_set_gap.)
static const sh8601_lcd_init_cmd_t kInitCmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},  // RGB565
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0x00}, 1, 0},  // brightness: dark until the first frame
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, (PANEL - 1) >> 8, (PANEL - 1) & 0xFF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, (PANEL - 1) >> 8, (PANEL - 1) & 0xFF}, 4, 0},
    {0x11, NULL, 0, 100},
    {0x29, NULL, 0, 0},
};

static void dcs(uint8_t cmd, const uint8_t* param, size_t n) {
    if (!s_io) return;
    if (s_io_lock) xSemaphoreTake(s_io_lock, portMAX_DELAY);
    esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (cmd << 8), param, n);
    if (s_io_lock) xSemaphoreGive(s_io_lock);
}

static esp_err_t init_i2c(void) {
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c);
}

static esp_err_t init_panel(void) {
    for (int i = 0; i < 2; i++) {
        s_stripe[i] = heap_caps_malloc(STRIPE_BYTES, MALLOC_CAP_DMA);
        ESP_RETURN_ON_FALSE(s_stripe[i], ESP_ERR_NO_MEM, TAG, "stripe buffer");
    }
    s_stripe_free = xSemaphoreCreateCounting(2, 2);

    const spi_bus_config_t bus =
        SH8601_PANEL_BUS_QSPI_CONFIG(PIN_LCD_PCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3, STRIPE_BYTES);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "qspi bus");
    esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, on_trans_done, NULL);
    io_cfg.pclk_hz = 80 * 1000 * 1000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io), TAG, "panel io");

    const sh8601_vendor_config_t vendor = {
        .init_cmds = kInitCmds,
        .init_cmds_size = sizeof kInitCmds / sizeof kInitCmds[0],
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void*)&vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(s_io, &dev, &s_panel), TAG, "co5300");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, X_GAP, 0), TAG, "panel gap");
    return esp_lcd_panel_disp_on_off(s_panel, true);
}

static esp_err_t init_touch(void) {
    const gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_TP_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    const gpio_config_t irq = {.pin_bit_mask = 1ULL << PIN_TP_INT, .mode = GPIO_MODE_INPUT,
                               .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&irq);
    gpio_set_level(PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(60));
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ADDR_CST9217, .scl_speed_hz = 400000};
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &cfg, &s_tp), TAG, "touch device");
    return i2c_master_probe(s_i2c, ADDR_CST9217, 50);
}

static void init_scaling(void) {
    for (int i = 0; i < OUT; i++) {
        float s = (i + 0.5f) * BOARD_WIDTH / OUT - 0.5f;
        if (s < 0) s = 0;
        int a = (int)s;
        if (a > BOARD_WIDTH - 2) a = BOARD_WIDTH - 2;
        int f = (int)((s - a) * 32 + 0.5f);
        s_src[i] = (uint16_t)a;
        s_frac[i] = (uint8_t)(f > 32 ? 32 : f);
    }
}

static void present_task(void* arg);

esp_err_t board_init(void) {
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "i2c");
    ESP_LOGI(TAG, "Waveshare AMOLED 1.75C (round, CO5300 + CST9217)");

    for (int i = 0; i < 2; i++) {
        s_scene[i] = heap_caps_malloc(BOARD_WIDTH * BOARD_HEIGHT * 2, MALLOC_CAP_SPIRAM);
        ESP_RETURN_ON_FALSE(s_scene[i], ESP_ERR_NO_MEM, TAG, "scene buffer");
        memset(s_scene[i], 0, BOARD_WIDTH * BOARD_HEIGHT * 2);
    }
    init_scaling();

    ESP_RETURN_ON_ERROR(init_panel(), TAG, "panel");
    s_io_lock = xSemaphoreCreateMutex();
    s_frame_ready = xSemaphoreCreateBinary();
    s_frame_done = xSemaphoreCreateBinary();
    xSemaphoreGive(s_frame_done);
    xTaskCreatePinnedToCore(present_task, "present", 3072, NULL, 4, NULL, 0);
    if (init_touch() != ESP_OK) ESP_LOGW(TAG, "no touch chip answered");
    ESP_LOGI(TAG, "display ready: the 480x480 scene at 400x400, centred in the circle");
    return ESP_OK;
}

// ---------------------------------------------------------------- audio

board_audio_pins_t board_audio_pins(void) {
    return (board_audio_pins_t){PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT};
}

void board_speaker_amp(bool on) {
    static bool configured;
    if (!configured) {
        const gpio_config_t io = {.pin_bit_mask = 1ULL << PIN_AMP_EN, .mode = GPIO_MODE_OUTPUT,
                                  .pull_down_en = GPIO_PULLDOWN_ENABLE};
        gpio_config(&io);
        configured = true;
    }
    gpio_set_level(PIN_AMP_EN, on);
}

// ---------------------------------------------------------------- per frame

uint16_t* board_draw_buffer(void) { return s_scene[s_draw]; }

#define SPREAD(c) ((((uint32_t)(c)) | ((uint32_t)(c) << 16)) & 0x07E0F81Fu)
static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

static void send(int x0, int w, int y0, int rows, int cur) {
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x0 + w, y0 + rows, s_stripe[cur]);
    xSemaphoreGive(s_io_lock);
    if (err != ESP_OK) ESP_LOGE(TAG, "draw at row %d: %s", y0, esp_err_to_name(err));
}

static uint32_t s_hrow[2][OUT];
static int s_hrow_src[2] = {-1, -1};

static const uint32_t* scaled_row(const uint16_t* scene, int sy, int keep) {
    for (int i = 0; i < 2; i++)
        if (s_hrow_src[i] == sy) return s_hrow[i];
    int slot = s_hrow_src[0] == keep ? 1 : 0;
    const uint16_t* a = scene + sy * BOARD_WIDTH;
    uint32_t* o = s_hrow[slot];
    for (int ox = 0; ox < OUT; ox++) {
        int sx = s_src[ox];
        uint32_t fx = s_frac[ox];
        o[ox] = ((SPREAD(a[sx]) * (32 - fx) + SPREAD(a[sx + 1]) * fx) >> 5) & 0x07E0F81Fu;
    }
    s_hrow_src[slot] = sy;
    return o;
}

static void send_frame(const uint16_t* scene) {
    int cur = 0;
    if (!s_cleared) {
        s_cleared = true;
        for (int y0 = 0; y0 < PANEL; y0 += STRIPE_ROWS) {
            int n = PANEL - y0 < STRIPE_ROWS ? PANEL - y0 : STRIPE_ROWS;
            xSemaphoreTake(s_stripe_free, portMAX_DELAY);
            memset(s_stripe[cur], 0, (size_t)PANEL * n * 2);
            send(0, PANEL, y0, n, cur);
            cur ^= 1;
        }
    }
    s_hrow_src[0] = s_hrow_src[1] = -1;
    for (int oy0 = 0; oy0 < OUT; oy0 += STRIPE_ROWS) {
        int n = OUT - oy0 < STRIPE_ROWS ? OUT - oy0 : STRIPE_ROWS;
        xSemaphoreTake(s_stripe_free, portMAX_DELAY);
        uint16_t* stripe = s_stripe[cur];
        for (int r = 0; r < n; r++) {
            int oy = oy0 + r, sy = s_src[oy];
            const uint32_t* top = scaled_row(scene, sy, sy + 1);
            const uint32_t* bot = scaled_row(scene, sy + 1, sy);
            uint32_t fy = s_frac[oy];
            uint16_t* o = stripe + r * OUT;
            for (int ox = 0; ox < OUT; ox++) {
                uint32_t v = ((top[ox] * (32 - fy) + bot[ox] * fy) >> 5) & 0x07E0F81Fu;
                o[ox] = swap16((uint16_t)(v | (v >> 16)));
            }
        }
        send(OFF, OUT, OFF + oy0, n, cur);
        cur ^= 1;
    }
}

static void present_task(void* arg) {
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_frame_ready, portMAX_DELAY);
        send_frame(s_showing);
        xSemaphoreGive(s_frame_done);
        vTaskDelay(1);  // let core 0's idle task run (the task watchdog)
    }
}

void board_present(void) {
    if (!s_panel) return;
    xSemaphoreTake(s_frame_done, portMAX_DELAY);
    s_showing = s_scene[s_draw];
    s_draw ^= 1;
    xSemaphoreGive(s_frame_ready);
}

void board_display_power(bool on) {
    if (on) {
        dcs(0x11, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
        dcs(0x29, NULL, 0);
    } else {
        dcs(0x28, NULL, 0);
        dcs(0x10, NULL, 0);
    }
}

void board_backlight(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint8_t level = (uint8_t)(255 * percent / 100);
    dcs(0x51, &level, 1);
}

// ---------------------------------------------------------------- touch

// One read of the CST9217: up to two fingers, 5 bytes each (the second starts 2 bytes later), the
// count in byte 5, an ack (0xAB) in byte 6; then the ack is written back.
int board_read_touch(board_touch_t* out, int max) {
    if (!s_tp) return 0;
    const uint8_t cmd[2] = {0xD0, 0x00};
    uint8_t buf[15] = {0};
    if (i2c_master_transmit_receive(s_tp, cmd, 2, buf, sizeof buf, 50) != ESP_OK) return 0;
    const uint8_t ack[3] = {0xD0, 0x00, 0xAB};
    i2c_master_transmit(s_tp, ack, 3, 50);
    if (buf[6] != 0xAB) return 0;
    int n = buf[5] & 0x7F;
    if (n > 2) n = 2;
    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        const uint8_t* d = buf + i * 5 + (i ? 2 : 0);
        if ((d[0] & 0x0F) != 0x06) continue;  // 6: touching
        int px = (d[1] << 4) | (d[3] >> 4), py = (d[2] << 4) | (d[3] & 0x0F);
        float sx = (px - OFF + 0.5f) * BOARD_WIDTH / OUT, sy = (py - OFF + 0.5f) * BOARD_HEIGHT / OUT;
        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        if (sx > BOARD_WIDTH - 1) sx = BOARD_WIDTH - 1;
        if (sy > BOARD_HEIGHT - 1) sy = BOARD_HEIGHT - 1;
        out[count].track = (uint8_t)(d[0] >> 4);
        out[count].x = (int16_t)sx;
        out[count].y = (int16_t)sy;
        count++;
    }
    static int s_was = 0;  // where each touch lands, for checking the calibration in the log
    if (count > 0 && s_was == 0) ESP_LOGI(TAG, "touch at scene %d,%d", out[0].x, out[0].y);
    s_was = count;
    return count;
}

i2c_master_bus_handle_t board_i2c(void) { return s_i2c; }
