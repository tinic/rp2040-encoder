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
    static constexpr uint kRefBasePin = 26;  // GP26=X, GP27=Y, GP28=Z, GP29=A

    // After a reference edge the input of that encoder is not counted again until its count is
    // kRefRearmCounts from the count at the edge AND the input has been inactive, without any
    // edge, for kRefQuietUs. A reference pulse has slow, noisy flanks: an axis that moves slowly
    // over the mark, or stops on its flank, would otherwise deliver many edges (about 100 in one
    // pass were seen on a 5 um glass scale). On that scale the distance rule alone already gave
    // one count per crossing; the quiet time is there for a flank that is longer or noisier.
    // Both values are far below the distance and the time between two marks or index pulses.
    static constexpr int32_t kRefRearmCounts = 32;
    static constexpr uint32_t kRefQuietUs = 2000;

    static constexpr uint8_t kAllAxesMask = (1U << kNumEncoders) - 1U;
    static constexpr uint8_t kDefaultRefEnableMask = kAllAxesMask;
    static constexpr uint8_t kDefaultRefInvertMask = 0x00U;

    struct Snapshot {
        std::array<int32_t, kNumEncoders> counts{};
        std::array<int32_t, kNumEncoders> ref_counts{};
        std::array<uint32_t, kNumEncoders> ref_pulse_counts{};
        uint8_t ref_enable_mask = 0;
        uint8_t ref_invert_mask = 0;
        uint8_t ref_live_mask = 0;
        uint8_t index_armed_mask = 0;
    };

    static QuadratureEncoder& instance();

    void init();

    void get_all_counts(std::array<int32_t, kNumEncoders>& counts) const;
    void get_snapshot(Snapshot& out) const;

    void reset_count(size_t encoder_idx);

    void set_ref_config(uint8_t enable_mask, uint8_t invert_mask);
    [[nodiscard]] uint8_t get_ref_enable_mask() const {
        return ref_enable_mask;
    }
    [[nodiscard]] uint8_t get_ref_invert_mask() const {
        return ref_invert_mask;
    }

    void arm_index(size_t encoder_idx, bool arm);

    // Main loop, every pass: lets a reference input count again once its axis has left the
    // mark (see kRefRearmCounts). Cheap while no input is held off.
    void service_ref_inputs();

 private:
    QuadratureEncoder() = default;

    bool initialized = false;

    PIO pio = pio0;
    std::array<uint, kNumEncoders> sm_nums = {};

    volatile int32_t positions[kNumEncoders] = {};
    volatile int32_t count_offsets[kNumEncoders] = {};

    uint8_t ref_enable_mask = kDefaultRefEnableMask;
    uint8_t ref_invert_mask = kDefaultRefInvertMask;
    volatile uint8_t index_armed_mask = 0;
    volatile uint32_t ref_pulse_counts[kNumEncoders] = {};
    volatile int32_t ref_positions[kNumEncoders] = {};
    // Bit N: the pulse of encoder N has been counted. Until service_ref_inputs() lets it count
    // again, every edge of that input only notes its time in ref_last_edge_us[N].
    // ref_edge_counts[N] is the raw count at the counted edge.
    volatile uint8_t ref_holdoff_mask = 0;
    volatile int32_t ref_edge_counts[kNumEncoders] = {};
    volatile uint32_t ref_last_edge_us[kNumEncoders] = {};

    std::array<int, kNumEncoders> dma_data_chans = {};
    std::array<int, kNumEncoders> dma_ctrl_chans = {};
    std::array<volatile int32_t*, kNumEncoders> dma_dst_ptrs = {};

    void setup_pio();
    void setup_dma();
    void setup_ref_pins();
    void configure_ref_pins_locked();
    void rearm_ref_pin_locked(size_t encoder_idx);
    void handle_ref_irq(uint gpio, uint32_t events);
    static void gpio_irq_callback(uint gpio, uint32_t events);
};

#endif
