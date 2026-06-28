#ifndef WS2812_H
#define WS2812_H

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

void ws2812_init(uint pin);
void ws2812_put_pixel(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif