#include "quadrature_encoder.h"

#include <algorithm>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/structs/dma.h"
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
    count_offsets.fill(0);
    std::fill_n(std::begin(positions), kNumEncoders, 0);

    setup_pio();

    for (size_t i = 0; i < kNumEncoders; i++) {
        pio_sm_clear_fifos(pio, sm_nums[i]);
    }

    setup_dma();
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

void QuadratureEncoder::get_all_counts(std::array<int32_t, kNumEncoders>& counts) const {
    for (size_t i = 0; i < kNumEncoders; i++) {
        counts[i] = positions[i] - count_offsets[i];
    }
}

void QuadratureEncoder::reset_count(size_t encoder_idx) {
    if (encoder_idx >= kNumEncoders) {
        return;
    }
    count_offsets[encoder_idx] = positions[encoder_idx];
}
