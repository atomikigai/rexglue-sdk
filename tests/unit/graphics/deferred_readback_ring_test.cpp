#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <rex/graphics/deferred_readback_ring.h>

namespace rex::graphics {

TEST_CASE("DeferredReadbackRing grows and reuses completed slots",
          "[graphics][readback]") {
  DeferredReadbackRing ring(3);

  size_t first = ring.AcquireFreeSlot();
  REQUIRE(first == 0);
  ring.MarkPending(first, 7);
  size_t second = ring.AcquireFreeSlot();
  REQUIRE(second == 1);
  ring.MarkPending(second, 7);
  size_t third = ring.AcquireFreeSlot();
  REQUIRE(third == 2);
  ring.MarkPending(third, 8);

  CHECK(ring.slot_count() == 3);
  CHECK(ring.AcquireFreeSlot() == DeferredReadbackRing::kInvalidSlot);
  CHECK(ring.oldest_pending_slot() == first);
  CHECK(ring.pending_submission(first) == 7);

  ring.Release(first);
  CHECK(ring.AcquireFreeSlot() == first);
  ring.MarkPending(first, 9);
  CHECK(ring.oldest_pending_slot() == second);
}

TEST_CASE("DeferredReadbackRing completion releases are deterministic",
          "[graphics][readback]") {
  DeferredReadbackRing ring(4);
  for (uint64_t submission : {11, 12, 12, 14}) {
    size_t slot = ring.AcquireFreeSlot();
    REQUIRE(slot != DeferredReadbackRing::kInvalidSlot);
    ring.MarkPending(slot, submission);
  }

  uint64_t completed_submission = 12;
  size_t released = 0;
  while (true) {
    size_t slot = ring.oldest_pending_slot();
    if (slot == DeferredReadbackRing::kInvalidSlot ||
        ring.pending_submission(slot) > completed_submission) {
      break;
    }
    ring.Release(slot);
    ++released;
  }

  CHECK(released == 3);
  CHECK_FALSE(ring.empty());
  size_t pending = ring.oldest_pending_slot();
  REQUIRE(pending != DeferredReadbackRing::kInvalidSlot);
  CHECK(ring.pending_submission(pending) == 14);
  ring.Release(pending);
  CHECK(ring.empty());
}

}  // namespace rex::graphics
