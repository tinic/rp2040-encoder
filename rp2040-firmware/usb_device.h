#ifndef USB_DEVICE_H_
#define USB_DEVICE_H_

#include <cstddef>
#include <cstdint>

#include "tusb.h"

class USBDevice {
 public:
    static constexpr uint8_t VENDOR_INTERFACE = 0;
    static constexpr uint8_t EP_VENDOR_IN = 0x81;
    static constexpr uint8_t EP_VENDOR_OUT = 0x01;

    static constexpr uint16_t VENDOR_ID = 0x2E8A;
    static constexpr uint16_t PRODUCT_ID = 0xC0DE;

    static constexpr uint8_t VENDOR_REQUEST_GET_POSITION = 0x01;
    static constexpr uint8_t VENDOR_REQUEST_SET_TEST_MODE = 0x02;
    static constexpr uint8_t VENDOR_REQUEST_SET_SCALE = 0x03;
    static constexpr uint8_t VENDOR_REQUEST_GET_SCALE = 0x04;
    static constexpr uint8_t VENDOR_REQUEST_RESET_POSITION = 0x05;
    
    static constexpr uint32_t POSITION_DATA_SENTINEL = 0x3F8A7C91;
    static constexpr uint32_t SCALE_DATA_SENTINEL = 0x7B2D4E8F;
    
    static USBDevice& instance();

    void init();
    void task();
    [[nodiscard]] bool send_position_data();
    [[nodiscard]] bool send_scale_data();

 private:
    USBDevice() = default;
    bool initialized = false;
};

#endif