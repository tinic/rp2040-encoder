#include "ws2812_led.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"

WS2812Led& WS2812Led::instance() {
    static WS2812Led led;
    if (!led.initialized) {
        led.init();
        led.initialized = true;
    }
    return led;
}

void WS2812Led::init() {
    const auto offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, PICO_DEFAULT_WS2812_PIN, 800000, false);
}

void WS2812Led::set_color(uint8_t red, uint8_t green, uint8_t blue) {
    const uint32_t pixel_grb = (static_cast<uint32_t>(green) << 16) | (static_cast<uint32_t>(red) << 8) | blue;
    put_pixel(pixel_grb);
}

void WS2812Led::set_red() {
    set_color(255, 0, 0);
}

void WS2812Led::set_green() {
    set_color(0, 255, 0);
}

void WS2812Led::set_blue() {
    set_color(0, 0, 255);
}

void WS2812Led::set_off() {
    set_color(0, 0, 0);
}

void WS2812Led::put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}