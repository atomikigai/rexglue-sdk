# tests/ppc/c_harness/CMakeLists.txt
#
# Independent verification for the C11 codegen backend (rexglue codegen
# --lang c). Recompiles a handful of PPC instruction tests through both
# backends directly (rex::codegen::AnalyzeTestBinary + FunctionNode::emitCpp),
# compiles each result with its own toolchain (clang -std=gnu11 for C, the
# project's C++ standard for C++), runs both, and diffs their REGISTER_OUT
# dumps. Independent of tests/ppc's Catch2-based recompile-tests pipeline
# (resources/templates/test, owned elsewhere).
#
# Reuses the .bin/.map artifacts already produced by ppc_add_test_binary()
# for the .s stems in PPC_C_HARNESS_STEMS below (all part of the same
# tests/ppc/asm/*.s corpus the parent tests/ppc/CMakeLists.txt assembles).

set(PPC_C_HARNESS_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PPC_C_HARNESS_GEN_DIR})

add_executable(ppc_c_harness_gen generate_harness.cpp)
target_link_libraries(ppc_c_harness_gen PRIVATE rex::codegen rexruntime fmt::fmt)

# Depend on the specific .bin/.map pairs this harness recompiles, not the
# whole PPC_TEST_BINS list, so unrelated test files don't force regeneration.
set(PPC_C_HARNESS_STEMS
    instr_add seq_branch_carry instr_vaddfp
    instr_cmp instr_cmpi instr_cmpl instr_cmpli
    instr_fmadd instr_fnmsub instr_fctixz
    instr_vslh instr_vsrh instr_vsrah
    instr_ldst_scalar)
set(PPC_C_HARNESS_INPUTS "")
foreach(STEM ${PPC_C_HARNESS_STEMS})
    list(APPEND PPC_C_HARNESS_INPUTS
        ${PPC_BIN_DIR}/${STEM}.bin
        ${PPC_BIN_DIR}/${STEM}.map)
endforeach()

add_custom_command(
    OUTPUT ${PPC_C_HARNESS_GEN_DIR}/harness_cpp.cpp ${PPC_C_HARNESS_GEN_DIR}/harness_c.c
    COMMAND ppc_c_harness_gen --bin-dir ${PPC_BIN_DIR} --out-dir ${PPC_C_HARNESS_GEN_DIR}
    DEPENDS ppc_c_harness_gen ${PPC_C_HARNESS_INPUTS}
    COMMENT "Recompiling PPC C backend verification harness (C++ and C11)"
    VERBATIM
)

add_custom_target(ppc_c_harness_gen_run
    DEPENDS ${PPC_C_HARNESS_GEN_DIR}/harness_cpp.cpp ${PPC_C_HARNESS_GEN_DIR}/harness_c.c
)

# --- C++ reference build: real project headers (include/rex/ppc/context.h) ---
add_executable(ppc_c_harness_cpp ${PPC_C_HARNESS_GEN_DIR}/harness_cpp.cpp)
add_dependencies(ppc_c_harness_cpp ppc_c_harness_gen_run)
set_source_files_properties(${PPC_C_HARNESS_GEN_DIR}/harness_cpp.cpp PROPERTIES GENERATED TRUE)
target_include_directories(ppc_c_harness_cpp PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ppc_c_harness_cpp PRIVATE simde rexruntime fmt::fmt)
target_compile_options(ppc_c_harness_cpp PRIVATE -msse4.1 -mssse3)

# --- C11 build: this node's own minimal x360rt/ppc.h stand-in (see its
# header comment) plus a handful of hand-written x360_* runtime hook stubs
# emitted directly into harness_c.c by the generator. ---
add_executable(ppc_c_harness_c ${PPC_C_HARNESS_GEN_DIR}/harness_c.c)
add_dependencies(ppc_c_harness_c ppc_c_harness_gen_run)
set_source_files_properties(${PPC_C_HARNESS_GEN_DIR}/harness_c.c PROPERTIES
    GENERATED TRUE
    LANGUAGE C
)
target_include_directories(ppc_c_harness_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# -lm: isnan()/fma()/sqrt() (src/codegen/builders/floating_point.cpp's C-mode
# output) aren't always inlined at -march=x86-64-v2 (no native FMA3), so they
# fall back to real libm calls. Real generated C projects need this too (see
# findings): their CMake glue lives outside this node's ownership.
target_link_libraries(ppc_c_harness_c PRIVATE simde m)
target_compile_options(ppc_c_harness_c PRIVATE -std=gnu11 -msse4.1 -mssse3)
set_target_properties(ppc_c_harness_c PROPERTIES LINKER_LANGUAGE C)

# --- Parity check: run both, diff their REGISTER_OUT dumps ---
add_custom_command(
    OUTPUT ${PPC_C_HARNESS_GEN_DIR}/parity.stamp
    COMMAND $<TARGET_FILE:ppc_c_harness_cpp> > ${PPC_C_HARNESS_GEN_DIR}/out_cpp.txt
    COMMAND $<TARGET_FILE:ppc_c_harness_c> > ${PPC_C_HARNESS_GEN_DIR}/out_c.txt
    COMMAND ${CMAKE_COMMAND} -E compare_files
        ${PPC_C_HARNESS_GEN_DIR}/out_cpp.txt ${PPC_C_HARNESS_GEN_DIR}/out_c.txt
    COMMAND ${CMAKE_COMMAND} -E touch ${PPC_C_HARNESS_GEN_DIR}/parity.stamp
    DEPENDS ppc_c_harness_cpp ppc_c_harness_c
    COMMENT "Comparing C++ vs C11 backend REGISTER_OUT dumps"
    VERBATIM
)
add_custom_target(ppc_c_backend_parity ALL DEPENDS ${PPC_C_HARNESS_GEN_DIR}/parity.stamp)

add_test(NAME ppc_c_backend_parity
    COMMAND ${CMAKE_COMMAND} -E compare_files
        ${PPC_C_HARNESS_GEN_DIR}/out_cpp.txt ${PPC_C_HARNESS_GEN_DIR}/out_c.txt
)
set_tests_properties(ppc_c_backend_parity PROPERTIES
    LABELS "ppc;codegen-c-backend"
)
