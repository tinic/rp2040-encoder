#include "quadrature_encoder.h"

#include <algorithm>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/dma.h"
#include "hardware/sync.h"
#include "quadrature_encoder.pio.h"

namespace {
QuadratureEncoder* active_encoder_instance = nullptr;
}  // namespace

QuadratureEncoder& QuadratureEncoder::instance() {
    static QuadratureEncoder encoder;
    if (!encoder.initialized) {
        active_encoder_instance = &encoder;
        encoder.init();
        encoder.initialized = true;
    }
    return encoder;
}

void QuadratureEncoder::init() {
    std::fill_n(std::begin(count_offsets), kNumEncoders, 0);
    std::fill_n(std::begin(positions), kNumEncoders, 0);
    std::fill_n(std::begin(ref_pulse_counts), kNumEncoders, 0U);
    std::fill_n(std::begin(ref_positions), kNumEncoders, 0);
    index_armed_mask = 0U;

    setup_pio();

    for (size_t i = 0; i < kNumEncoders; i++) {
        pio_sm_clear_fifos(pio, sm_nums[i]);
    }

    setup_dma();
    setup_ref_pins();
}

void QuadratureEncoder::setup_pio() {
    const auto offset = pio_add_program(pio, &quadrature_encoder_program);

    for (size_t i = 0; i < kNumEncoders; i++) {
        const auto sm = pio_claim_unused_sm(pio, true);
        sm_nums[i] = sm;

        const auto pin_base = kBasePin + (i * kPinsPerEncoder);
        quadrature_encoder_program_init(pio, sm, offset, pin_base, 0);
    }
}

void QuadratureEncoder::setup_dma() {
    for (size_t i = 0; i < kNumEncoders; i++) {
        dma_data_chans[i] = dma_claim_unused_channel(true);
        dma_ctrl_chans[i] = dma_claim_unused_channel(true);
        dma_dst_ptrs[i] = &positions[i];

        // Data channel: PIO RX FIFO -> positions[i], paced by DREQ.
        // One transfer per arm; on completion, chain triggers the control channel
        // which re-arms us by writing back to AL2_WRITE_ADDR_TRIG. TRANS_COUNT is
        // restored from the shadow register on each trigger, giving endless drain.
        dma_channel_config data_cfg = dma_channel_get_default_config(dma_data_chans[i]);
        channel_config_set_transfer_data_size(&data_cfg, DMA_SIZE_32);
        channel_config_set_read_increment(&data_cfg, false);
        channel_config_set_write_increment(&data_cfg, false);
        channel_config_set_dreq(&data_cfg, pio_get_dreq(pio, sm_nums[i], false));
        channel_config_set_chain_to(&data_cfg, dma_ctrl_chans[i]);

        // Hand the DMA controller a non-volatile pointer to a volatile slot. The
        // volatile is for the CPU's view (DMA writes asynchronously); the DMA
        // engine sees memory directly and is unaffected by the C++ qualifier.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        dma_channel_configure(dma_data_chans[i], &data_cfg, const_cast<int32_t*>(&positions[i]), &pio->rxf[sm_nums[i]],
                              1, true);

        // Control channel: one transfer that writes &positions[i] into the data
        // channel's AL2_WRITE_ADDR_TRIG, which restores write_addr (no-op since
        // increment=false) and re-triggers the data channel.
        dma_channel_config ctrl_cfg = dma_channel_get_default_config(dma_ctrl_chans[i]);
        channel_config_set_transfer_data_size(&ctrl_cfg, DMA_SIZE_32);
        channel_config_set_read_increment(&ctrl_cfg, false);
        channel_config_set_write_increment(&ctrl_cfg, false);

        dma_channel_configure(dma_ctrl_chans[i], &ctrl_cfg, &dma_hw->ch[dma_data_chans[i]].al2_write_addr_trig,
                              reinterpret_cast<const volatile void*>(&dma_dst_ptrs[i]), 1, false);
    }
}

void QuadratureEncoder::setup_ref_pins() {
    const uint32_t irq = save_and_disable_interrupts();
    for (size_t i = 0; i < kNumEncoders; i++) {
        const uint pin = kRefBasePin + static_cast<uint>(i);
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
    }
    gpio_set_irq_callback(&QuadratureEncoder::gpio_irq_callback);
    configure_ref_pins_locked();
    irq_set_enabled(IO_IRQ_BANK0, true);
    restore_interrupts(irq);
}

void QuadratureEncoder::configure_ref_pins_locked() {
    for (size_t i = 0; i < kNumEncoders; i++) {
        const uint pin = kRefBasePin + static_cast<uint>(i);
        const auto bit = static_cast<uint8_t>(1U << i);
        const bool enabled = (ref_enable_mask & bit) != 0U;
        const bool inverted = (ref_invert_mask & bit) != 0U;

        // Disable both edges first so changing pulls cannot trigger a stale edge.
        gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);

        // Bias unwired pins to their inactive level (pull-down for active-HIGH,
        // pull-up for active-LOW) so floating GP26-GP29 inputs never generate
        // spurious interrupts. The RP2040's ~50 kOhm internal pull is easily
        // overridden by a push-pull isolator output (~20 Ohm).
        if (enabled && inverted) {
            gpio_pull_up(pin);
        } else {
            gpio_pull_down(pin);
        }

        gpio_acknowledge_irq(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);
        if (enabled) {
            const uint32_t edge = inverted ? GPIO_IRQ_EDGE_FALL : GPIO_IRQ_EDGE_RISE;
            gpio_set_irq_enabled(pin, edge, true);
        } else {
            index_armed_mask = static_cast<uint8_t>(index_armed_mask & static_cast<uint8_t>(~bit));
        }
    }
}

void QuadratureEncoder::gpio_irq_callback(uint gpio, uint32_t events) {
    if (active_encoder_instance != nullptr) {
        active_encoder_instance->handle_ref_irq(gpio, events);
    }
}

void QuadratureEncoder::handle_ref_irq(uint gpio, uint32_t events) {
    if (gpio < kRefBasePin || gpio >= kRefBasePin + kNumEncoders) {
        return;
    }
    const size_t idx = gpio - kRefBasePin;
    const auto bit = static_cast<uint8_t>(1U << idx);
    if ((ref_enable_mask & bit) == 0U) {
        return;
    }
    const uint32_t expected_edge = ((ref_invert_mask & bit) != 0U) ? GPIO_IRQ_EDGE_FALL : GPIO_IRQ_EDGE_RISE;
    if ((events & expected_edge) == 0U) {
        return;
    }

    const int32_t raw = positions[idx];
    const uint32_t rel = static_cast<uint32_t>(raw) - static_cast<uint32_t>(count_offsets[idx]);
    ref_positions[idx] = static_cast<int32_t>(rel);
    ref_pulse_counts[idx] = ref_pulse_counts[idx] + 1U;

    if ((index_armed_mask & bit) != 0U) {
        count_offsets[idx] = raw;
        index_armed_mask = static_cast<uint8_t>(index_armed_mask & static_cast<uint8_t>(~bit));
    }
}

void QuadratureEncoder::set_ref_config(uint8_t enable_mask, uint8_t invert_mask) {
    const uint8_t masked_enable = static_cast<uint8_t>(enable_mask & kAllAxesMask);
    const uint8_t masked_invert = static_cast<uint8_t>(invert_mask & kAllAxesMask);
    const uint32_t irq = save_and_disable_interrupts();
    ref_enable_mask = masked_enable;
    ref_invert_mask = masked_invert;
    configure_ref_pins_locked();
    restore_interrupts(irq);
}

void QuadratureEncoder::arm_index(size_t encoder_idx, bool arm) {
    if (encoder_idx >= kNumEncoders) {
        return;
    }
    const auto bit = static_cast<uint8_t>(1U << encoder_idx);
    const uint32_t irq = save_and_disable_interrupts();
    if (arm && ((ref_enable_mask & bit) != 0U)) {
        index_armed_mask = static_cast<uint8_t>(index_armed_mask | bit);
    } else {
        index_armed_mask = static_cast<uint8_t>(index_armed_mask & static_cast<uint8_t>(~bit));
    }
    restore_interrupts(irq);
}

void QuadratureEncoder::get_all_counts(std::array<int32_t, kNumEncoders>& counts) const {
    const uint32_t irq = save_and_disable_interrupts();
    for (size_t i = 0; i < kNumEncoders; i++) {
        // Subtract in unsigned space: two's-complement wrap is the intended
        // behavior when the free-running PIO counter rolls past INT32_MAX, but
        // signed overflow is UB in C++. The unsigned result has identical bits.
        const auto delta = static_cast<uint32_t>(positions[i]) - static_cast<uint32_t>(count_offsets[i]);
        counts[i] = static_cast<int32_t>(delta);
    }
    restore_interrupts(irq);
}

void QuadratureEncoder::get_snapshot(Snapshot& out) const {
    const uint32_t irq = save_and_disable_interrupts();
    out.ref_enable_mask = ref_enable_mask;
    out.ref_invert_mask = ref_invert_mask;
    out.index_armed_mask = index_armed_mask;
    uint8_t live_mask = 0U;
    for (size_t i = 0; i < kNumEncoders; i++) {
        const auto delta = static_cast<uint32_t>(positions[i]) - static_cast<uint32_t>(count_offsets[i]);
        out.counts[i] = static_cast<int32_t>(delta);
        out.ref_counts[i] = ref_positions[i];
        out.ref_pulse_counts[i] = ref_pulse_counts[i];
        const bool pin_high = gpio_get(kRefBasePin + static_cast<uint>(i));
        const bool inverted = ((ref_invert_mask >> i) & 1U) != 0U;
        if (pin_high != inverted) {
            live_mask = static_cast<uint8_t>(live_mask | static_cast<uint8_t>(1U << i));
        }
    }
    out.ref_live_mask = live_mask;
    restore_interrupts(irq);
}

void QuadratureEncoder::reset_count(size_t encoder_idx) {
    if (encoder_idx >= kNumEncoders) {
        return;
    }
    const uint32_t irq = save_and_disable_interrupts();
    count_offsets[encoder_idx] = positions[encoder_idx];
    restore_interrupts(irq);
}
