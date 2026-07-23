#include "ws2812.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"

static PIO pixel_pio;
static uint pixel_sm;
static bool initialized;
static uint32_t last_grb;
static bool has_last_color;

bool ws2812_init(uint pin) {
    if (initialized) {
        return true;
    }

    pixel_pio = pio0;
    const int claimed_sm = pio_claim_unused_sm(pixel_pio, false);
    if (claimed_sm < 0) {
        return false;
    }
    pixel_sm = (uint)claimed_sm;

    const uint offset = pio_add_program(pixel_pio, &ws2812_program);

    pio_gpio_init(pixel_pio, pin);
    pio_sm_set_consecutive_pindirs(pixel_pio, pixel_sm, pin, 1, true);

    pio_sm_config config = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&config, pin);
    sm_config_set_out_shift(&config, false, true, 24);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);

    const float cycles_per_bit = (float)(ws2812_T1 + ws2812_T2 + ws2812_T3);
    const float divider = (float)clock_get_hz(clk_sys) / (800000.0f * cycles_per_bit);
    sm_config_set_clkdiv(&config, divider);

    pio_sm_init(pixel_pio, pixel_sm, offset, &config);
    pio_sm_set_enabled(pixel_pio, pixel_sm, true);

    initialized = true;
    has_last_color = false;
    ws2812_off();
    return true;
}

void ws2812_put_pixel(uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized) {
        return;
    }

    const uint32_t grb = ((uint32_t)g << 16U) |
                         ((uint32_t)r << 8U) |
                         (uint32_t)b;

    if (has_last_color && grb == last_grb) {
        return;
    }

    pio_sm_put_blocking(pixel_pio, pixel_sm, grb << 8U);
    sleep_us(80);

    last_grb = grb;
    has_last_color = true;
}

void ws2812_off(void) {
    ws2812_put_pixel(0, 0, 0);
}
