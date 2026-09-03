/**
 * @file        graphics/sdk_gpu_host.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/graphics/sdk_gpu_host.h>

#include <chrono>
#include <string>
#include <utility>

#include <rex/assert.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/memory.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>

namespace rex::system {

namespace {

class SdkGpuHostThread final : public IGpuHostThread {
 public:
  explicit SdkGpuHostThread(object_ref<XHostThread> thread) : thread_(std::move(thread)) {}

  void Join() override {
    if (thread_) {
      thread_->Wait(0, 0, 0, nullptr);
      thread_.reset();
    }
  }

  void Resume() override {
    if (thread_) {
      thread_->thread()->Resume();
    }
  }

 private:
  object_ref<XHostThread> thread_;
};

}  // namespace

SdkGpuHost::SdkGpuHost(runtime::FunctionDispatcher* function_dispatcher, KernelState* kernel_state)
    : function_dispatcher_(function_dispatcher), kernel_state_(kernel_state) {}

SdkGpuHost::~SdkGpuHost() = default;

uint8_t* SdkGpuHost::membase() const {
  return function_dispatcher_->memory()->physical_membase();
}

uint8_t* SdkGpuHost::TranslatePhysical(uint32_t guest_address) const {
  return function_dispatcher_->memory()->TranslatePhysical<uint8_t*>(guest_address);
}

uint8_t* SdkGpuHost::TranslateVirtual(uint32_t guest_address) const {
  return function_dispatcher_->memory()->TranslateVirtual<uint8_t*>(guest_address);
}

uint64_t SdkGpuHost::AddMmioRange(uint32_t base, uint32_t mask, uint32_t size, void* user,
                                  GpuMmioReadCallback read_cb, GpuMmioWriteCallback write_cb) {
  bool added = function_dispatcher_->memory()->AddVirtualMappedRange(
      base, mask, size, user, reinterpret_cast<runtime::MMIOReadCallback>(read_cb),
      reinterpret_cast<runtime::MMIOWriteCallback>(write_cb));
  // rex::memory::Memory has no MMIO range removal API (matches the pre-
  // IGpuHost behavior of never unregistering GraphicsSystem's GPU register
  // range at Shutdown); `base` is stable and non-zero for any real GPU
  // register block, so it doubles as the "registered" handle here.
  return added ? uint64_t(base) : 0;
}

void SdkGpuHost::RemoveMmioRange(uint64_t handle) {
  // No-op: see AddMmioRange. Kept for interface symmetry with hosts (the C
  // shim) that do support real removal.
  (void)handle;
}

std::unique_ptr<IGpuHostThread> SdkGpuHost::CreateHostThread(std::string_view name,
                                                              void (*fn)(void* user), void* user) {
  auto thread = object_ref<XHostThread>(new XHostThread(kernel_state_, 128 * 1024, 0, [fn, user]() {
    fn(user);
    return 0;
  }));
  thread->set_name(std::string(name));
  thread->Create();
  return std::make_unique<SdkGpuHostThread>(std::move(thread));
}

void SdkGpuHost::SetInterruptTarget(uint32_t callback_address, uint32_t user_data) {
  interrupt_callback_address_ = callback_address;
  interrupt_callback_user_data_ = user_data;
}

void SdkGpuHost::RaiseGraphicsInterrupt(uint32_t source, uint32_t cpu) {
  if (!interrupt_callback_address_) {
    return;
  }

  auto thread = XThread::GetCurrentThread();
  assert_not_null(thread);

  if (cpu == 0xFFFFFFFF) {
    cpu = 2;
  }
  thread->SetActiveCpu(uint8_t(cpu));

  uint64_t args[] = {source, interrupt_callback_user_data_};
  function_dispatcher_->ExecuteInterrupt(thread->thread_state(), interrupt_callback_address_, args,
                                         rex::countof(args));
}

uint64_t SdkGpuHost::QueryGuestTickCount() const {
  return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
}

void SdkGpuHost::Log(GpuLogLevel level, std::string_view message) const {
  switch (level) {
    case GpuLogLevel::kDebug:
      REXGPU_DEBUG("{}", message);
      break;
    case GpuLogLevel::kInfo:
      REXGPU_INFO("{}", message);
      break;
    case GpuLogLevel::kWarn:
      REXGPU_WARN("{}", message);
      break;
    case GpuLogLevel::kError:
      REXGPU_ERROR("{}", message);
      break;
  }
}

}  // namespace rex::system
