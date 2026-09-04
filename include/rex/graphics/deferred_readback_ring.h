/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef REX_GRAPHICS_DEFERRED_READBACK_RING_H_
#define REX_GRAPHICS_DEFERRED_READBACK_RING_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace rex::graphics {

// Submission ownership for a dynamically-sized readback ring. Resource handles
// and mappings are kept by the backend so this policy can be tested without a
// graphics API or a device.
class DeferredReadbackRing {
 public:
  static constexpr size_t kInvalidSlot = std::numeric_limits<size_t>::max();
  static constexpr size_t kDefaultMaxSlots = 256;

  explicit DeferredReadbackRing(size_t max_slots = kDefaultMaxSlots)
      : max_slots_(max_slots) {}

  size_t AcquireFreeSlot() {
    for (size_t offset = 0; offset < submissions_.size(); ++offset) {
      size_t index = (next_index_ + offset) % submissions_.size();
      if (!submissions_[index]) {
        return index;
      }
    }
    if (submissions_.size() >= max_slots_) {
      return kInvalidSlot;
    }
    submissions_.push_back(0);
    issue_order_.push_back(0);
    return submissions_.size() - 1;
  }

  void MarkPending(size_t index, uint64_t submission) {
    if (index >= submissions_.size() || !submission) {
      return;
    }
    submissions_[index] = submission;
    issue_order_[index] = next_issue_order_++;
    next_index_ = (index + 1) % submissions_.size();
  }

  void Release(size_t index) {
    if (index < submissions_.size()) {
      submissions_[index] = 0;
      issue_order_[index] = 0;
    }
  }

  size_t oldest_pending_slot() const {
    size_t oldest_slot = kInvalidSlot;
    uint64_t oldest_order = std::numeric_limits<uint64_t>::max();
    for (size_t i = 0; i < submissions_.size(); ++i) {
      if (submissions_[i] && issue_order_[i] < oldest_order) {
        oldest_slot = i;
        oldest_order = issue_order_[i];
      }
    }
    return oldest_slot;
  }

  uint64_t pending_submission(size_t index) const {
    return index < submissions_.size() ? submissions_[index] : 0;
  }
  size_t slot_count() const { return submissions_.size(); }
  size_t max_slots() const { return max_slots_; }

  bool empty() const {
    for (uint64_t submission : submissions_) {
      if (submission) {
        return false;
      }
    }
    return true;
  }

 private:
  std::vector<uint64_t> submissions_;
  std::vector<uint64_t> issue_order_;
  size_t next_index_ = 0;
  size_t max_slots_ = kDefaultMaxSlots;
  uint64_t next_issue_order_ = 1;
};

}  // namespace rex::graphics

#endif  // REX_GRAPHICS_DEFERRED_READBACK_RING_H_
