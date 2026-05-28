#ifndef QUADRATURE_ENCODER_H_
#define QUADRATURE_ENCODER_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "hardware/pio.h"

class QuadratureEncoder {
 public:
    static constexpr size_t kNumEncoders = 4;

    static constexpr uint kBasePin = 0;
    static constexpr uint kPinsPerEncoder = 2;

    static QuadratureEncoder& instance();

    void init();

    void get_count(size_t encoder_idx, int32_t& count);
    void get_all_counts(std::array<int32_t, kNumEncoders>& counts);

    void reset_count(size_t encoder_idx);
    void set_count(size_t encoder_idx, int32_t new_count);

 private:
    QuadratureEncoder() = default;

    bool initialized = false;

    PIO pio = pio0;
    std::array<uint, kNumEncoders> sm_nums = {};

    std::array<int32_t, kNumEncoders> positions = {};
    std::array<int32_t, kNumEncoders> count_offsets = {};

    int32_t drain_latest(size_t encoder_idx);

    void setup_pio();
};

#endif
