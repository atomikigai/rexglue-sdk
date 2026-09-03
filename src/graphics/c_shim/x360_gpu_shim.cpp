/**
 * x360_gpu_shim.cpp -- minimal C++ shim implementing x360_gpu_shim_abi.h,
 * loaded by x360recomp's runtime/src/video/gpu_backend_dynamic.c (x360rt/
 * gpu.h's dynamic GPU backend) to reach rexglue-sdk's IGraphicsSystem
 * (include/rex/system/interfaces/graphics.h) without x360rt itself ever
 * compiling C++.
 *
 * Scope reached today:
 *   create      -- links directly against librexgpu-xenos.so's
 *                  rex_gpu_create/rex_gpu_abi_version (declared below per
 *                  include/rex/system/gpu_plugin.h's documented ABI; not
 *                  dlopen'd from inside this shim -- see CMakeLists.txt,
 *                  which links both librexruntime.so and librexgpu-xenos.so
 *                  directly).
 *   initialize  -- IGraphicsSystem::SetupPresentation(nullptr) (offscreen-
 *                  capable Vulkan provider + Presenter, no window/surface),
 *                  then IGraphicsSystem::SetupGuestGpu(IGpuHost*) through
 *                  CShimGpuHost below (include/rex/graphics/gpu_host.h),
 *                  which wires MMIO, the command processor thread, and the
 *                  vsync ticker without a rex::runtime::FunctionDispatcher
 *                  or rex::system::KernelState -- see gpu_host.h's file
 *                  header for why those were previously required and are
 *                  not anymore.
 *   write_register / read_register -- CShimGpuHost::AddMmioRange is the
 *                  only channel that reaches GraphicsSystem::ReadRegister/
 *                  WriteRegister (both protected on GraphicsSystem, not part
 *                  of the IGraphicsSystem interface): SetupGuestGpu calls it
 *                  once with (GraphicsSystem*, ReadRegisterThunk,
 *                  WriteRegisterThunk); CShimGpuHost stores that triple and
 *                  these two ABI functions replay it.
 *   initialize_ring_buffer / enable_read_pointer_write_back -- forward
 *                  directly to IGraphicsSystem::InitializeRingBuffer/
 *                  EnableReadPointerWriteBack, which reach the real command
 *                  processor. Their `ptr` is a guest physical address (see
 *                  x360rt/video.h); CShimGpuHost::TranslatePhysical resolves
 *                  it against the same `membase` x360rt itself writes ring
 *                  buffer/register memory through (x360_mem_base(), passed
 *                  in below), so what the command processor's worker thread
 *                  reads is exactly what x360rt wrote.
 *   set_interrupt_callback -- stores x360rt's host trampoline
 *                  (x360_video_gpu_interrupt_trampoline, registered by
 *                  VdSetGraphicsInterruptCallback) on CShimGpuHost;
 *                  GraphicsSystem's own vsync worker thread (now real, via
 *                  CShimGpuHost::CreateHostThread) calls
 *                  CShimGpuHost::RaiseGraphicsInterrupt at the guest's
 *                  configured refresh rate, which replays that trampoline.
 *   swap        -- unchanged from before: rex::ui::Presenter::
 *                  RefreshGuestOutput (src/ui/presenter.cpp), a real,
 *                  bounded, no-command Vulkan queue submission with no
 *                  window/swapchain involved. Independent of the ring
 *                  buffer's own XE_SWAP packet, which the command processor
 *                  now also processes as part of normal ring buffer
 *                  draining (see xenos::PM4_XE_SWAP handling in
 *                  command_processor.cpp) -- x360rt's VdSwap (video_swap.c)
 *                  always does both: writes the PM4 packet AND calls this.
 *   shutdown    -- IGraphicsSystem::Shutdown() + delete.
 *
 * Guest content visibility: GraphicsSystem::SetupGuestGpu(IGpuHost*) builds
 * its own rex::memory::Memory for SharedMemory/PrimitiveProcessor/
 * TextureCache (graphics_system.cpp) via Memory::InitializeExternal(this
 * membase), which mirrors this exact host pointer rather than mmap'ing a
 * private arena -- see xmemory.h's doc comment on InitializeExternal for the
 * address-layout contract this depends on (this file's own
 * CShimGpuHost::TranslatePhysical must agree with it, and does). Real guest
 * vertex/index/texture/constant data written by x360rt through
 * X360_HEAP_PHYS_A0 is therefore visible to the renderer, not just ring
 * buffer control flow/register I/O/the swap interrupt/presentation.
 * Write-watch invalidation for that memory (SharedMemory's usual
 * mprotect-based re-upload skip) is a documented no-op in this mode -- see
 * InitializeExternal's own comment -- so CommandProcessor::
 * ExecutePacketType3_XE_SWAP instead force-invalidates once per processed
 * swap packet when memory().is_external(), unrelated to this shim.
 */
#include "x360_gpu_shim_abi.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>

#include <rex/graphics/gpu_host.h>
#include <rex/platform.h>
#include <rex/system/gpu_plugin.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include <SDL3/SDL.h>

#if REX_PLATFORM_GNU_LINUX
#include <X11/Xlib-xcb.h>
#include <rex/ui/surface_gnulinux.h>
#elif REX_PLATFORM_WIN32
#include <rex/ui/surface_win.h>
#elif REX_PLATFORM_MAC
#include <SDL3/SDL_metal.h>
#include <rex/ui/surface_mac.h>
#endif

/* include/rex/system/gpu_plugin.h documents this ABI (kGpuCreateSymbol/
 * kGpuAbiVersionSymbol); declared directly here (rather than resolved via
 * LoadGpuPlugin's dlopen path) because this shim links librexgpu-xenos.so
 * at build time -- see CMakeLists.txt. */
extern "C" {
std::uint32_t rex_gpu_abi_version();
rex::system::IGraphicsSystem* rex_gpu_create(std::uint32_t abi_version,
                                             const rex::system::GpuCreateInfo* info);
}

namespace {

// IGpuHost adapter for x360rt: no rex::runtime::FunctionDispatcher/
// rex::system::KernelState involved (see this file's header comment and
// gpu_host.h). `membase` is x360_mem_base() -- the same host pointer
// x360rt's own runtime/src/video/*.c code writes ring buffer/register
// memory through, so guest-physical addresses resolve to the same bytes on
// both sides.
class CShimGpuHost final : public rex::system::IGpuHost {
 public:
  explicit CShimGpuHost(std::uint8_t* membase) : membase_(membase) {}

  std::uint8_t* membase() const override { return membase_; }

  std::uint8_t* TranslatePhysical(std::uint32_t guest_address) const override {
    // Must match x360rt's own x360_mem_translate_physical (runtime/src/mem/
    // mem_core.c) and rex::memory::Memory::InitializeExternal's
    // physical_membase_ placement exactly: x360rt's guest-physical content
    // lives in the X360_HEAP_PHYS_A0 view (guest 0xA0000000-0xBFFFFFFF), not
    // at a raw (unshifted) host offset -- see x360rt/mem.h's heap layout.
    return membase_ + 0xA0000000u + (guest_address & 0x1FFFFFFFu);
  }

  std::uint8_t* TranslateVirtual(std::uint32_t guest_address) const override {
    return membase_ + guest_address;
  }

  std::uint64_t AddMmioRange(std::uint32_t /*base*/, std::uint32_t /*mask*/,
                             std::uint32_t /*size*/, void* user,
                             rex::system::GpuMmioReadCallback read_cb,
                             rex::system::GpuMmioWriteCallback write_cb) override {
    // x360rt does not trap MMIO accesses through this range the way
    // rex::memory::Memory does for the SDK's own Runtime; runtime/src/
    // ppc/ppc_mmio.c's registry (x360_mmio_register_range) routes
    // 0x7FC80000-0x7FCFFFFF loads/stores to x360_gpu_shim_read_register/
    // write_register below, which replay the callbacks stored here -- see
    // this file's header comment.
    mmio_user_ = user;
    mmio_read_ = read_cb;
    mmio_write_ = write_cb;
    return 1;  // Single fixed range; any non-zero handle works.
  }

  void RemoveMmioRange(std::uint64_t /*handle*/) override {
    mmio_user_ = nullptr;
    mmio_read_ = nullptr;
    mmio_write_ = nullptr;
  }

  std::unique_ptr<rex::system::IGpuHostThread> CreateHostThread(std::string_view /*name*/,
                                                                 void (*fn)(void*),
                                                                 void* user) override;

  void RaiseGraphicsInterrupt(std::uint32_t source, std::uint32_t cpu) override {
    x360_gpu_shim_interrupt_fn cb = interrupt_cb_.load(std::memory_order_acquire);
    if (cb) {
      cb(interrupt_user_data_, source, cpu);
    }
  }

  std::uint64_t QueryGuestTickCount() const override {
    return std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
  }

  void Log(rex::system::GpuLogLevel level, std::string_view message) const override {
    const char* prefix = "info";
    switch (level) {
      case rex::system::GpuLogLevel::kDebug:
        prefix = "debug";
        break;
      case rex::system::GpuLogLevel::kInfo:
        prefix = "info";
        break;
      case rex::system::GpuLogLevel::kWarn:
        prefix = "warn";
        break;
      case rex::system::GpuLogLevel::kError:
        prefix = "error";
        break;
    }
    std::fprintf(stderr, "x360_gpu_shim [%s]: %.*s\n", prefix, int(message.size()),
                message.data());
  }

  // Reached directly (not through IGpuHost) by x360_gpu_shim_read_register/
  // write_register/set_interrupt_callback below.
  bool ReplayWriteRegister(std::uint32_t addr, std::uint32_t value) {
    if (!mmio_write_) {
      return false;
    }
    mmio_write_(nullptr, mmio_user_, addr, value);
    return true;
  }

  bool ReplayReadRegister(std::uint32_t addr, std::uint32_t* out_value) {
    if (!mmio_read_) {
      return false;
    }
    *out_value = mmio_read_(nullptr, mmio_user_, addr);
    return true;
  }

  void SetInterruptCallback(x360_gpu_shim_interrupt_fn cb, void* user_data) {
    interrupt_user_data_ = user_data;
    interrupt_cb_.store(cb, std::memory_order_release);
  }

 private:
  std::uint8_t* membase_;

  void* mmio_user_ = nullptr;
  rex::system::GpuMmioReadCallback mmio_read_ = nullptr;
  rex::system::GpuMmioWriteCallback mmio_write_ = nullptr;

  std::atomic<x360_gpu_shim_interrupt_fn> interrupt_cb_{nullptr};
  void* interrupt_user_data_ = nullptr;
};

class StdGpuHostThread final : public rex::system::IGpuHostThread {
 public:
  explicit StdGpuHostThread(std::thread thread) : thread_(std::move(thread)) {}

  void Join() override {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  std::thread thread_;
};

// x360rt owns the SDL window and pumps its events. This small adapter gives
// the presenter the Window/Surface pairing it requires without exposing any
// rex::ui type through the C ABI or creating a second SDL window.
class ShimWindowedAppContext final : public rex::ui::WindowedAppContext {
 private:
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};

class ShimWindow final : public rex::ui::Window {
 public:
  ShimWindow(rex::ui::WindowedAppContext& app_context, SDL_Window* sdl_window)
      : Window(app_context, "x360recomp", 1, 1), sdl_window_(sdl_window) {}

  ~ShimWindow() override { EnterDestructor(); }

 protected:
  bool OpenImpl() override {
    int width = 0;
    int height = 0;
    if (!sdl_window_ || !SDL_GetWindowSizeInPixels(sdl_window_, &width, &height) || width <= 0 ||
        height <= 0) {
      return false;
    }
    WindowDestructionReceiver destruction_receiver(this);
    OnActualSizeUpdate(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                       destruction_receiver);
    return !destruction_receiver.IsWindowDestroyed();
  }

  void RequestCloseImpl() override {}

  std::unique_ptr<rex::ui::Surface> CreateSurfaceImpl(
      rex::ui::Surface::TypeFlags allowed_types) override {
#if REX_PLATFORM_GNU_LINUX
    SDL_PropertiesID properties = SDL_GetWindowProperties(sdl_window_);
    if (allowed_types & rex::ui::Surface::kTypeFlag_WaylandSurface) {
      auto* display = static_cast<wl_display*>(SDL_GetPointerProperty(
          properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr));
      auto* surface = static_cast<wl_surface*>(SDL_GetPointerProperty(
          properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr));
      if (display && surface) {
        return std::make_unique<rex::ui::WaylandSurface>(display, surface, sdl_window_);
      }
    }
    if (allowed_types & rex::ui::Surface::kTypeFlag_XcbWindow) {
      auto* display = static_cast<Display*>(SDL_GetPointerProperty(
          properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
      auto window = static_cast<xcb_window_t>(SDL_GetNumberProperty(
          properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
      if (display && window) {
        return std::make_unique<rex::ui::XcbWindowSurface>(XGetXCBConnection(display), window);
      }
    }
#elif REX_PLATFORM_WIN32
    if (allowed_types & rex::ui::Surface::kTypeFlag_Win32Hwnd) {
      SDL_PropertiesID properties = SDL_GetWindowProperties(sdl_window_);
      auto hwnd = static_cast<HWND>(
          SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
      auto instance = static_cast<HINSTANCE>(
          SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr));
      if (hwnd) {
        return std::make_unique<rex::ui::Win32HwndSurface>(instance, hwnd);
      }
    }
#elif REX_PLATFORM_MAC
    if (allowed_types & rex::ui::Surface::kTypeFlag_CAMetalLayer) {
      SDL_MetalView view = SDL_Metal_CreateView(sdl_window_);
      if (view) {
        void* layer = SDL_Metal_GetLayer(view);
        if (layer) {
          return std::make_unique<rex::ui::CAMetalLayerSurface>(sdl_window_, view, layer);
        }
        SDL_Metal_DestroyView(view);
      }
    }
#endif
    return nullptr;
  }

  // Normal guest-only presentation is submitted immediately by Presenter.
  // x360rt owns SDL event pumping, so there is no second UI loop to notify.
  void RequestPaintImpl() override {}

 private:
  SDL_Window* sdl_window_;
};

std::unique_ptr<rex::system::IGpuHostThread> CShimGpuHost::CreateHostThread(std::string_view,
                                                                            void (*fn)(void*),
                                                                            void* user) {
  return std::make_unique<StdGpuHostThread>(std::thread(fn, user));
}

struct ShimHandle {
  rex::system::IGraphicsSystem* graphics_system = nullptr;
  std::unique_ptr<CShimGpuHost> gpu_host;
  std::unique_ptr<ShimWindowedAppContext> app_context;
  std::unique_ptr<ShimWindow> window;
  std::atomic<bool> first_swap_entered_logged{false};
  std::atomic<bool> first_swap_logged{false};
  bool guest_gpu_ready = false;
};

ShimHandle* AsHandle(x360_gpu_shim_handle handle) {
  return static_cast<ShimHandle*>(handle);
}

}  // namespace

extern "C" {

std::uint32_t x360_gpu_shim_abi_version(void) {
  return 1u;
}

x360_gpu_shim_handle x360_gpu_shim_create(const char* plugin_dir, const char* backend) {
  (void)plugin_dir; /* See x360_gpu_shim_abi.h: unused (linked, not dlopen'd). */
  if (rex_gpu_abi_version() != rex::system::kGpuPluginAbiVersion) {
    return nullptr;
  }

  rex::system::GpuCreateInfo info{};
  info.struct_size = sizeof(info);
  info.backend = backend ? backend : "any";

  rex::system::IGraphicsSystem* graphics_system =
      rex_gpu_create(rex::system::kGpuPluginAbiVersion, &info);
  if (!graphics_system) {
    return nullptr;
  }

  ShimHandle* handle = new ShimHandle();
  handle->graphics_system = graphics_system;
  return handle;
}

std::int32_t x360_gpu_shim_initialize(x360_gpu_shim_handle handle_v, std::uint8_t* membase,
                                      void* window) {
  ShimHandle* handle = AsHandle(handle_v);
  SDL_Window* sdl_window = static_cast<SDL_Window*>(window);
  if (!handle || !handle->graphics_system || !membase || !sdl_window || !SDL_IsMainThread()) {
    return -1;
  }

  handle->app_context = std::make_unique<ShimWindowedAppContext>();
  handle->window = std::make_unique<ShimWindow>(*handle->app_context, sdl_window);
  if (!handle->window->Open()) {
    handle->window.reset();
    handle->app_context.reset();
    return -1;
  }
  if (XFAILED(handle->graphics_system->SetupPresentation(handle->app_context.get())) ||
      !handle->graphics_system->presenter()) {
    handle->window.reset();
    handle->app_context.reset();
    return -1;
  }
  handle->window->SetPresenter(handle->graphics_system->presenter());
  if (!SDL_ShowWindow(sdl_window)) {
    handle->window->SetPresenter(nullptr);
    handle->window.reset();
    handle->app_context.reset();
    return -1;
  }
  SDL_RaiseWindow(sdl_window);
  std::fprintf(stderr, "x360_gpu_shim: attached SDL window to presenter\n");

  handle->gpu_host = std::make_unique<CShimGpuHost>(membase);
  handle->guest_gpu_ready =
      XSUCCEEDED(handle->graphics_system->SetupGuestGpu(handle->gpu_host.get()));
  if (!handle->guest_gpu_ready) {
    // Degraded but not fatal: swap/shutdown still work (see file header),
    // matching this shim's original present-only behavior.
    handle->gpu_host->Log(rex::system::GpuLogLevel::kError,
                          "SetupGuestGpu(IGpuHost*) failed; register/ring-buffer/interrupt "
                          "operations will report unsupported");
  }
  return 0;
}

std::int32_t x360_gpu_shim_write_register(x360_gpu_shim_handle handle_v, std::uint32_t addr,
                                          std::uint32_t value) {
  ShimHandle* handle = AsHandle(handle_v);
  if (!handle || !handle->guest_gpu_ready || !handle->gpu_host->ReplayWriteRegister(addr, value)) {
    return -1;
  }
  return 0;
}

std::int32_t x360_gpu_shim_read_register(x360_gpu_shim_handle handle_v, std::uint32_t addr,
                                         std::uint32_t* out_value) {
  ShimHandle* handle = AsHandle(handle_v);
  if (!handle || !handle->guest_gpu_ready ||
      !handle->gpu_host->ReplayReadRegister(addr, out_value)) {
    if (out_value) {
      *out_value = 0;
    }
    return -1;
  }
  return 0;
}

std::int32_t x360_gpu_shim_initialize_ring_buffer(x360_gpu_shim_handle handle_v, std::uint32_t ptr,
                                                  std::uint32_t size_log2) {
  ShimHandle* handle = AsHandle(handle_v);
  if (!handle || !handle->guest_gpu_ready) {
    return -1;
  }
  handle->graphics_system->InitializeRingBuffer(ptr, size_log2);
  return 0;
}

std::int32_t x360_gpu_shim_enable_read_pointer_write_back(x360_gpu_shim_handle handle_v,
                                                          std::uint32_t ptr,
                                                          std::uint32_t block_size_log2) {
  ShimHandle* handle = AsHandle(handle_v);
  if (!handle || !handle->guest_gpu_ready) {
    return -1;
  }
  handle->graphics_system->EnableReadPointerWriteBack(ptr, block_size_log2);
  return 0;
}

std::int32_t x360_gpu_shim_set_interrupt_callback(x360_gpu_shim_handle handle_v,
                                                  x360_gpu_shim_interrupt_fn cb, void* user_data) {
  ShimHandle* handle = AsHandle(handle_v);
  if (!handle || !handle->guest_gpu_ready) {
    return -1;
  }
  handle->gpu_host->SetInterruptCallback(cb, user_data);
  return 0;
}

std::int32_t x360_gpu_shim_swap(x360_gpu_shim_handle handle_v, std::uint32_t frontbuffer_ptr,
                                std::uint32_t width, std::uint32_t height) {
  (void)frontbuffer_ptr; /* See file header: the resolved render target, not this raw address,
                          * is what a real IssueSwap would sample -- out of scope (see gpu_host.h). */
  ShimHandle* handle = AsHandle(handle_v);
  if (handle && !handle->first_swap_entered_logged.exchange(true, std::memory_order_relaxed)) {
    std::fprintf(stderr, "x360_gpu_shim: first swap entered (%ux%u)\n", width, height);
  }
  if (!handle || !handle->graphics_system || !handle->graphics_system->presenter()) {
    return -1;
  }
  bool presented = handle->graphics_system->presenter()->RefreshGuestOutput(
      width, height, width, height,
      [](rex::ui::Presenter::GuestOutputRefreshContext&) { return true; });
  if (!handle->first_swap_logged.exchange(true, std::memory_order_relaxed)) {
    std::fprintf(stderr, "x360_gpu_shim: first guest-output refresh %s (%ux%u)\n",
                 presented ? "accepted" : "rejected", width, height);
  }
  return presented ? 0 : -1;
}

void x360_gpu_shim_shutdown(x360_gpu_shim_handle handle_v) {
  ShimHandle* handle = AsHandle(handle_v);
  if (!handle) {
    return;
  }
  if (handle->graphics_system) {
    handle->graphics_system->Shutdown();
    delete handle->graphics_system;
  }
  handle->window.reset();
  handle->app_context.reset();
  delete handle;
}

}  // extern "C"
