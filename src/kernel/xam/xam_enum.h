#pragma once

#include <cstdint>

#include <rex/system/xtypes.h>

namespace rex::kernel::xam {

struct XamEnumerateOverlappedCompletion {
  X_RESULT result;
  uint32_t extended_error;
  uint32_t length;
};

constexpr XamEnumerateOverlappedCompletion MakeXamEnumerateOverlappedCompletion(
    X_RESULT enumerator_result, uint32_t item_count) {
  // Real XAM reports exhaustion as ERROR_FUNCTION_FAILED in the overlapped result while preserving
  // HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES) in extended_error. Xenia Canary does the same mapping:
  // https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xam/xam_enum.cc
  const auto overlapped_result =
      enumerator_result == X_ERROR_NO_MORE_FILES ? X_ERROR_FUNCTION_FAILED : enumerator_result;
  return {overlapped_result, X_HRESULT_FROM_WIN32(enumerator_result), item_count};
}

}  // namespace rex::kernel::xam
