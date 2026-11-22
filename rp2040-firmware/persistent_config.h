#ifndef PERSISTENT_CONFIG_H_
#define PERSISTENT_CONFIG_H_

#include <array>
#include <cstddef>
#include <cstdint>

// Single-sector flash-backed configuration store living in the last 4 KiB
// of the RP2040's flash. Holds the per-encoder scale factors so they
// survive power cycles without LinuxCNC having to re-send them on every
// HAL load. Schema is versioned and CRC32-checked; a corrupt or
// uninitialised sector reads as "no valid blob" and the caller falls
// back to compile-time defaults.
class PersistentConfig {
 public:
    static constexpr size_t kNumScales = 4;

    struct Settings {
        std::array<double, kNumScales> scale_factors{};
        uint8_t ref_enable_mask = 0x0FU;
        uint8_t ref_invert_mask = 0x00U;
    };

    // Returns true and populates `settings` if a valid blob was loaded;
    // returns false otherwise (first boot, corruption, unknown schema).
    // Schema v1 blobs (scales only) are transparently upgraded with default
    // reference-pulse settings. On false, `settings` is left untouched.
    [[nodiscard]] static bool load(Settings& settings);

    // Atomically writes `settings` to flash. Takes ~50 ms (sector erase +
    // page program) and disables interrupts during the flash op; USB
    // packets may be queued briefly but DMA encoder draining continues
    // on the AHB bus and counts cannot be lost.
    static bool save(const Settings& settings);

    // Erases the flash sector. Subsequent load() returns false.
    static void erase();
};

#endif
