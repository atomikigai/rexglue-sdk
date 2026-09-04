#include <array>
#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <rex/system/xenumerator.h>
#include <rex/system/xio.h>

#include "kernel/xam/xam_enum.h"

namespace {

using rex::X_HRESULT;
using rex::X_RESULT;
using rex::kernel::xam::MakeXamEnumerateOverlappedCompletion;
using namespace rex::system;

TEST_CASE("empty XamEnumerate overlapped completion uses the XAM error tuple", "[xam][enumerate]") {
  constexpr auto completion = MakeXamEnumerateOverlappedCompletion(X_ERROR_NO_MORE_FILES, 0);

  STATIC_REQUIRE(completion.result == X_ERROR_FUNCTION_FAILED);
  STATIC_REQUIRE(completion.extended_error == X_HRESULT_FROM_WIN32(X_ERROR_NO_MORE_FILES));
  STATIC_REQUIRE(completion.length == 0);
}

TEST_CASE("XamEnumerate overlapped completion preserves other results", "[xam][enumerate]") {
  constexpr auto success = MakeXamEnumerateOverlappedCompletion(X_ERROR_SUCCESS, 2);
  constexpr auto invalid = MakeXamEnumerateOverlappedCompletion(X_ERROR_INVALID_PARAMETER, 0);

  STATIC_REQUIRE(success.result == X_ERROR_SUCCESS);
  STATIC_REQUIRE(success.extended_error == X_HRESULT_FROM_WIN32(X_ERROR_SUCCESS));
  STATIC_REQUIRE(success.length == 2);
  STATIC_REQUIRE(invalid.result == X_ERROR_INVALID_PARAMETER);
  STATIC_REQUIRE(invalid.extended_error == X_HRESULT_FROM_WIN32(X_ERROR_INVALID_PARAMETER));
}

TEST_CASE("empty static enumerator returns no more files without writes", "[xam][enumerate]") {
  XStaticUntypedEnumerator enumerator(nullptr, 1, sizeof(uint32_t));
  std::array<uint8_t, sizeof(uint32_t)> buffer{};
  uint32_t written_count = 0;

  CHECK(enumerator.WriteItems(0, buffer.data(), &written_count) == X_ERROR_NO_MORE_FILES);
  CHECK(written_count == 0);
}

TEST_CASE("XAM_OVERLAPPED setters target the guest-visible fields", "[xam][enumerate]") {
  STATIC_REQUIRE(offsetof(XAM_OVERLAPPED, result) == 0x0);
  STATIC_REQUIRE(offsetof(XAM_OVERLAPPED, length) == 0x4);
  STATIC_REQUIRE(offsetof(XAM_OVERLAPPED, extended_error) == 0x18);

  alignas(XAM_OVERLAPPED) std::array<uint8_t, sizeof(XAM_OVERLAPPED)> storage{};
  auto* overlapped = reinterpret_cast<XAM_OVERLAPPED*>(storage.data());
  XOverlappedSetResult(overlapped, X_ERROR_FUNCTION_FAILED);
  XOverlappedSetLength(overlapped, 0);
  XOverlappedSetExtendedError(overlapped, X_HRESULT_FROM_WIN32(X_ERROR_NO_MORE_FILES));

  CHECK(uint32_t(overlapped->result) == X_ERROR_FUNCTION_FAILED);
  CHECK(uint32_t(overlapped->length) == 0);
  CHECK(uint32_t(overlapped->extended_error) == X_HRESULT_FROM_WIN32(X_ERROR_NO_MORE_FILES));
}

}  // namespace
