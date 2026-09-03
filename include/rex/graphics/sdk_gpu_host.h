/**
 * @file        graphics/sdk_gpu_host.h
 * @brief       IGpuHost adapter over a real FunctionDispatcher + KernelState
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 *
 * @remarks     Used internally by GraphicsSystem::SetupGuestGpu(
 *              FunctionDispatcher*, KernelState*) to reimplement that
 *              overload on top of SetupGuestGpu(IGpuHost*) without changing
 *              behavior for rex_app/Runtime callers: every method below
 *              forwards to the exact same rex::memory::Memory /
 *              rex::system::XHostThread / rex::runtime::FunctionDispatcher
 *              calls GraphicsSystem::SetupGuestGpu made directly before this
 *              adapter existed.
 */

#pragma once

#include <cstdint>

#include <rex/graphics/gpu_host.h>

namespace rex::runtime {
class FunctionDispatcher;
}  // namespace rex::runtime

namespace rex::system {

class KernelState;

class SdkGpuHost final : public IGpuHost {
 public:
  SdkGpuHost(runtime::FunctionDispatcher* function_dispatcher, KernelState* kernel_state);
  ~SdkGpuHost() override;

  uint8_t* membase() const override;
  uint8_t* TranslatePhysical(uint32_t guest_address) const override;
  uint8_t* TranslateVirtual(uint32_t guest_address) const override;

  uint64_t AddMmioRange(uint32_t base, uint32_t mask, uint32_t size, void* user,
                        GpuMmioReadCallback read_cb, GpuMmioWriteCallback write_cb) override;
  void RemoveMmioRange(uint64_t handle) override;

  std::unique_ptr<IGpuHostThread> CreateHostThread(std::string_view name, void (*fn)(void* user),
                                                   void* user) override;

  void SetInterruptTarget(uint32_t callback_address, uint32_t user_data) override;
  void RaiseGraphicsInterrupt(uint32_t source, uint32_t cpu) override;

  uint64_t QueryGuestTickCount() const override;

  void Log(GpuLogLevel level, std::string_view message) const override;

 private:
  runtime::FunctionDispatcher* function_dispatcher_;
  KernelState* kernel_state_;
  uint32_t interrupt_callback_address_ = 0;
  uint32_t interrupt_callback_user_data_ = 0;
};

}  // namespace rex::system
