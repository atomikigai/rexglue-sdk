/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef REX_GRAPHICS_VBLANK_SWAP_PACER_H_
#define REX_GRAPHICS_VBLANK_SWAP_PACER_H_

#include <cstdint>
#include <limits>

namespace rex::graphics {

// Chooses the simulated vblank on which a swap may be presented. Waiting and
// clock access stay outside this class so the policy is deterministic and can
// be tested without a graphics API or host threads.
class VblankSwapPacer {
 public:
  uint64_t Schedule(uint64_t current_vblank, uint32_t interval_vblanks) {
    if (!has_previous_swap_) {
      has_previous_swap_ = true;
      last_scheduled_vblank_ = AddSaturated(current_vblank, 1);
      return last_scheduled_vblank_;
    }

    uint64_t desired_vblank = AddSaturated(last_scheduled_vblank_, interval_vblanks);
    // If the desired tick has already happened, wait for the next tick and
    // make it the new baseline rather than carrying missed time as debt.
    last_scheduled_vblank_ =
        current_vblank >= desired_vblank ? AddSaturated(current_vblank, 1) : desired_vblank;
    return last_scheduled_vblank_;
  }

 private:
  static uint64_t AddSaturated(uint64_t value, uint64_t addend) {
    constexpr uint64_t kMaximum = std::numeric_limits<uint64_t>::max();
    return addend > kMaximum - value ? kMaximum : value + addend;
  }

  uint64_t last_scheduled_vblank_ = 0;
  bool has_previous_swap_ = false;
};

}  // namespace rex::graphics

#endif  // REX_GRAPHICS_VBLANK_SWAP_PACER_H_
