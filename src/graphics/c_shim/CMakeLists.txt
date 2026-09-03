# rexgpu-xenos-cshim: standalone build for x360_gpu_shim.cpp, the C ABI
# adapter x360recomp's runtime/src/video/gpu_backend_dynamic.c dlopen()s
# (x360rt/gpu.h's dynamic GPU backend). src/graphics/CMakeLists.txt now also
# declares an in-tree `rexgpu-xenos-cshim` target that links the live
# rexruntime/rexgpu-xenos CMake targets instead of a pre-built out/ tree;
# prefer that when building through the SDK's own CMake configuration. This
# standalone project remains for building just the shim against an
# already-built SDK without reconfiguring the whole tree.
#
# Invoke from the rexglue-sdk repo root:
#   cmake -S src/graphics/c_shim -B out/build/c_shim-linux-amd64
#   cmake --build out/build/c_shim-linux-amd64
#
# Links directly against the already-built out/linux-amd64/librexruntime.so
# and out/linux-amd64/librexgpu-xenos.so (built by the SDK's own CMake
# presets beforehand) rather than dlopen'ing them: this project only
# declares the two headers it actually needs
# (include/rex/system/gpu_plugin.h, include/rex/ui/presenter.h) and the two
# extern "C" symbols documented in x360_gpu_shim.cpp's header comment.
cmake_minimum_required(VERSION 3.25)
project(rexgpu_xenos_cshim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REXGLUE_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(REXGLUE_SDK_OUT ${REXGLUE_SDK_ROOT}/out/linux-amd64)

if(NOT EXISTS ${REXGLUE_SDK_OUT}/librexruntime.so OR NOT EXISTS ${REXGLUE_SDK_OUT}/librexgpu-xenos.so)
    message(FATAL_ERROR
        "rexgpu-xenos-cshim: missing ${REXGLUE_SDK_OUT}/librexruntime.so or "
        "librexgpu-xenos.so -- build the rexglue-sdk 'linux-amd64' preset first.")
endif()

add_library(rexgpu-xenos-cshim SHARED x360_gpu_shim.cpp)
target_include_directories(rexgpu-xenos-cshim PRIVATE ${REXGLUE_SDK_ROOT}/include)
target_compile_options(rexgpu-xenos-cshim PRIVATE -Wall -Wextra -Werror)
target_link_libraries(rexgpu-xenos-cshim PRIVATE
    ${REXGLUE_SDK_OUT}/librexruntime.so
    ${REXGLUE_SDK_OUT}/librexgpu-xenos.so
)

# The two libraries above are not in a standard runtime search path (they
# live under the SDK's own out/ tree); rpath lets the built .so and anything
# dlopen()'ing it (x360rt's gpu_backend_dynamic.c) resolve them without
# LD_LIBRARY_PATH.
set_target_properties(rexgpu-xenos-cshim PROPERTIES
    BUILD_RPATH ${REXGLUE_SDK_OUT}
    INSTALL_RPATH ${REXGLUE_SDK_OUT}
)
