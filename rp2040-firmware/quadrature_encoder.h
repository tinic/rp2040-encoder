#ifndef QUADRATURE_ENCODER_H_
#define QUADRATURE_ENCODER_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "hardware/pio.h"
#include "pico/time.h"
#include "hardware/gpio.h"

class QuadratureEncoder {
 public:
    static constexpr size_t kNumEncoders = 4;

    static constexpr uint kBasePin = 0;
    static constexpr uint kPinsPerEncoder = 2;

    static QuadratureEncoder& instance();

    void init();

    void get_count(size_t encoder_idx, int32_t& count) const;

    void get_all_counts(std::array<int32_t, kNumEncoders>& counts) const;

    void reset_count(size_t encoder_idx);
    
    void set_count(size_t encoder_idx, int32_t new_count);

    void set_max_step_rate(int max_rate) {
        max_step_rate = max_rate;
    }


    static void pio_irq_handler();

 private:
    QuadratureEncoder() = default;
    bool initialized = false;

    PIO pio = pio0;
    std::array<uint, kNumEncoders> sm_nums = {};

    std::array<int32_t, kNumEncoders> count_offsets = {};
    
    static std::array<int32_t, kNumEncoders> positions;
    static PIO static_pio;
    static std::array<uint, kNumEncoders> static_sm_nums;

    uint32_t last_fifo_drain = 0;
    static constexpr uint32_t kFifoDrainInterval = 1;

    int max_step_rate = 0;
    repeating_timer_t timer;

    void setup_pio();
    void setup_interrupts();
};

#endif