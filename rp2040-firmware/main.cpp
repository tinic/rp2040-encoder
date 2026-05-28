#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "position.h"
#include "usb_device.h"
#include "ws2812_led.h"

int main() {
    WS2812Led::instance().set_blue();

    USBDevice::instance();

    Position& pos = Position::instance();

    if (pos.should_apply_defaults()) {
        pos.set_scale(0, 0.001);
        pos.set_scale(1, 0.001);
        pos.set_scale(2, 0.001);
        pos.set_scale(3, 0.1);
    }

    pos.enable_test_mode(false);

    WS2812Led::instance().set_green();

    watchdog_enable(1000, true);

    while (true) {
        USBDevice::instance().task();
        pos.tick();
        watchdog_update();
    }
}
