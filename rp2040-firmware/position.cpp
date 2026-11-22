#include "position.h"

#include <cmath>
#include <cstring>

#include "persistent_config.h"
#include "pico/time.h"
#include "quadrature_encoder.h"
#include "usb_device.h"

Position& Position::instance() {
    static Position position;
    if (!position.initialized) {
        position.initialized = true;
        position.init();
    }
    return position;
}

void Position::init() {
    QuadratureEncoder& enc = QuadratureEncoder::instance();
    // If the flash sector holds a valid blob, those values override the
    // compile-time defaults in main.cpp; main.cpp reads should_apply_defaults()
    // and skips its set_scale() calls when load() succeeded. Without that gate
    // the dedup in set_scale() only no-ops when the persisted value happens
    // to equal the hardcoded default — every other USB-set scale would be
    // silently overwritten on the next boot.
    PersistentConfig::Settings settings{};
    config_loaded = PersistentConfig::load(settings);
    if (config_loaded) {
        scale_factors = settings.scale_factors;
        ref_enable_mask = static_cast<uint8_t>(settings.ref_enable_mask & QuadratureEncoder::kAllAxesMask);
        ref_invert_mask = static_cast<uint8_t>(settings.ref_invert_mask & QuadratureEncoder::kAllAxesMask);
        enc.set_ref_config(ref_enable_mask, ref_invert_mask);
    }
}

void Position::set_scale(size_t pos, double scale) {
    if (pos >= kPositions) {
        return;
    }
    if (scale_factors[pos] == scale) {
        return;  // dedup: no-op writes do not arm the auto-save
    }
    scale_factors[pos] = scale;
    config_dirty = true;
    config_dirty_since_ms = to_ms_since_boot(get_absolute_time());
}

void Position::set_ref_config(uint8_t enable_mask, uint8_t invert_mask) {
    const auto masked_enable = static_cast<uint8_t>(enable_mask & QuadratureEncoder::kAllAxesMask);
    const auto masked_invert = static_cast<uint8_t>(invert_mask & QuadratureEncoder::kAllAxesMask);
    if (ref_enable_mask == masked_enable && ref_invert_mask == masked_invert) {
        return;  // dedup: no-op writes do not arm the auto-save
    }
    ref_enable_mask = masked_enable;
    ref_invert_mask = masked_invert;
    QuadratureEncoder::instance().set_ref_config(ref_enable_mask, ref_invert_mask);
    config_dirty = true;
    config_dirty_since_ms = to_ms_since_boot(get_absolute_time());
}

void Position::arm_index(size_t pos, bool arm) const {
    if (!initialized || pos >= kPositions) {
        return;
    }
    QuadratureEncoder::instance().arm_index(pos, arm);
}

void Position::tick() {
    if (!config_dirty) {
        return;
    }
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - config_dirty_since_ms < kAutoSaveDebounceMs) {
        return;
    }
    PersistentConfig::Settings settings{};
    settings.scale_factors = scale_factors;
    settings.ref_enable_mask = ref_enable_mask;
    settings.ref_invert_mask = ref_invert_mask;
    PersistentConfig::save(settings);
    config_dirty = false;
}

void Position::reset_persistent_config() {
    PersistentConfig::erase();
    config_dirty = false;
}

bool Position::get(uint8_t* out, size_t& bytes) {
    if (!initialized) {
        return false;
    }

    if (test_mode) {
        update_test_mode();
    } else {
        update_from_encoders();
    }

    // Layout (56 bytes within the 64-byte USB packet):
    //   [0..3]   uint32_t sentinel (POSITION_DATA_SENTINEL)
    //   [4..35]  double   positions[4]
    //   [36]     uint8_t  ref_enable_mask
    //   [37]     uint8_t  ref_invert_mask
    //   [38]     uint8_t  ref_live_mask
    //   [39]     uint8_t  index_armed_mask
    //   [40..55] uint32_t ref_pulse_counts[4]
    bytes = sizeof(uint32_t) + sizeof(positions) + 4U + sizeof(ref_pulse_counts);
    if (out != nullptr) {
        uint32_t sentinel = USBDevice::POSITION_DATA_SENTINEL;
        memcpy(out, &sentinel, sizeof(sentinel));
        memcpy(out + sizeof(sentinel), reinterpret_cast<const uint8_t*>(positions.data()), sizeof(positions));

        const size_t meta_offset = sizeof(sentinel) + sizeof(positions);
        out[meta_offset + 0U] = ref_enable_mask;
        out[meta_offset + 1U] = ref_invert_mask;
        out[meta_offset + 2U] = ref_live_mask;
        out[meta_offset + 3U] = index_armed_mask;
        memcpy(out + meta_offset + 4U, reinterpret_cast<const uint8_t*>(ref_pulse_counts.data()),
               sizeof(ref_pulse_counts));
    }

    return true;
}

bool Position::get_ref_positions(uint8_t* out, size_t& bytes) {
    if (!initialized) {
        return false;
    }

    if (!test_mode) {
        update_from_encoders();
    }

    // Layout (52 bytes within the 64-byte USB packet):
    //   [0..3]   uint32_t sentinel (REF_POSITION_DATA_SENTINEL)
    //   [4..35]  double   ref_positions[4]
    //   [36..51] uint32_t ref_pulse_counts[4]
    bytes = sizeof(uint32_t) + sizeof(ref_positions) + sizeof(ref_pulse_counts);
    if (out != nullptr) {
        uint32_t sentinel = USBDevice::REF_POSITION_DATA_SENTINEL;
        memcpy(out, &sentinel, sizeof(sentinel));
        memcpy(out + sizeof(sentinel), reinterpret_cast<const uint8_t*>(ref_positions.data()), sizeof(ref_positions));
        memcpy(out + sizeof(sentinel) + sizeof(ref_positions),
               reinterpret_cast<const uint8_t*>(ref_pulse_counts.data()), sizeof(ref_pulse_counts));
    }

    return true;
}

void Position::update_from_encoders() {
    QuadratureEncoder::Snapshot snap{};
    QuadratureEncoder::instance().get_snapshot(snap);
    ref_enable_mask = snap.ref_enable_mask;
    ref_invert_mask = snap.ref_invert_mask;
    ref_live_mask = snap.ref_live_mask;
    index_armed_mask = snap.index_armed_mask;
    ref_pulse_counts = snap.ref_pulse_counts;
    for (size_t i = 0; i < kPositions; i++) {
        positions[i] = static_cast<double>(snap.counts[i]) * scale_factors[i];
        ref_positions[i] = static_cast<double>(snap.ref_counts[i]) * scale_factors[i];
    }
}

bool Position::reset_encoder(size_t pos) {
    if (!initialized) {
        return false;
    }
    if (pos >= kPositions) {
        return false;
    }

    QuadratureEncoder::instance().reset_count(pos);
    positions[pos] = 0.0;
    return true;
}

void Position::enable_test_mode(bool enable) {
    if (enable && !test_mode) {
        test_mode = true;
        test_mode_start_time = to_ms_since_boot(get_absolute_time());
        test_mode_base_positions = positions;
    } else if (!enable && test_mode) {
        test_mode = false;
    }
}

void Position::set_test_pattern(uint8_t pattern) {
    if (pattern < static_cast<uint8_t>(TestPattern::COUNT)) {
        test_pattern = static_cast<TestPattern>(pattern);
        if (test_mode) {
            test_mode_start_time = to_ms_since_boot(get_absolute_time());
            test_mode_base_positions = positions;
        }
    }
}

void Position::update_test_mode() {
    if (!test_mode) {
        return;
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const uint32_t elapsed = now - test_mode_start_time;
    const double time_sec = elapsed * 0.001;

    switch (test_pattern) {
        case TestPattern::SINE_WAVE: {
            positions[0] = test_mode_base_positions[0] + 5.0 * sin(time_sec * 0.5);
            positions[1] = test_mode_base_positions[1] + 3.0 * sin(time_sec * 0.7 + 1.57);
            positions[2] = test_mode_base_positions[2] + 2.0 * sin(time_sec * 0.3);
            positions[3] = test_mode_base_positions[3] + 45.0 * sin(time_sec * 0.2);
            break;
        }

        case TestPattern::CIRCULAR: {
            const double radius = 10.0;
            const double angular_vel = 0.3;
            positions[0] = test_mode_base_positions[0] + radius * cos(time_sec * angular_vel);
            positions[1] = test_mode_base_positions[1] + radius * sin(time_sec * angular_vel);
            positions[2] = test_mode_base_positions[2] + 1.0 * sin(time_sec * 0.1);
            positions[3] = test_mode_base_positions[3] + time_sec * 5.0;
            break;
        }

        case TestPattern::LINEAR_RAMP: {
            positions[0] = test_mode_base_positions[0] + time_sec * 2.0;
            positions[1] = test_mode_base_positions[1] + time_sec * 1.5;
            positions[2] = test_mode_base_positions[2] + time_sec * 0.5;
            positions[3] = test_mode_base_positions[3] + time_sec * 10.0;
            break;
        }

        case TestPattern::RANDOM_WALK: {
            static uint32_t last_update = 0;

            if (now - last_update >= 50) {
                static uint32_t seed = 0x12345678;
                auto next_random = [](uint32_t& s) -> double {
                    s = (s * 1664525U + 1013904223U);
                    return (static_cast<double>((s >> 16) & 0x7FFF) / 32768.0) - 0.5;
                };

                positions[0] += next_random(seed) * 0.02;
                positions[1] += next_random(seed) * 0.02;
                positions[2] += next_random(seed) * 0.01;
                positions[3] += next_random(seed) * 0.1;
                last_update = now;
            }
            break;
        }

        case TestPattern::COUNT:
            break;
    }
}
