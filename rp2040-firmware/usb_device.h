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
    static constexpr uint8_t VENDOR_REQUEST_GET_VERSION = 0x06;
    static constexpr uint8_t VENDOR_REQUEST_BOOTSEL = 0x07;
    static constexpr uint8_t VENDOR_REQUEST_RESET_CONFIG = 0x08;
    static constexpr uint8_t VENDOR_REQUEST_SET_REF_CONFIG = 0x09;
    static constexpr uint8_t VENDOR_REQUEST_GET_REF_POSITION = 0x0A;
    static constexpr uint8_t VENDOR_REQUEST_ARM_INDEX = 0x0B;

    // Every response is this long. It must stay below the endpoint's 64-byte packet size: a
    // response that fills a packet exactly is followed by a zero-length packet to end the
    // transfer, and a host that reads 64 bytes leaves that empty packet queued, so that its
    // next read returns nothing and every answer is taken one request late. 56 bytes is the
    // longest payload (GET_POSITION) and goes out as one short packet.
    static constexpr size_t kResponseSize = 56;

    static constexpr uint32_t POSITION_DATA_SENTINEL = 0x3F8A7C91;
    static constexpr uint32_t SCALE_DATA_SENTINEL = 0x7B2D4E8F;
    static constexpr uint32_t VERSION_DATA_SENTINEL = 0x5A1B9C3D;
    static constexpr uint32_t REF_POSITION_DATA_SENTINEL = 0x9C4E2A71;

    static USBDevice& instance();

    static void init();
    void task() const;
    [[nodiscard]] bool send_position_data() const;
    [[nodiscard]] bool send_scale_data() const;
    [[nodiscard]] bool send_version_data() const;
    [[nodiscard]] bool send_ref_position_data() const;

 private:
    USBDevice() = default;
    bool initialized = false;
};

#endif