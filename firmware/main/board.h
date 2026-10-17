// The board: display, backlight and touch. Two boards implement it, picked in menuconfig
// (Pocket Squatch > Board):
//
//   board.c           Waveshare ESP32-S3-Touch-LCD-4B, 480x480: the scene px for px.
//                     Pins and the ST7701 start-up sequence come from Waveshare's board
//                     support package (waveshare/esp32_s3_touch_lcd_4b 2.0.0, Apache-2.0),
//                     copied without its LVGL, which the scene doesn't need.
//   board_amoled18.c  Waveshare ESP32-S3-Touch-AMOLED-1.8, 368x448: the scene scaled to
//                     368x368 and centred, touches mapped back.
//
// Either way the scene draws a 480x480 frame and gets touches in 480x480 coordinates.
// C, like the originals, so driver configuration stays close to them.
#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// The scene's size, on every board.
#define BOARD_WIDTH 480
#define BOARD_HEIGHT 480

typedef struct {
    uint8_t track;  // stays the same for one finger while it's down
    int16_t x, y;
} board_touch_t;

esp_err_t board_init(void);

// The frame buffer not on screen. Draw a whole frame into it, then present it.
uint16_t* board_draw_buffer(void);
// Shows the drawn buffer from the next refresh and waits for the swap, so the next frame
// can be drawn into the buffer that just left the screen.
void board_present(void);

void board_backlight(int percent);  // on the AMOLED: the panel's own brightness

// Fills up to max fingers currently touching and returns how many; 0 when none.
int board_read_touch(board_touch_t* out, int max);

i2c_master_bus_handle_t board_i2c(void);

#ifdef __cplusplus
}
#endif
