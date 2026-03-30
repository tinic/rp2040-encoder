#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pll.h"
#include "hardware/sync.h"
#include "hardware/xosc.h"
#include "pico/stdlib.h"
#include "position.h"
#include "quadrature_encoder.h"
#include "usb_device.h"
#include "ws2812_led.h"

static constexpr uint STATUS_LED_PIN = 8;

int main() {
    WS2812Led::instance().set_blue();

    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);
    gpio_put(STATUS_LED_PIN, 1);

    USBDevice::instance();

    Position& pos = Position::instance();

    pos.set_scale(0, 0.001);
    pos.set_scale(1, 0.001);
    pos.set_scale(2, 0.001);
    pos.set_scale(3, 0.1);
    
    pos.enable_test_mode(false);

    WS2812Led::instance().set_green();

    while (1) {
        USBDevice::instance().task();
    }
}
