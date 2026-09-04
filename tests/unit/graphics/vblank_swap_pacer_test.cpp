#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include <rex/graphics/vblank_swap_pacer.h>

namespace rex::graphics {

TEST_CASE("VblankSwapPacer spaces on-time swaps", "[graphics][pacing]") {
  VblankSwapPacer pacer;

  CHECK(pacer.Schedule(100, 2) == 101);
  CHECK(pacer.Schedule(100, 2) == 103);
  CHECK(pacer.Schedule(102, 2) == 105);
  CHECK(pacer.Schedule(104, 2) == 107);
}

TEST_CASE("VblankSwapPacer realigns late swaps without debt", "[graphics][pacing]") {
  VblankSwapPacer pacer;

  CHECK(pacer.Schedule(20, 2) == 21);
  CHECK(pacer.Schedule(24, 2) == 25);
  CHECK(pacer.Schedule(25, 2) == 27);
  CHECK(pacer.Schedule(26, 2) == 29);
}

TEST_CASE("VblankSwapPacer saturates at the counter limit", "[graphics][pacing]") {
  VblankSwapPacer pacer;
  constexpr uint64_t kMaximum = std::numeric_limits<uint64_t>::max();

  CHECK(pacer.Schedule(kMaximum - 1, 2) == kMaximum);
  CHECK(pacer.Schedule(kMaximum - 1, 2) == kMaximum);
  CHECK(pacer.Schedule(kMaximum, 2) == kMaximum);
}

}  // namespace rex::graphics
