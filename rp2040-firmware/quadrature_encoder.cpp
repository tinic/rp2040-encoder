#include "quadrature_encoder.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "quadrature_encoder.pio.h"

std::array<int32_t, QuadratureEncoder::kNumEncoders> QuadratureEncoder::positions = {};
PIO QuadratureEncoder::static_pio = nullptr;
std::array<uint, QuadratureEncoder::kNumEncoders> QuadratureEncoder::static_sm_nums = {};

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
    
    static_pio = pio;
    static_sm_nums = sm_nums;
    
    setup_interrupts();

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

void QuadratureEncoder::setup_interrupts() {
    pio_set_irqn_source_enabled(pio, 0, pis_sm0_rx_fifo_not_empty, true);
    pio_set_irqn_source_enabled(pio, 0, pis_sm1_rx_fifo_not_empty, true);
    pio_set_irqn_source_enabled(pio, 0, pis_sm2_rx_fifo_not_empty, true);
    pio_set_irqn_source_enabled(pio, 0, pis_sm3_rx_fifo_not_empty, true);
    
    irq_set_exclusive_handler(PIO0_IRQ_0, pio_irq_handler);
    irq_set_priority(PIO0_IRQ_0, 0);
    irq_set_enabled(PIO0_IRQ_0, true);
}

void QuadratureEncoder::pio_irq_handler() {
    if (!static_pio) return;
    
    for (size_t i = 0; i < kNumEncoders; i++) {
        while (!pio_sm_is_rx_fifo_empty(static_pio, static_sm_nums[i])) {
            positions[i] = (int32_t)static_pio->rxf[static_sm_nums[i]];
        }
    }
    
    pio_interrupt_clear(static_pio, 0);
}


void QuadratureEncoder::get_all_counts(std::array<int32_t, kNumEncoders>& counts) const {
    for (size_t i = 0; i < kNumEncoders; i++) {
        counts[i] = positions[i] - count_offsets[i];
    }
}

void QuadratureEncoder::get_count(size_t encoder_idx, int32_t& count) const {
    if (encoder_idx >= kNumEncoders) return;
    count = positions[encoder_idx] - count_offsets[encoder_idx];
}

void QuadratureEncoder::reset_count(size_t encoder_idx) {
    if (encoder_idx >= kNumEncoders) return;
    count_offsets[encoder_idx] = positions[encoder_idx];
}

void QuadratureEncoder::set_count(size_t encoder_idx, int32_t new_count) {
    if (encoder_idx >= kNumEncoders) return;
    count_offsets[encoder_idx] = positions[encoder_idx] - new_count;
}