#include "persistent_config.h"

#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

namespace {

struct Data {
    uint32_t magic;
    uint32_t schema_version;
    std::array<double, PersistentConfig::kNumScales> scale_factors;
    uint32_t crc32;
};

constexpr uint32_t kMagic = 0x52503234U;  // "RP24"
constexpr uint32_t kSchemaVersion = 1U;
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
constexpr size_t kPaddedSize = ((sizeof(Data) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;

uint32_t compute_crc32(const void* buf, size_t len) {
    const auto* p = static_cast<const uint8_t*>(buf);
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int j = 0; j < 8; ++j) {
            const uint32_t mask = -(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

}  // namespace

bool PersistentConfig::load(std::array<double, kNumScales>& scales) {
    // XIP-mapped read of the reserved flash sector.
    const auto* d = reinterpret_cast<const Data*>(XIP_BASE + kFlashOffset);
    if (d->magic != kMagic) {
        return false;
    }
    if (d->schema_version != kSchemaVersion) {
        return false;
    }
    if (compute_crc32(d, offsetof(Data, crc32)) != d->crc32) {
        return false;
    }
    scales = d->scale_factors;
    return true;
}

bool PersistentConfig::save(const std::array<double, kNumScales>& scales) {
    alignas(FLASH_PAGE_SIZE) static std::array<uint8_t, kPaddedSize> buf{};
    buf.fill(0xFF);  // erased state for the trailing pad

    Data d{};
    d.magic = kMagic;
    d.schema_version = kSchemaVersion;
    d.scale_factors = scales;
    d.crc32 = compute_crc32(&d, offsetof(Data, crc32));
    memcpy(buf.data(), &d, sizeof(d));

    const uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kFlashOffset, buf.data(), buf.size());
    restore_interrupts(irq);
    return true;
}

void PersistentConfig::erase() {
    const uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
}
