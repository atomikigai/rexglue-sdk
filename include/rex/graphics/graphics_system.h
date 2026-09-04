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

#pragma once

#include "rex/system/function_dispatcher.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <rex/graphics/gpu_host.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/sdk_gpu_host.h>
#include <rex/kernel.h>
#include <rex/memory.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/xthread.h>
#include <rex/thread/mutex.h>
#include <rex/ui/graphics_provider.h>
#include <rex/ui/presenter.h>

// Forward declarations
namespace rex {
class Memory;
namespace stream {
class ByteStream;
}  // namespace stream
}  // namespace rex

namespace rex::ui {
class WindowedAppContext;
}  // namespace rex::ui

namespace rex::graphics {

class CommandProcessor;

class GraphicsSystem : public system::IGraphicsSystem {
 public:
  virtual ~GraphicsSystem();

  virtual std::string name() const = 0;

  memory::Memory* memory() const { return memory_; }
  runtime::FunctionDispatcher* function_dispatcher() const { return function_dispatcher_; }
  system::KernelState* kernel_state() const { return kernel_state_; }
  system::IGpuHost* gpu_host() const { return gpu_host_; }
  ::rex::ui::GraphicsProvider* provider() const override { return provider_.get(); }
  ::rex::ui::Presenter* presenter() const override { return presenter_.get(); }

  X_STATUS SetupPresentation(::rex::ui::WindowedAppContext* app_context) override;
  // FunctionDispatcher/KernelState overload: builds an internal SdkGpuHost
  // wrapping both, then delegates to the IGpuHost overload below. Existing
  // callers (rex_app/Runtime) are unaffected.
  X_STATUS SetupGuestGpu(runtime::FunctionDispatcher* function_dispatcher,
                         system::KernelState* kernel_state) override;
  // Host-abstracted entry point (see rex/graphics/gpu_host.h); this is where
  // the actual MMIO/command-processor/vsync-worker setup lives.
  X_STATUS SetupGuestGpu(system::IGpuHost* gpu_host) override;
  bool has_presentation() const override { return presenter_ != nullptr; }
  void Shutdown() override;

  // May be called from any thread any number of times, even during recovery
  // from a device loss.
  void OnHostGpuLossFromAnyThread(bool is_responsible);

  RegisterFile* register_file() { return &register_file_; }
  CommandProcessor* command_processor() const { return command_processor_.get(); }

  void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) override;
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) override;

  void SetInterruptCallback(uint32_t callback, uint32_t user_data) override;
  void DispatchInterruptCallback(uint32_t source, uint32_t cpu);

  virtual void ClearCaches();
  virtual void InvalidateGpuMemory();

  void InitializeShaderStorage(const std::filesystem::path& cache_root, uint32_t title_id,
                               bool blocking) override;

  bool is_paused() const { return paused_; }
  void Pause();
  void Resume();

  // Enables the host-only vblank signal used by an opt-in swap pacer. The
  // guest-visible vblank counter and interrupt path are unchanged.
  void EnableSwapVblankSignal();
  uint64_t GetSwapVblankCount() const;
  bool WaitForSwapVblank(uint64_t target_vblank);

  bool Save(::rex::stream::ByteStream* stream);
  bool Restore(::rex::stream::ByteStream* stream);

 protected:
  GraphicsSystem();

  // Backends build their provider here. Called lazily from either setup
  // entry point; with_presentation is false only on headless guest-GPU paths.
  virtual void CreateProvider(bool with_presentation) = 0;

  virtual std::unique_ptr<CommandProcessor> CreateCommandProcessor() = 0;

  static uint32_t ReadRegisterThunk(void* ppc_context, GraphicsSystem* gs, uint32_t addr);
  static void WriteRegisterThunk(void* ppc_context, GraphicsSystem* gs, uint32_t addr,
                                 uint32_t value);

  uint32_t ReadRegister(uint32_t addr);
  void WriteRegister(uint32_t addr, uint32_t value);

  void MarkVblank();

  // Runs on the host thread created by gpu_host_->CreateHostThread in
  // SetupGuestGpu(IGpuHost*); ticks MarkVblank at the guest's configured
  // refresh rate until vsync_worker_running_ goes false.
  void RunVsyncLoop();
  static void RunVsyncLoopThunk(void* user);

  // memory_/function_dispatcher_/kernel_state_ remain populated (and behave
  // exactly as before) when SetupGuestGpu(FunctionDispatcher*, KernelState*)
  // was used; they stay null for a GraphicsSystem set up directly through
  // SetupGuestGpu(IGpuHost*) (e.g. rexglue-sdk's C shim). Guest-address
  // translation and MMIO/threading/interrupt dispatch always go through
  // gpu_host_, which is set in both cases.
  memory::Memory* memory_ = nullptr;
  runtime::FunctionDispatcher* function_dispatcher_ = nullptr;
  system::KernelState* kernel_state_ = nullptr;
  system::IGpuHost* gpu_host_ = nullptr;
  std::unique_ptr<system::SdkGpuHost> owned_gpu_host_;
  // Standalone rex::memory::Memory built only when SetupGuestGpu(IGpuHost*)
  // is used directly (no FunctionDispatcher, so no Memory to reuse): backend
  // subsystems (SharedMemory/PrimitiveProcessor/TextureCache) still need a
  // real Memory instance for host-GPU bookkeeping. Initialized via
  // Memory::InitializeExternal(gpu_host_->membase()) when the host exposes
  // one (e.g. x360recomp's x360rt), so it mirrors real guest content instead
  // of a private, unshared arena -- see SetupGuestGpu's definition.
  std::unique_ptr<memory::Memory> fallback_memory_;
  ::rex::ui::WindowedAppContext* app_context_ = nullptr;
  std::unique_ptr<::rex::ui::GraphicsProvider> provider_;
  bool provider_supports_presentation_ = false;

  uint32_t interrupt_callback_ = 0;
  uint32_t interrupt_callback_data_ = 0;

  uint64_t mmio_range_handle_ = 0;
  std::atomic<bool> vsync_worker_running_;
  std::unique_ptr<system::IGpuHostThread> vsync_worker_thread_;

  // Inactive in the default configuration. MarkVblank performs only a single
  // flag check unless a backend explicitly enables swap pacing.
  std::atomic<bool> swap_vblank_signal_enabled_ = false;
  mutable std::mutex swap_vblank_mutex_;
  std::condition_variable swap_vblank_condition_;
  uint64_t swap_vblank_count_ = 0;

  RegisterFile register_file_;
  std::unique_ptr<CommandProcessor> command_processor_;

  bool paused_ = false;

 private:
  std::unique_ptr<::rex::ui::Presenter> presenter_;

  std::atomic_flag host_gpu_loss_reported_;
};

}  // namespace rex::graphics
