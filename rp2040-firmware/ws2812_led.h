#ifndef WS2812_LED_H_
#define WS2812_LED_H_

#include <cstdint>

#include "hardware/pio.h"

class WS2812Led {
 public:
    static WS2812Led& instance();

    void set_color(uint8_t red, uint8_t green, uint8_t blue);
    void set_red();
    void set_green();
    void set_blue();
    void set_off();

 private:
    WS2812Led() = default;
    bool initialized = false;

    void init();
    void put_pixel(uint32_t pixel_grb);

    PIO pio = pio1;
    uint sm = 0;
};

#endif