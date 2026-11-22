#include "persistent_config.h"

#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

namespace {

struct DataV1 {
    uint32_t magic;
    uint32_t schema_version;
    std::array<double, PersistentConfig::kNumScales> scale_factors;
    uint32_t crc32;
};

struct DataV2 {
    uint32_t magic;
    uint32_t schema_version;
    std::array<double, PersistentConfig::kNumScales> scale_factors;
    uint8_t ref_enable_mask;
    uint8_t ref_invert_mask;
    std::array<uint8_t, 2> reserved;
    uint32_t crc32;
};

constexpr uint32_t kMagic = 0x52503234U;  // "RP24"
constexpr uint32_t kSchemaVersionV1 = 1U;
constexpr uint32_t kSchemaVersionV2 = 2U;
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
constexpr size_t kPaddedSize = ((sizeof(DataV2) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;

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

bool PersistentConfig::load(Settings& settings) {
    // XIP-mapped read of the reserved flash sector.
    const auto* v2 = reinterpret_cast<const DataV2*>(XIP_BASE + kFlashOffset);
    if (v2->magic != kMagic) {
        return false;
    }
    if (v2->schema_version == kSchemaVersionV2) {
        if (compute_crc32(v2, offsetof(DataV2, crc32)) != v2->crc32) {
            return false;
        }
        settings.scale_factors = v2->scale_factors;
        settings.ref_enable_mask = v2->ref_enable_mask;
        settings.ref_invert_mask = v2->ref_invert_mask;
        return true;
    }
    if (v2->schema_version == kSchemaVersionV1) {
        const auto* v1 = reinterpret_cast<const DataV1*>(XIP_BASE + kFlashOffset);
        if (compute_crc32(v1, offsetof(DataV1, crc32)) != v1->crc32) {
            return false;
        }
        settings.scale_factors = v1->scale_factors;
        settings.ref_enable_mask = 0x0FU;
        settings.ref_invert_mask = 0x00U;
        return true;
    }
    return false;
}

bool PersistentConfig::save(const Settings& settings) {
    alignas(FLASH_PAGE_SIZE) static std::array<uint8_t, kPaddedSize> buf{};
    buf.fill(0xFF);  // erased state for the trailing pad

    DataV2 d{};
    d.magic = kMagic;
    d.schema_version = kSchemaVersionV2;
    d.scale_factors = settings.scale_factors;
    d.ref_enable_mask = settings.ref_enable_mask;
    d.ref_invert_mask = settings.ref_invert_mask;
    d.reserved = {};
    d.crc32 = compute_crc32(&d, offsetof(DataV2, crc32));
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
