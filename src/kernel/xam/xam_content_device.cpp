/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/cvar.h>
#include <rex/kernel/xam/private.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/system/flags.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xenumerator.h>
#include <rex/system/xtypes.h>

// Low-volume trace of Xam content-container and storage-device API calls
// (create/open/close/delete/enumerate content, get device data/state/name,
// enumerate devices, device selector UI) with their arguments and return
// values. Off by default; every call site below is gated so that no string
// is formatted or constructed while this cvar is false. Follows the same
// pattern as file_lifecycle_trace (src/kernel/xboxkrnl/xboxkrnl_io.cpp) and
// profile_settings_trace (src/system/xam/user_profile.cpp).
REXCVAR_DEFINE_BOOL(xam_content_device_trace, false, "Kernel",
                    "Log Xam content-container and storage-device API calls (create, open/close, "
                    "delete, enumerate content; device data/state/name, device enumeration, "
                    "device selector UI) with arguments and return values");

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::system;
using namespace rex::system::xam;

// TODO(gibbed): real information.
//
// Until we expose real information about a HDD device, we
// claim there is 3GB free on a 4GB dummy HDD.
//
// There is a possibility that certain games are bugged in that
// they incorrectly only look at the lower 32-bits of free_bytes,
// when it is a 64-bit value. Which means any size above ~4GB
// will not be recognized properly.
#define ONE_GB (1024ull * 1024ull * 1024ull)

static const DummyDeviceInfo dummy_hdd_device_info_ = {
    DummyDeviceId::HDD, DeviceType::HDD,
    20ull * ONE_GB,  // 20GB
    3ull * ONE_GB,   // 3GB, so it looks a little used.
    u"Dummy HDD",
};
static const DummyDeviceInfo dummy_odd_device_info_ = {
    DummyDeviceId::ODD, DeviceType::ODD,
    7ull * ONE_GB,  // 7GB (rough maximum)
    0ull * ONE_GB,  // read-only FS, so no free space
    u"Dummy ODD",
};
static const DummyDeviceInfo* dummy_device_infos_[] = {
    &dummy_hdd_device_info_,
    &dummy_odd_device_info_,
};
#undef ONE_GB

const DummyDeviceInfo* GetDummyDeviceInfo(uint32_t device_id) {
  const auto& begin = std::begin(dummy_device_infos_);
  const auto& end = std::end(dummy_device_infos_);
  auto it = std::find_if(begin, end, [device_id](const auto& item) {
    return static_cast<uint32_t>(item->device_id) == device_id;
  });
  return it == end ? nullptr : *it;
}

namespace {

// Logs one content/device API call. Only invoked from call sites already
// gated on REXCVAR_GET(xam_content_device_trace), so no formatting happens
// when the trace is disabled.
void TraceContentDeviceCall(const char* api, uint32_t device_id, const DummyDeviceInfo* device_info,
                            uint32_t result) {
  if (device_info) {
    REXKRNL_INFO(
        "[xam_content_device] {} device_id={:#x} found=true type={} total_bytes={} "
        "free_bytes={} result={:#x}",
        api, device_id, static_cast<uint32_t>(device_info->device_type), device_info->total_bytes,
        device_info->free_bytes, result);
  } else {
    REXKRNL_INFO("[xam_content_device] {} device_id={:#x} found=false result={:#x}", api, device_id,
                 result);
  }
}

}  // namespace

u32 XamContentGetDeviceName_entry(u32 device_id, mapped_wstring name_buffer, u32 name_capacity) {
  auto device_info = GetDummyDeviceInfo(device_id);
  u32 result = X_ERROR_SUCCESS;
  if (device_info == nullptr) {
    result = X_ERROR_DEVICE_NOT_CONNECTED;
  } else {
    auto name = std::u16string(device_info->name);
    if (name_capacity < name.size() + 1) {
      result = X_ERROR_INSUFFICIENT_BUFFER;
    } else {
      rex::string::copy_and_swap_truncating(name_buffer, name, name_capacity);
    }
  }
  if (REXCVAR_GET(xam_content_device_trace)) {
    TraceContentDeviceCall("XamContentGetDeviceName", device_id, device_info, result);
  }
  return result;
}

u32 XamContentGetDeviceState_entry(u32 device_id, mapped_void overlapped_ptr) {
  auto device_info = GetDummyDeviceInfo(device_id);
  X_RESULT result = device_info ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  if (REXCVAR_GET(xam_content_device_trace)) {
    TraceContentDeviceCall("XamContentGetDeviceState", device_id, device_info, result);
  }
  if (overlapped_ptr) {
    if (device_info == nullptr) {
      REX_KERNEL_STATE()->CompleteOverlappedImmediateEx(
          overlapped_ptr.guest_address(), X_ERROR_FUNCTION_FAILED, X_ERROR_DEVICE_NOT_CONNECTED, 0);
    } else {
      REX_KERNEL_STATE()->CompleteOverlappedImmediate(overlapped_ptr.guest_address(),
                                                      X_ERROR_SUCCESS);
    }
    return X_ERROR_IO_PENDING;
  }
  return result;
}

typedef struct {
  rex::be<uint32_t> device_id;
  rex::be<uint32_t> device_type;
  rex::be<uint64_t> total_bytes;
  rex::be<uint64_t> free_bytes;
  union {
    rex::be<uint16_t> name[28];
    char16_t name_chars[28];
  };
} X_CONTENT_DEVICE_DATA;
static_assert_size(X_CONTENT_DEVICE_DATA, 0x50);

u32 XamContentGetDeviceData_entry(u32 device_id, ppc_ptr_t<X_CONTENT_DEVICE_DATA> device_data) {
  auto device_info = GetDummyDeviceInfo(device_id);
  u32 result = X_ERROR_SUCCESS;
  if (device_info == nullptr) {
    result = X_ERROR_DEVICE_NOT_CONNECTED;
  } else {
    device_data.Zero();
    device_data->device_id = static_cast<uint32_t>(device_info->device_id);
    device_data->device_type = static_cast<uint32_t>(device_info->device_type);
    device_data->total_bytes = device_info->total_bytes;
    device_data->free_bytes = device_info->free_bytes;
    rex::string::copy_and_swap_truncating(device_data->name_chars, device_info->name,
                                          rex::countof(device_data->name_chars));
  }
  if (REXCVAR_GET(xam_content_device_trace)) {
    TraceContentDeviceCall("XamContentGetDeviceData", device_id, device_info, result);
  }
  return result;
}

u32 XamContentCreateDeviceEnumerator_entry(u32 content_type, u32 content_flags, u32 max_count,
                                           mapped_u32 buffer_size_ptr, mapped_u32 handle_out) {
  assert_not_null(handle_out);

  if (buffer_size_ptr) {
    *buffer_size_ptr = sizeof(X_CONTENT_DEVICE_DATA) * max_count;
  }

  auto e = make_object<XStaticEnumerator<X_CONTENT_DEVICE_DATA>>(REX_KERNEL_STATE(), max_count);
  auto result = e->Initialize(0xFE, 0xFE, 0x2000A, 0x20009, 0);
  if (XFAILED(result)) {
    if (REXCVAR_GET(xam_content_device_trace)) {
      REXKRNL_INFO(
          "[xam_content_device] XamContentCreateDeviceEnumerator content_type={:#x} "
          "flags={:#x} max_count={} -> Initialize failed result={:#x}",
          content_type, content_flags, max_count, uint32_t(result));
    }
    return result;
  }

  for (const auto& device_info : dummy_device_infos_) {
    // Copy our dummy device into the enumerator
    auto device_data = e->AppendItem();
    assert_not_null(device_data);
    if (device_data) {
      device_data->device_id = static_cast<uint32_t>(device_info->device_id);
      device_data->device_type = static_cast<uint32_t>(device_info->device_type);
      device_data->total_bytes = device_info->total_bytes;
      device_data->free_bytes = device_info->free_bytes;
      rex::string::copy_and_swap_truncating(device_data->name_chars, device_info->name,
                                            rex::countof(device_data->name_chars));
    }
  }

  *handle_out = e->handle();
  if (REXCVAR_GET(xam_content_device_trace)) {
    REXKRNL_INFO(
        "[xam_content_device] XamContentCreateDeviceEnumerator content_type={:#x} flags={:#x} "
        "max_count={} -> items={} handle={:#x} result={:#x}",
        content_type, content_flags, max_count, e->item_count(), e->handle(),
        uint32_t(X_ERROR_SUCCESS));
  }
  return X_ERROR_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

// fix me later
namespace rex {
namespace system {
namespace xam {

const DummyDeviceInfo* GetDummyDeviceInfo(uint32_t device_id) {
  return rex::kernel::xam::GetDummyDeviceInfo(device_id);
}

}  // namespace xam
}  // namespace system
}  // namespace rex

REX_EXPORT(__imp__XamContentGetDeviceName, rex::kernel::xam::XamContentGetDeviceName_entry)
REX_EXPORT(__imp__XamContentGetDeviceState, rex::kernel::xam::XamContentGetDeviceState_entry)
REX_EXPORT(__imp__XamContentGetDeviceData, rex::kernel::xam::XamContentGetDeviceData_entry)
REX_EXPORT(__imp__XamContentCreateDeviceEnumerator,
           rex::kernel::xam::XamContentCreateDeviceEnumerator_entry)

REX_EXPORT_STUB(__imp__XamContentAddCacheDevice);
REX_EXPORT_STUB(__imp__XamContentDeviceCheckUpdates);
REX_EXPORT_STUB(__imp__XamContentGetDefaultDevice);
REX_EXPORT_STUB(__imp__XamContentGetDeviceSerialNumber);
REX_EXPORT_STUB(__imp__XamContentGetDeviceVolumePath);
REX_EXPORT_STUB(__imp__XamContentGetLocalizedDeviceData);
REX_EXPORT_STUB(__imp__XamContentRemoveCacheDevice);
