/**
 * x360_gpu_shim_abi.h -- C ABI exported by librexgpu-xenos-cshim.so
 * (x360_gpu_shim.cpp), consumed by x360recomp's runtime/src/video/
 * gpu_backend_dynamic.c (x360rt/gpu.h's dynamic GPU backend).
 *
 * This header is documentation, not a shared compilation unit: the C side
 * (gpu_backend_dynamic.c) resolves every symbol below with dlsym() and its
 * own local function-pointer typedefs rather than including this file, so
 * that x360rt never needs a C++23 toolchain or this repository's include
 * path. Keep the two declarations in sync by hand; a mismatch is caught at
 * runtime (wrong argument types corrupt the stack/registers silently -- the
 * ABI version guard below only catches *some* mismatches, not all).
 *
 * Every function beyond create/shutdown returns int32_t: 0 on success,
 * nonzero (conventionally -1) on failure/unsupported. All of them are now
 * real (see x360_gpu_shim.cpp's file header for exactly what each one
 * reaches inside rexglue-sdk's IGraphicsSystem), unblocked by
 * rex::system::IGpuHost (include/rex/graphics/gpu_host.h): a host
 * abstraction narrow enough for this shim to implement (CShimGpuHost, over
 * just the `membase` already passed to x360_gpu_shim_initialize below) in
 * place of the full rex::system::KernelState + rex::runtime::
 * FunctionDispatcher SetupGuestGpu previously required. No `x360_gpu_host`
 * struct is passed from the x360rt side: every IGpuHost capability this
 * shim needs (address translation, MMIO replay, host threading, interrupt
 * relay, tick counting, logging) is derivable from `membase` alone or is
 * self-contained (host threads/timing), so there was nothing left for
 * x360rt to inject through the ABI.
 */
#pragma once

#include <cstdint>

extern "C" {

typedef void* x360_gpu_shim_handle;
typedef void (*x360_gpu_shim_interrupt_fn)(void* user_data, std::uint32_t source,
                                           std::uint32_t cpu);

/* Must equal 1 for this version of the ABI; gpu_backend_dynamic.c refuses
 * to use the shim otherwise. */
std::uint32_t x360_gpu_shim_abi_version(void);

/* `plugin_dir` is accepted for a future dlopen-based shim but unused today
 * (this shim links librexgpu-xenos.so directly at build time -- see
 * CMakeLists.txt); `backend` is "vulkan", "d3d12", or "any"
 * (rex::system::GpuCreateInfo::backend). Returns NULL on any failure
 * (ABI mismatch, plugin factory failure). */
x360_gpu_shim_handle x360_gpu_shim_create(const char* plugin_dir, const char* backend);

/* Builds a presentation provider + presenter and attaches it to `window`, a
 * live SDL_Window* supplied on SDL's main thread. The SDL window remains
 * owned and event-pumped by x360rt; the shim creates only the internal
 * rex::ui Window/Surface adapter needed by the presenter. It then wires the
 * guest GPU (IGraphicsSystem::SetupGuestGpu(IGpuHost*)) using `membase` (the
 * guest address space's host base pointer, i.e. x360_mem_base()) to resolve
 * guest physical addresses. Returns 0 even if SetupGuestGpu
 * fails (degrades to swap/shutdown-only, matching x360_gpu_backend_null's
 * always-succeeds contract); write_register/read_register/
 * initialize_ring_buffer/enable_read_pointer_write_back/
 * set_interrupt_callback report -1 in that case. */
std::int32_t x360_gpu_shim_initialize(x360_gpu_shim_handle handle, std::uint8_t* membase,
                                      void* window);

std::int32_t x360_gpu_shim_write_register(x360_gpu_shim_handle handle, std::uint32_t addr,
                                          std::uint32_t value);
std::int32_t x360_gpu_shim_read_register(x360_gpu_shim_handle handle, std::uint32_t addr,
                                         std::uint32_t* out_value);
std::int32_t x360_gpu_shim_initialize_ring_buffer(x360_gpu_shim_handle handle, std::uint32_t ptr,
                                                  std::uint32_t size_log2);
std::int32_t x360_gpu_shim_enable_read_pointer_write_back(x360_gpu_shim_handle handle,
                                                          std::uint32_t ptr,
                                                          std::uint32_t block_size_log2);
std::int32_t x360_gpu_shim_set_interrupt_callback(x360_gpu_shim_handle handle,
                                                  x360_gpu_shim_interrupt_fn cb, void* user_data);

/* Presents an empty frame via rex::ui::Presenter::RefreshGuestOutput, which
 * is self-contained (no window/swapchain touched -- see x360_gpu_shim.cpp).
 * `frontbuffer_ptr` is unused (the refresher callback writes nothing): it is
 * independent of the ring buffer's own XE_SWAP packet, which the command
 * processor now processes as part of normal ring buffer draining once
 * SetupGuestGpu has succeeded. */
std::int32_t x360_gpu_shim_swap(x360_gpu_shim_handle handle, std::uint32_t frontbuffer_ptr,
                                std::uint32_t width, std::uint32_t height);

void x360_gpu_shim_shutdown(x360_gpu_shim_handle handle);

}  // extern "C"
