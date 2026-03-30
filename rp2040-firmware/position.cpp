
#include "position.h"
#include "quadrature_encoder.h"
#include "usb_device.h"
#include "pico/time.h"
#include <cstring>
#include <cmath>
#include <cstdlib>

Position& Position::instance() {
    static Position position;
    if (!position.initialized) {
        position.initialized = true;
        position.init();
    }
    return position;
}

void Position::init() {
    QuadratureEncoder::instance();
    initialized = true;
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
    
    bytes = sizeof(uint32_t) + sizeof(positions);
    if (out != nullptr) {
        uint32_t sentinel = USBDevice::POSITION_DATA_SENTINEL;
        memcpy(out, &sentinel, sizeof(sentinel));
        
        memcpy(out + sizeof(sentinel), reinterpret_cast<const uint8_t*>(positions.data()), sizeof(positions));
    }

    return true;
}

void Position::update_from_encoders() {
    std::array<int32_t, kPositions> counts;
    QuadratureEncoder::instance().get_all_counts(counts);
    for (size_t i = 0; i < kPositions; i++) {
        positions[i] = static_cast<double>(counts[i]) * scale_factors[i];
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
    if (!test_mode) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed = now - test_mode_start_time;
    double time_sec = elapsed * 0.001;
    
    switch (test_pattern) {
        case TestPattern::SINE_WAVE: {
            positions[0] = test_mode_base_positions[0] + 5.0 * sin(time_sec * 0.5);
            positions[1] = test_mode_base_positions[1] + 3.0 * sin(time_sec * 0.7 + 1.57);
            positions[2] = test_mode_base_positions[2] + 2.0 * sin(time_sec * 0.3);
            positions[3] = test_mode_base_positions[3] + 45.0 * sin(time_sec * 0.2);
            break;
        }
        
        case TestPattern::CIRCULAR: {
            double radius = 10.0;
            double angular_vel = 0.3;
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
            static uint32_t seed = 0x12345678;
            
            if (now - last_update >= 50) {
                auto next_random = [](uint32_t& s) -> double {
                    s = (s * 1664525u + 1013904223u);
                    return ((s >> 16) & 0x7FFF) / 32768.0 - 0.5;
                };
                
                positions[0] += next_random(seed) * 0.02;
                positions[1] += next_random(seed) * 0.02;
                positions[2] += next_random(seed) * 0.01;
                positions[3] += next_random(seed) * 0.1;
                last_update = now;
            }
            break;
        }
    }
}
