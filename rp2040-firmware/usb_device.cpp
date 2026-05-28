#include "usb_device.h"

#include <array>
#include <cstring>

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/time.h"
#include "position.h"
#include "tusb.h"
#include "version.h"
#include "ws2812_led.h"
tusb_desc_device_t const desc_device = {.bLength = sizeof(tusb_desc_device_t),
                                        .bDescriptorType = TUSB_DESC_DEVICE,
                                        .bcdUSB = 0x0200,
                                        .bDeviceClass = 0xFF,
                                        .bDeviceSubClass = 0x00,
                                        .bDeviceProtocol = 0x00,
                                        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
                                        .idVendor = USBDevice::VENDOR_ID,
                                        .idProduct = USBDevice::PRODUCT_ID,
                                        .bcdDevice = 0x0100,
                                        .iManufacturer = 0x01,
                                        .iProduct = 0x02,
                                        .iSerialNumber = 0x03,
                                        .bNumConfigurations = 0x01};

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    TUD_VENDOR_DESCRIPTOR(USBDevice::VENDOR_INTERFACE, 0, USBDevice::EP_VENDOR_OUT, USBDevice::EP_VENDOR_IN, 64)};

char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "RP2040",
    "Quadrature Encoder",
    "4ENC-" GIT_SHORT_SHA "-" GIT_COMMIT_DATE_SHORT,
};

USBDevice& USBDevice::instance() {
    static USBDevice device;
    if (!device.initialized) {
        device.init();
        device.initialized = true;
    }
    return device;
}

void USBDevice::init() {
    tusb_init();
    
    // Set USB interrupt to lower priority than encoder interrupts
    irq_set_priority(USBCTRL_IRQ, 255);  // Lowest priority
}

void USBDevice::task() {
    tud_task();

    // One USB transfer = one command. Inspect the opcode at request_buf[0] and
    // treat any unknown opcode as a framing error: drop the transfer silently
    // rather than walking byte-by-byte through it.
    while (tud_vendor_n_available(VENDOR_INTERFACE)) {
        std::array<uint8_t, 64> request_buf{};
        uint32_t count = tud_vendor_n_read(VENDOR_INTERFACE, request_buf.data(), request_buf.size());
        if (count == 0) continue;

        static bool flip = false;
        if (flip) {
            WS2812Led::instance().set_color(64, 64, 0);
        } else {
            WS2812Led::instance().set_off();
        }
        flip ^= 1;

        switch (request_buf[0]) {
            case VENDOR_REQUEST_GET_POSITION:
                (void)send_position_data();
                break;
            case VENDOR_REQUEST_SET_TEST_MODE:
                if (count >= 2) {
                    uint8_t mode = request_buf[1];
                    if (mode == 0) {
                        Position::instance().enable_test_mode(false);
                    } else {
                        Position::instance().enable_test_mode(true);
                        Position::instance().set_test_pattern(mode - 1);
                    }
                }
                break;
            case VENDOR_REQUEST_SET_SCALE:
                if (count >= 10) {
                    uint8_t encoder_index = request_buf[1];
                    double scale;
                    memcpy(&scale, &request_buf[2], sizeof(double));
                    if (encoder_index < Position::kPositions) {
                        Position::instance().set_scale(encoder_index, scale);
                    }
                }
                break;
            case VENDOR_REQUEST_GET_SCALE:
                (void)send_scale_data();
                break;
            case VENDOR_REQUEST_RESET_POSITION:
                if (count >= 2) {
                    uint8_t encoder_index = request_buf[1];
                    if (encoder_index < Position::kPositions) {
                        (void)Position::instance().reset_encoder(encoder_index);
                    }
                }
                break;
        }
    }
}

bool USBDevice::send_position_data() {
    if (!initialized) {
        return false;
    }

    if (!tud_vendor_n_mounted(VENDOR_INTERFACE)) {
        return false;
    }

    static std::array<uint8_t, 64> buffer{};
    size_t bytes = 0;

    if (!Position::instance().get(buffer.data(), bytes)) {
        return false;
    }

    if (tud_vendor_n_write_available(VENDOR_INTERFACE) < buffer.size()) {
        return false;
    }

    uint32_t written = tud_vendor_n_write(VENDOR_INTERFACE, buffer.data(), buffer.size());
    if (buffer.size() != written) {
        return false;
    }
    tud_vendor_n_write_flush(VENDOR_INTERFACE);
    return true;
}

bool USBDevice::send_scale_data() {
    if (!initialized) {
        return false;
    }

    if (!tud_vendor_n_mounted(VENDOR_INTERFACE)) {
        return false;
    }

    static std::array<uint8_t, 64> buffer{};
    Position& pos = Position::instance();

    uint32_t sentinel = SCALE_DATA_SENTINEL;
    memcpy(buffer.data(), &sentinel, sizeof(sentinel));

    for (size_t i = 0; i < Position::kPositions; i++) {
        double scale = pos.get_scale(i);
        memcpy(&buffer[sizeof(sentinel) + i * sizeof(double)], &scale, sizeof(double));
    }

    if (tud_vendor_n_write_available(VENDOR_INTERFACE) < buffer.size()) {
        return false;
    }

    uint32_t written = tud_vendor_n_write(VENDOR_INTERFACE, buffer.data(), buffer.size());
    if (buffer.size() != written) {
        return false;
    }
    tud_vendor_n_write_flush(VENDOR_INTERFACE);
    return true;
}

extern "C" {

uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*)&desc_device;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    static uint16_t _desc_str[32];
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
            return NULL;

        const char* str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 31)
            chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
    (void)rhport;
    (void)stage;
    (void)request;
    return false;
}

void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize) {
    (void)itf;
    (void)buffer;
    (void)bufsize;
}

void tud_vendor_tx_cb(uint8_t itf, uint32_t sent_bytes) {
    (void)itf;
    (void)sent_bytes;
}


}