#include "board.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_io_expander_tca9554.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "board";

// ---------------------------------------------------------------- pins (from the BSP)

#define I2C_PORT I2C_NUM_1
#define I2C_SDA GPIO_NUM_47
#define I2C_SCL GPIO_NUM_48
#define I2C_HZ 100000

#define LCD_VSYNC GPIO_NUM_3
#define LCD_HSYNC GPIO_NUM_46
#define LCD_DE GPIO_NUM_17
#define LCD_PCLK GPIO_NUM_9
#define LCD_BACKLIGHT GPIO_NUM_4
#define LCD_PCLK_HZ (16 * 1000 * 1000)
#define LCD_BOUNCE_LINES 20

// On the TCA9554 IO expander: the ST7701's 3-wire SPI, and the shared LCD/touch reset.
#define EXP_LCD_CS IO_EXPANDER_PIN_NUM_0
#define EXP_LCD_SDA IO_EXPANDER_PIN_NUM_1
#define EXP_LCD_SCL IO_EXPANDER_PIN_NUM_2
#define EXP_RESET IO_EXPANDER_PIN_NUM_5
#define EXP_TOUCH_INT IO_EXPANDER_PIN_NUM_6  // held low through reset: GT911 at address 0x5D
#define EXP_SPEAKER_AMP IO_EXPANDER_PIN_NUM_3  // the speaker amplifier's enable

// Audio, from the BSP (BSP_I2S_*): the ES8311 codec on I2S.
#define I2S_MCLK 5
#define I2S_BCLK 16
#define I2S_WS 7
#define I2S_DOUT 6

#define BACKLIGHT_LEDC_CH LEDC_CHANNEL_1
#define BACKLIGHT_LEDC_TIMER LEDC_TIMER_1

// The panel's start-up commands, in st7701_init.c (verbatim from the BSP).
extern const st7701_lcd_init_cmd_t board_st7701_init_cmds[];
extern const size_t board_st7701_init_cmd_count;

// ---------------------------------------------------------------- state

static i2c_master_bus_handle_t s_i2c;
static esp_io_expander_handle_t s_expander;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static uint16_t* s_fb[2];
static int s_back = 1;  // buffer 0 is on screen first
static SemaphoreHandle_t s_vsync;
static volatile uint32_t s_frames;  // counts refreshes, from the vsync interrupt

static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* data, void* ctx) {
    BaseType_t woke = pdFALSE;
    s_frames++;
    xSemaphoreGiveFromISR(s_vsync, &woke);
    return woke == pdTRUE;
}

// ---------------------------------------------------------------- setup

static esp_err_t init_backlight(void) {
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BACKLIGHT_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 1023,  // the backlight is active low: full duty is dark
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    return ledc_channel_config(&channel);
}

void board_display_power(bool on) { (void)on; }  // the backlight does it (the RGB panel needs feeding)

void board_backlight(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CH, (uint32_t)(1023 * (100 - percent) / 100));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CH);
}

static esp_err_t init_i2c(void) {
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_i2c), TAG, "i2c bus");
    return esp_io_expander_new_i2c_tca9554(s_i2c, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_expander);
}

// Resets the panel and the touch controller together, holding the touch INT line low so
// the GT911 comes up at its usual address. Same order and timing as the BSP.
static void reset_panel_and_touch(void) {
    esp_io_expander_set_dir(s_expander, EXP_RESET | EXP_TOUCH_INT, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(s_expander, EXP_TOUCH_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_io_expander_set_level(s_expander, EXP_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_io_expander_set_level(s_expander, EXP_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_io_expander_set_dir(s_expander, EXP_TOUCH_INT, IO_EXPANDER_INPUT);
}

static esp_err_t init_panel(void) {
    // The ST7701 takes its start-up commands over a bit-banged 3-wire SPI on the expander.
    spi_line_config_t line = {
        .cs_io_type = IO_TYPE_EXPANDER,
        .cs_expander_pin = EXP_LCD_CS,
        .scl_io_type = IO_TYPE_EXPANDER,
        .scl_expander_pin = EXP_LCD_SCL,
        .sda_io_type = IO_TYPE_EXPANDER,
        .sda_expander_pin = EXP_LCD_SDA,
        .io_expander = s_expander,
    };
    esp_lcd_panel_io_3wire_spi_config_t io_cfg = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line, 0);
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_3wire_spi(&io_cfg, &io), TAG, "3-wire spi");

    // Then pixels stream over the 16-bit RGB bus from two frame buffers in PSRAM.
    esp_lcd_rgb_panel_config_t rgb = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = ST7701_480_480_PANEL_60HZ_RGB_TIMING(),
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = BOARD_WIDTH * LCD_BOUNCE_LINES,
        .psram_trans_align = 64,
        .hsync_gpio_num = LCD_HSYNC,
        .vsync_gpio_num = LCD_VSYNC,
        .de_gpio_num = LCD_DE,
        .pclk_gpio_num = LCD_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42, GPIO_NUM_2, GPIO_NUM_1, GPIO_NUM_21,
                           GPIO_NUM_8, GPIO_NUM_18, GPIO_NUM_45, GPIO_NUM_38, GPIO_NUM_39, GPIO_NUM_10,
                           GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14},
        .flags.fb_in_psram = 1,
    };
    rgb.timings.pclk_hz = LCD_PCLK_HZ;
    rgb.timings.h_res = BOARD_WIDTH;
    rgb.timings.v_res = BOARD_HEIGHT;

    st7701_vendor_config_t vendor = {
        .rgb_config = &rgb,
        .init_cmds = board_st7701_init_cmds,
        .init_cmds_size = board_st7701_init_cmd_count,
        .flags = {.auto_del_panel_io = 1, .mirror_by_cmd = 0},
    };
    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = GPIO_NUM_NC,  // reset is on the expander, done above
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(io, &dev, &s_panel), TAG, "st7701");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");

    void* fb0 = NULL;
    void* fb1 = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1), TAG, "frame buffers");
    s_fb[0] = fb0;
    s_fb[1] = fb1;

    s_vsync = xSemaphoreCreateBinary();
    const esp_lcd_rgb_panel_event_callbacks_t cbs = {.on_vsync = on_vsync};
    return esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, NULL);
}

static esp_err_t init_touch(void) {
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = I2C_HZ;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(s_i2c, &io_cfg, &io), TAG, "touch io");
    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_WIDTH,
        .y_max = BOARD_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,  // on the expander, shared with the panel
        .int_gpio_num = GPIO_NUM_NC,  // not wired to the ESP32: touch is polled each frame
    };
    return esp_lcd_touch_new_i2c_gt911(io, &cfg, &s_touch);
}

esp_err_t board_init(void) {
    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "backlight");  // dark until the first frame
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "i2c");
    reset_panel_and_touch();
    ESP_RETURN_ON_ERROR(init_panel(), TAG, "panel");
    ESP_RETURN_ON_ERROR(init_touch(), TAG, "touch");
    ESP_LOGI(TAG, "display and touch ready");
    return ESP_OK;
}

// ---------------------------------------------------------------- audio

board_audio_pins_t board_audio_pins(void) { return (board_audio_pins_t){I2S_MCLK, I2S_BCLK, I2S_WS, I2S_DOUT}; }

void board_speaker_amp(bool on) {
    static bool configured;
    if (!s_expander) return;
    if (!configured) {
        esp_io_expander_set_dir(s_expander, EXP_SPEAKER_AMP, IO_EXPANDER_OUTPUT);
        configured = true;
    }
    esp_io_expander_set_level(s_expander, EXP_SPEAKER_AMP, on ? 1 : 0);
}

// ---------------------------------------------------------------- per frame

uint16_t* board_draw_buffer(void) { return s_fb[s_back]; }

void board_present(void) {
    // Handing the driver one of its own frame buffers switches to it, without a copy, from
    // the next refresh. Wait for a refresh that starts after this call before drawing into
    // the other buffer, which is the one that was on screen until then.
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BOARD_WIDTH, BOARD_HEIGHT, s_fb[s_back]);
    uint32_t start = s_frames;
    for (int tries = 0; s_frames == start && tries < 5; tries++) xSemaphoreTake(s_vsync, pdMS_TO_TICKS(20));
    s_back ^= 1;
}

int board_read_touch(board_touch_t* out, int max) {
    if (esp_lcd_touch_read_data(s_touch) != ESP_OK) return 0;
    esp_lcd_touch_point_data_t pts[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
    uint8_t n = 0;
    if (esp_lcd_touch_get_data(s_touch, pts, &n, CONFIG_ESP_LCD_TOUCH_MAX_POINTS) != ESP_OK) return 0;
    int count = n < max ? n : max;
    for (int i = 0; i < count; i++) {
        out[i].track = pts[i].track_id;
        out[i].x = (int16_t)pts[i].x;
        out[i].y = (int16_t)pts[i].y;
    }
    return count;
}

i2c_master_bus_handle_t board_i2c(void) { return s_i2c; }
