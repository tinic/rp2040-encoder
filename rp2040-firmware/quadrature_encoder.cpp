#include "quadrature_encoder.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "quadrature_encoder.pio.h"

QuadratureEncoder& QuadratureEncoder::instance() {
    static QuadratureEncoder encoder;
    if (!encoder.initialized) {
        encoder.init();
        encoder.initialized = true;
    }
    return encoder;
}

void QuadratureEncoder::init() {
    setup_pio();

    count_offsets.fill(0);
    positions.fill(0);

    for (size_t i = 0; i < kNumEncoders; i++) {
        pio_sm_clear_fifos(pio, sm_nums[i]);
    }
}

void QuadratureEncoder::setup_pio() {
    uint offset = pio_add_program(pio, &quadrature_encoder_program);

    for (size_t i = 0; i < kNumEncoders; i++) {
        uint sm = pio_claim_unused_sm(pio, true);
        sm_nums[i] = sm;

        uint pin_base = kBasePin + (i * kPinsPerEncoder);
        quadrature_encoder_program_init(pio, sm, offset, pin_base, 0);
    }
}

int32_t QuadratureEncoder::drain_latest(size_t encoder_idx) {
    int32_t v = positions[encoder_idx];
    while (!pio_sm_is_rx_fifo_empty(pio, sm_nums[encoder_idx])) {
        v = (int32_t)pio->rxf[sm_nums[encoder_idx]];
    }
    positions[encoder_idx] = v;
    return v;
}

void QuadratureEncoder::get_all_counts(std::array<int32_t, kNumEncoders>& counts) {
    for (size_t i = 0; i < kNumEncoders; i++) {
        counts[i] = drain_latest(i) - count_offsets[i];
    }
}

void QuadratureEncoder::get_count(size_t encoder_idx, int32_t& count) {
    if (encoder_idx >= kNumEncoders) return;
    count = drain_latest(encoder_idx) - count_offsets[encoder_idx];
}

void QuadratureEncoder::reset_count(size_t encoder_idx) {
    if (encoder_idx >= kNumEncoders) return;
    count_offsets[encoder_idx] = drain_latest(encoder_idx);
}

void QuadratureEncoder::set_count(size_t encoder_idx, int32_t new_count) {
    if (encoder_idx >= kNumEncoders) return;
    count_offsets[encoder_idx] = drain_latest(encoder_idx) - new_count;
}
