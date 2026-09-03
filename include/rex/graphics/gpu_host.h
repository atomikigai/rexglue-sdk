/**
 * @file        graphics/gpu_host.h
 * @brief       Host abstraction consumed by GraphicsSystem::SetupGuestGpu
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 *
 * @remarks     IGraphicsSystem::SetupGuestGpu originally required a full
 *              rex::runtime::FunctionDispatcher* (guest memory + guest
 *              function execution) and a full rex::system::KernelState*
 *              (guest thread bookkeeping). Both are heavyweight rexglue-sdk
 *              Runtime types that a GPU-only integration -- notably
 *              x360recomp's C11 runtime (x360rt), which owns its own guest
 *              memory and threading and never links rexglue-sdk's C++
 *              Runtime -- cannot construct.
 *
 *              IGpuHost narrows that dependency down to exactly what
 *              GraphicsSystem needs: guest-physical/virtual address
 *              translation for the ring buffer and register reads it
 *              performs directly (see CommandProcessor), MMIO range
 *              registration for the 0x7FC80000-0x7FCFFFFF GPU register
 *              block, a way to spin up host worker threads (the command
 *              processor thread and the vsync ticker), and a way to raise
 *              the guest graphics interrupt without knowing how the host
 *              actually dispatches guest code.
 *
 *              Two implementations exist:
 *                - SdkGpuHost (include/rex/graphics/sdk_gpu_host.h): wraps a
 *                  real FunctionDispatcher + KernelState, used internally by
 *                  GraphicsSystem::SetupGuestGpu(FunctionDispatcher*,
 *                  KernelState*) so rex_app/Runtime callers are unaffected.
 *                - x360_gpu_shim.cpp's CShimGpuHost: wraps x360rt's guest
 *                  membase (already passed to x360_gpu_shim_initialize) plus
 *                  self-contained host threading/timing, with no rexglue-sdk
 *                  Runtime involved at all.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace rex::system {

// Matches rex::runtime::MMIOReadCallback/MMIOWriteCallback exactly (see
// include/rex/system/mmio_handler.h) so SdkGpuHost can forward the callbacks
// GraphicsSystem registers straight into rex::memory::Memory without a
// signature mismatch; hosts that do not use that MMIO trap mechanism (the
// C shim) are free to just store and forward them by hand.
using GpuMmioReadCallback = uint32_t (*)(void* ppc_context, void* user, uint32_t addr);
using GpuMmioWriteCallback = void (*)(void* ppc_context, void* user, uint32_t addr,
                                      uint32_t value);

enum class GpuLogLevel : uint8_t {
  kDebug,
  kInfo,
  kWarn,
  kError,
};

// A host-provided worker thread. Returned already running; the owner joins
// it exactly once (typically at Shutdown) and then drops it.
class IGpuHostThread {
 public:
  virtual ~IGpuHostThread() = default;
  virtual void Join() = 0;

  // Resumes a thread previously suspended by CommandProcessor::Pause (which
  // suspends itself from inside its own worker loop via the generic
  // rex::thread::Thread API, so it needs no host involvement) --
  // CommandProcessor::Resume needs the host's help only for this direction.
  // Default: not supported (documented no-op); hosts that cannot suspend/
  // resume an arbitrary thread they created (e.g. a plain std::thread, used
  // by rexglue-sdk's C shim) simply never see Pause()/Resume() reach a real
  // guest integration yet -- SetupGuestGpu(IGpuHost*) is a new capability,
  // so this is not a regression to any previously working path.
  virtual void Resume() {}
};

class IGpuHost {
 public:
  virtual ~IGpuHost() = default;

  // Base host pointer backing guest-physical address translation; equal to
  // TranslatePhysical(0). Exposed separately because some callers need the
  // raw base (e.g. to hand to a third API expecting a flat host pointer).
  virtual uint8_t* membase() const = 0;

  // Translates a guest physical/virtual address to a host pointer, matching
  // rex::memory::Memory::TranslatePhysical/TranslateVirtual's semantics
  // (big-endian contents, no bounds checking beyond what the host chooses to
  // do). CommandProcessor uses TranslatePhysical exclusively today (ring
  // buffer, register poll/write, shader loads); TranslateVirtual is provided
  // for symmetry/future use.
  virtual uint8_t* TranslatePhysical(uint32_t guest_address) const = 0;
  virtual uint8_t* TranslateVirtual(uint32_t guest_address) const = 0;

  // Registers dword read/write callbacks for a GPU register MMIO range.
  // `mask`/`size` follow rex::memory::Memory::AddVirtualMappedRange's
  // convention (mask selects which address bits participate in the range
  // match; size is the tracked range length). Returns a non-zero handle on
  // success, 0 on failure; RemoveMmioRange releases it. Hosts that route
  // MMIO through an explicit dispatch table instead of an OS-level trap (the
  // C shim, via x360rt's ppc_mmio.c) may implement this by storing the
  // callbacks and returning a constant handle rather than actually trapping
  // accesses themselves -- see that host's own definition of AddMmioRange
  // for how such a host still makes the callbacks reachable.
  virtual uint64_t AddMmioRange(uint32_t base, uint32_t mask, uint32_t size, void* user,
                                GpuMmioReadCallback read_cb, GpuMmioWriteCallback write_cb) = 0;
  virtual void RemoveMmioRange(uint64_t handle) = 0;

  // Starts a host worker thread running fn(user); returns immediately with a
  // handle to join later. `name` is used for host thread naming/debugging
  // only.
  virtual std::unique_ptr<IGpuHostThread> CreateHostThread(std::string_view name,
                                                            void (*fn)(void* user),
                                                            void* user) = 0;

  // Called once whenever the guest registers/clears its graphics interrupt
  // callback (mirrors the optional IGraphicsSystem::SetInterruptCallback
  // capability). Hosts that track the guest callback address themselves
  // (the C shim, via x360rt's own VdSetGraphicsInterruptCallback state) may
  // ignore this; hosts that need it to dispatch (SdkGpuHost, via
  // FunctionDispatcher::ExecuteInterrupt) store it. Default: no-op.
  virtual void SetInterruptTarget(uint32_t callback_address, uint32_t user_data) {
    (void)callback_address;
    (void)user_data;
  }

  // Raises the guest graphics interrupt with (source, cpu), mirroring
  // GraphicsSystem::DispatchInterruptCallback's arguments. The host owns how
  // a guest callback actually gets executed.
  virtual void RaiseGraphicsInterrupt(uint32_t source, uint32_t cpu) = 0;

  // Monotonic host tick count in nanoseconds since an arbitrary, stable
  // epoch. Used only to schedule the vsync ticker at the guest's configured
  // refresh rate; never exposed to guest-visible state, so hosts do not need
  // to match the Xbox 360's actual 50 MHz guest timebase.
  virtual uint64_t QueryGuestTickCount() const = 0;

  virtual void Log(GpuLogLevel level, std::string_view message) const = 0;
};

}  // namespace rex::system
