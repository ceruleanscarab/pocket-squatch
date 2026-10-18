// Waveshare ESP32-S3-Touch-AMOLED-1.8: the display, brightness and touch, behind the same
// board.h as the 4" board, so main.cpp, the sim and the scene don't change.
//
// The scene is drawn 480x480, as on the 4" board. This panel is 368x448 portrait, so each
// frame is scaled to 368x368 (bilinear) and centred, with 40 black rows above and below
// (black is "off" on an AMOLED). Touches are mapped back into the scene's 480x480, and a
// touch in the black bars lands on the nearest edge of the picture, which is where the
// status strip and the tray handle are.
//
// Two revisions of this board exist and are told apart at boot: V1 has an SH8601 panel and
// an FT3168 touch chip, V2 a CO5300 panel (same commands, 16 px x offset) and a CST816
// touch chip, which reports one finger only (so no two-finger scold on V2).
//
// Pins, the IO expander's bits, the panel's start-up commands and the V2 touch calibration
// are from pocket-tank (github.com/mediacutlet/pocket-tank, MIT), which runs on both
// revisions, and Waveshare's examples for the board.
#include "board.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "board";

// ---------------------------------------------------------------- pins (from pocket-tank)

#define PIN_LCD_CS 12
#define PIN_LCD_PCLK 11
#define PIN_LCD_D0 4
#define PIN_LCD_D1 5
#define PIN_LCD_D2 6
#define PIN_LCD_D3 7
#define PIN_I2C_SDA 15
#define PIN_I2C_SCL 14
#define LCD_HOST SPI2_HOST

#define PIN_I2S_MCLK 16  // audio, as pocket-tank (github.com/mediacutlet/pocket-tank) wires it on this board
#define PIN_I2S_BCLK 9
#define PIN_I2S_WS 45
#define PIN_I2S_DOUT 8
#define PIN_AMP_EN 46  // the NS4150B amplifier's enable, pulled down on the board
#define ADDR_EXPANDER 0x20  // TCA9554: bit 0 LCD reset, bit 1 panel power, bit 2 touch reset, bit 7 SD card select
#define ADDR_FT3168 0x38    // V1 touch
#define ADDR_CST816 0x15    // V2 touch: answering means a V2 board

#define PANEL_W 368
#define PANEL_H 448
#define V2_X_GAP 16

// The scene's 480x480, scaled into the middle of the panel.
#define OUT 368
#define OUT_Y ((PANEL_H - OUT) / 2)  // 40

#define STRIPE_ROWS 32
#define STRIPE_BYTES (PANEL_W * STRIPE_ROWS * 2)

// ---------------------------------------------------------------- state

static i2c_master_bus_handle_t s_i2c;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static bool s_v2;

// Two 480x480 frames in PSRAM: the scene draws into one while the other is scaled and sent
// to the panel by a task on the other core.
static uint16_t* s_scene[2];
static int s_draw;                   // which one the scene draws into
static const uint16_t* s_showing;    // the one being scaled and sent
static SemaphoreHandle_t s_frame_ready, s_frame_done;
static SemaphoreHandle_t s_io_lock;  // frames (present task) and commands (brightness) share the link
static uint16_t* s_stripe[2];  // DMA-capable, ping-pong
static SemaphoreHandle_t s_stripe_free;
static bool s_cleared;  // the black bars, sent once

// Scaling tables: for each output column and row, the first source pixel and the weight
// of the next one, in 32nds.
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

static const sh8601_lcd_init_cmd_t kInitCmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},  // RGB565
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0x00}, 1, 0},  // brightness: dark until the first frame
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x11, NULL, 0, 100},
    {0x29, NULL, 0, 0},
};

// A DCS command over the QSPI link, framed as the sh8601 driver frames its own.
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

// Panel power on and the panel and touch resets pulsed, through the TCA9554.
static esp_err_t power_up(void) {
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ADDR_EXPANDER, .scl_speed_hz = 400000};
    i2c_master_dev_handle_t dev;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &cfg, &dev), TAG, "no IO expander");
    const uint8_t config[2] = {0x03, (uint8_t)~0x87};  // bits 0, 1, 2 and 7 are outputs
    const uint8_t low[2] = {0x01, 0x80};               // resets low, SD card deselected
    const uint8_t high[2] = {0x01, 0x87};
    esp_err_t err = i2c_master_transmit(dev, config, 2, 100);
    if (err == ESP_OK) err = i2c_master_transmit(dev, low, 2, 100);
    vTaskDelay(pdMS_TO_TICKS(20));
    if (err == ESP_OK) err = i2c_master_transmit(dev, high, 2, 100);
    vTaskDelay(pdMS_TO_TICKS(120));
    i2c_master_bus_rm_device(dev);
    return err;
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
        .reset_gpio_num = -1,  // on the expander, done in power_up
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void*)&vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(s_io, &dev, &s_panel), TAG, "sh8601");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, s_v2 ? V2_X_GAP : 0, 0), TAG, "panel gap");
    return esp_lcd_panel_disp_on_off(s_panel, true);
}

static esp_err_t init_touch(void) {
    esp_lcd_panel_io_i2c_config_t io_cfg = s_v2 ? (esp_lcd_panel_io_i2c_config_t)ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG()
                                                : (esp_lcd_panel_io_i2c_config_t)ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.dev_addr = s_v2 ? ADDR_CST816 : ADDR_FT3168;
    io_cfg.scl_speed_hz = 400000;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(s_i2c, &io_cfg, &io), TAG, "touch io");
    const esp_lcd_touch_config_t cfg = {
        .x_max = PANEL_W,
        .y_max = PANEL_H,
        .rst_gpio_num = GPIO_NUM_NC,  // on the expander
        .int_gpio_num = GPIO_NUM_NC,  // polled each frame
    };
    return s_v2 ? esp_lcd_touch_new_i2c_cst816s(io, &cfg, &s_touch) : esp_lcd_touch_new_i2c_ft5x06(io, &cfg, &s_touch);
}

static void init_scaling(void) {
    for (int i = 0; i < OUT; i++) {
        float s = (i + 0.5f) * BOARD_WIDTH / OUT - 0.5f;  // centre of output px i, in scene px
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
    ESP_RETURN_ON_ERROR(power_up(), TAG, "power up");
    s_v2 = i2c_master_probe(s_i2c, ADDR_CST816, 50) == ESP_OK;
    ESP_LOGI(TAG, "Waveshare AMOLED 1.8, revision %s", s_v2 ? "V2 (CO5300 + CST816)" : "V1 (SH8601 + FT3168)");

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
    xTaskCreatePinnedToCore(present_task, "present", 3072, NULL, 4, NULL, 0);  // the game draws on core 1
    if (init_touch() != ESP_OK) ESP_LOGW(TAG, "no touch chip answered");  // the picture still works
    ESP_LOGI(TAG, "display ready: the 480x480 scene at 368x368, centred");
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

// RGB565 spread into one word with room for 5-bit weights: 0000 0GGG GGG0 0000 RRRR R000 00BB BBB.
#define SPREAD(c) ((((uint32_t)(c)) | ((uint32_t)(c) << 16)) & 0x07E0F81Fu)
static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }  // the panel wants big-endian

static void send(int y0, int rows, int cur) {
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y0, PANEL_W, y0 + rows, s_stripe[cur]);
    xSemaphoreGive(s_io_lock);
    if (err != ESP_OK) ESP_LOGE(TAG, "draw at row %d: %s", y0, esp_err_to_name(err));
}

// A source row, scaled across to OUT columns (spread, ready for the vertical blend). Each
// source row is used by one or two output rows, so the last two are kept.
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
    if (!s_cleared) {  // a fresh panel's memory isn't black
        s_cleared = true;
        for (int y0 = 0; y0 < PANEL_H; y0 += STRIPE_ROWS) {
            int n = PANEL_H - y0 < STRIPE_ROWS ? PANEL_H - y0 : STRIPE_ROWS;
            xSemaphoreTake(s_stripe_free, portMAX_DELAY);
            memset(s_stripe[cur], 0, (size_t)PANEL_W * n * 2);
            send(y0, n, cur);
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
            uint16_t* o = stripe + r * PANEL_W;
            for (int ox = 0; ox < OUT; ox++) {
                uint32_t v = ((top[ox] * (32 - fy) + bot[ox] * fy) >> 5) & 0x07E0F81Fu;
                o[ox] = swap16((uint16_t)(v | (v >> 16)));
            }
        }
        send(OUT_Y + oy0, n, cur);
        cur ^= 1;
    }
}

// On core 0: scales and sends each finished frame while the game draws the next on core 1.
static void present_task(void* arg) {
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_frame_ready, portMAX_DELAY);
        send_frame(s_showing);
        xSemaphoreGive(s_frame_done);  // its frame has been read; the last stripes finish on the wire
    }
}

void board_present(void) {
    if (!s_panel) return;
    xSemaphoreTake(s_frame_done, portMAX_DELAY);  // the frame before this one has been sent
    s_showing = s_scene[s_draw];
    s_draw ^= 1;
    xSemaphoreGive(s_frame_ready);
}

void board_backlight(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint8_t level = (uint8_t)(255 * percent / 100);
    dcs(0x51, &level, 1);  // an AMOLED has no backlight: this is the panel's own brightness
}

// Panel px -> scene px. The V2 touch chip reads about 14% large from the top left; this is
// pocket-tank's fit from nine-point taps on a V2 board. V1 was never measured: as reported.
static void to_scene(int px, int py, int16_t* sx, int16_t* sy) {
    float x = px, y = py;
    if (s_v2) {
        x = (x + 22.0f) / 1.138f;
        y = (y + 30.5f) / 1.142f;
    }
    float tx = x * BOARD_WIDTH / OUT;
    float ty = (y - OUT_Y) * BOARD_HEIGHT / OUT;
    if (tx < 0) tx = 0;
    if (tx > BOARD_WIDTH - 1) tx = BOARD_WIDTH - 1;
    if (ty < 0) ty = 0;
    if (ty > BOARD_HEIGHT - 1) ty = BOARD_HEIGHT - 1;
    *sx = (int16_t)tx;
    *sy = (int16_t)ty;
}

int board_read_touch(board_touch_t* out, int max) {
    if (!s_touch || esp_lcd_touch_read_data(s_touch) != ESP_OK) return 0;
    esp_lcd_touch_point_data_t pts[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
    uint8_t n = 0;
    if (esp_lcd_touch_get_data(s_touch, pts, &n, CONFIG_ESP_LCD_TOUCH_MAX_POINTS) != ESP_OK) return 0;
    int count = n < max ? n : max;
    for (int i = 0; i < count; i++) {
        out[i].track = (uint8_t)i;  // these chips report fingers in a steady order
        to_scene(pts[i].x, pts[i].y, &out[i].x, &out[i].y);
    }
    return count;
}

i2c_master_bus_handle_t board_i2c(void) { return s_i2c; }
