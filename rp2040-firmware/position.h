#ifndef POSITION_H_
#define POSITION_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "quadrature_encoder.h"

class Position {
 public:
    static constexpr size_t kPositions = QuadratureEncoder::kNumEncoders;

 private:
    static constexpr uint32_t kAutoSaveDebounceMs = 1000;

    bool initialized = false;
    void init();
    std::array<double, kPositions> positions{};
    std::array<double, kPositions> scale_factors{};

    // True iff init() pulled a valid blob from flash. Read once at boot via
    // should_apply_defaults() to decide whether main.cpp's compile-time
    // defaults should overwrite the loaded values. Not maintained after init.
    bool config_loaded = false;

    bool config_dirty = false;
    uint32_t config_dirty_since_ms = 0;

    bool test_mode = false;
    uint32_t test_mode_start_time = 0;
    std::array<double, kPositions> test_mode_base_positions{};
    enum class TestPattern {
        SINE_WAVE,
        CIRCULAR,
        LINEAR_RAMP,
        RANDOM_WALK,
        COUNT
    } test_pattern = TestPattern::SINE_WAVE;

    void update_from_encoders();

    void update_test_mode();

 public:
    static Position& instance();

    [[nodiscard]] bool get(uint8_t* out, size_t& bytes);

    void set_scale(size_t pos, double scale);

    // True at boot when init() found NO valid persisted config. main.cpp
    // gates its compile-time defaults on this so that USB-set scales are
    // not silently clobbered on the next boot. Phrased positively
    // ("should I apply defaults?") so the call site cannot be inverted by
    // accident — the wrong direction would wipe flash on every reboot.
    [[nodiscard]] bool should_apply_defaults() const {
        return !config_loaded;
    }

    [[nodiscard]] double get_scale(size_t pos) const {
        if (pos < kPositions) {
            return scale_factors[pos];
        }
        return 1.0;
    }

    [[nodiscard]] bool reset_encoder(size_t pos);

    void enable_test_mode(bool enable);
    void set_test_pattern(uint8_t pattern);
    [[nodiscard]] bool is_test_mode() const {
        return test_mode;
    }

    // Called from the main loop. If scale_factors[] has changed and the
    // debounce window has elapsed, persists the new values to flash.
    void tick();

    // Wipes the flash-backed config sector. Next boot falls back to the
    // compile-time defaults set in main.cpp.
    void reset_persistent_config();
};

#endif
