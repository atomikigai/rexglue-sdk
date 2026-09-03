/**
 * @file        tests/ppc/c_harness/generate_harness.cpp
 * @brief       Generates a standalone C11 and C++ harness pair that recompile
 *              a handful of PPC instruction tests through both rexglue
 *              codegen backends and compare REGISTER_OUT results.
 *
 * This is deliberately independent of tests/ppc's Catch2-based
 * recompile-tests pipeline (resources/templates/test, owned elsewhere): it
 * drives rex::codegen::AnalyzeTestBinary/FunctionNode::emitCpp directly and
 * hand-lists its test cases (mirroring tests/ppc/asm/instr_add.s,
 * seq_branch_carry.s and instr_vaddfp.s) instead of parsing REGISTER_IN/OUT
 * directives generically.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/codegen/binary_view.h>
#include <rex/codegen/codegen_context.h>
#include <rex/codegen/function_graph.h>
#include <rex/codegen/test_support.h>
#include <rex/cvar.h>
#include <rex/system/map_parser.h>

namespace codegen = rex::codegen;

namespace {

constexpr uint32_t kTestBaseAddress = 0x82010000;

struct RegSpec {
  std::string reg;
  std::string literal;                     // GPR/scalar: raw C/C++ literal text
  bool is_vector = false;
  std::array<std::string, 4> vec_hex_hi_lo{};  // [0]=high word .. [3]=low word, as in the .s file
};

struct TestCase {
  std::string stem;
  std::string label;
  std::vector<RegSpec> inputs;
  std::vector<RegSpec> outputs;
};

// Mirrors tests/ppc/asm/instr_add.s, seq_branch_carry.s, instr_vaddfp.s.
// Kept in sync by hand; a generic REGISTER_IN/OUT parser lives in
// src/rexglue/commands/test_recompiler.cpp (outside this node's ownership).
std::vector<TestCase> BuiltinTestCases() {
  std::vector<TestCase> cases;

  cases.push_back({"instr_add",
                   "test_add_1",
                   {{"r5", "0x00100000"}, {"r25", "0x0000FFFF"}},
                   {{"r5", "0x00100000"}, {"r25", "0x0000FFFF"}, {"r11", "0x0010FFFF"}}});

  cases.push_back({"instr_add",
                   "test_add_cr_5",
                   {{"r0", "-50"}, {"r25", "-25"}},
                   {{"r0", "-50"}, {"r25", "-25"}, {"r11", "-75"}, {"r12", "0x80000000"}}});

  cases.push_back({"seq_branch_carry",
                   "test_equiv_1",
                   {{"r11", "0x9E2A83C1"}},
                   {{"r7", "0xfffffffeffffffff"},
                    {"r8", "0xffffffff00000000"},
                    {"r9", "0xffffffff9e2a0000"},
                    {"r30", "0xffffffff9e2a83c1"},
                    {"r29", "0"},
                    {"r11", "0x000000009e2a83c1"},
                    {"r12", "1"}}});

  cases.push_back(
      {"instr_vaddfp",
       "test_vaddfp_1",
       {{"v3", "", true, {"41200000", "C1200000", "41700000", "C1700000"}},
        {"v4", "", true, {"C1200000", "41A00000", "C1A00000", "41F00000"}}},
       {{"v3", "", true, {"00000000", "41200000", "C0A00000", "41700000"}},
        {"v4", "", true, {"C1200000", "41A00000", "C1A00000", "41F00000"}}}});

  // --- Comparison (cmp/cmpi/cmpl/cmpli): src/codegen/builders/comparison.cpp
  // build_cmpd/cmpw/cmpld/cmplw/cmpdi/cmpwi/cmpldi/cmplwi, all via
  // emitCompareRegister/emitCompareImmediate (helpers.h). ---
  cases.push_back({"instr_cmp",
                   "test_cmpd_1",
                   {{"r3", "0x0000000100000000"}, {"r4", "0x0000000200000000"}},
                   {{"r3", "0x0000000100000000"}, {"r4", "0x0000000200000000"},
                    {"r12", "0x80000000"}}});
  cases.push_back({"instr_cmp",
                   "test_cmpw_1",
                   {{"r3", "0x0000000100000000"}, {"r4", "0x0000000200000000"}},
                   {{"r3", "0x0000000100000000"}, {"r4", "0x0000000200000000"},
                    {"r12", "0x20000000"}}});
  cases.push_back({"instr_cmpi",
                   "test_cmpdi_1",
                   {{"r3", "0x0000000100000000"}},
                   {{"r3", "0x0000000100000000"}, {"r12", "0x40000000"}}});
  cases.push_back({"instr_cmpi",
                   "test_cmpwi_1",
                   {{"r3", "0x0000000100000000"}},
                   {{"r3", "0x0000000100000000"}, {"r12", "0x80000000"}}});
  cases.push_back({"instr_cmpl",
                   "test_cmpld_1",
                   {{"r3", "0x0000000100000000"}, {"r4", "0x0000000200000000"}},
                   {{"r3", "0x0000000100000000"}, {"r4", "0x0000000200000000"},
                    {"r12", "0x80000000"}}});
  cases.push_back({"instr_cmpl",
                   "test_cmplw_1",
                   {{"r3", "0x0000000100000000"}, {"r4", "0x0000000200000000"}},
                   {{"r3", "0x0000000100000000"}, {"r4", "0x0000000200000000"},
                    {"r12", "0x20000000"}}});
  cases.push_back({"instr_cmpli",
                   "test_cmpldi_1",
                   {{"r3", "0x0000000100000000"}},
                   {{"r3", "0x0000000100000000"}, {"r12", "0x40000000"}}});
  cases.push_back({"instr_cmpli",
                   "test_cmplwi_1",
                   {{"r3", "0x0000000100000000"}},
                   {{"r3", "0x0000000100000000"}, {"r12", "0x80000000"}}});

  // --- fmadd/fnmsub (build_fmadd/build_fnmsub, floating_point.cpp): hex-bit
  // variants (raw REGISTER_IN/OUT, this harness's only supported style). ---
  cases.push_back({"instr_fmadd",
                   "test_fmadd_hex_1",
                   {{"f2", "0x4000000000000000"},
                    {"f3", "0x4008000000000000"},
                    {"f4", "0x3FF0000000000000"}},
                   {{"f1", "0x401C000000000000"},
                    {"f2", "0x4000000000000000"},
                    {"f3", "0x4008000000000000"},
                    {"f4", "0x3FF0000000000000"}}});
  cases.push_back({"instr_fnmsub",
                   "test_fnmsub_1",
                   {{"f2", "0x4000000000000000"},
                    {"f3", "0x4008000000000000"},
                    {"f4", "0x3FF0000000000000"}},
                   {{"f1", "0xC014000000000000"},
                    {"f2", "0x4000000000000000"},
                    {"f3", "0x4008000000000000"},
                    {"f4", "0x3FF0000000000000"}}});

  // --- fctiwz/fctidz (build_fctiwz/build_fctidz, floating_point.cpp: exercises
  // isnan() and the int64_t/double casts). ---
  cases.push_back({"instr_fctixz",
                   "test_fctiwz_5",
                   {{"f0", "0x3ff0000000000000"}},
                   {{"f0", "0x3ff0000000000000"}, {"f1", "0x0000000000000001"}}});
  cases.push_back({"instr_fctixz",
                   "test_fctiwz_7",
                   {{"f0", "0xc1e0000000000000"}},
                   {{"f0", "0xc1e0000000000000"}, {"f1", "0xFFFFFFFF80000000"}}});
  cases.push_back({"instr_fctixz",
                   "test_fctidz_5",
                   {{"f0", "0x3ff0000000000000"}},
                   {{"f0", "0x3ff0000000000000"}, {"f1", "0x0000000000000001"}}});
  cases.push_back({"instr_fctixz",
                   "test_fctidz_9",
                   {{"f0", "0x7ff0000000000000"}},
                   {{"f0", "0x7ff0000000000000"}, {"f1", "0x7fffffffffffffff"}}});

  // --- Variable vector shift (emit_vec_var_shift, context.cpp; build_vslh/
  // vsrh/vsrah, vector.cpp): exercises rexPpcNs()'s x360_simde_mm_{sllv,
  // srlv,srav}_epi16 free functions (pch_h_c.inja) vs rex::ppc::. ---
  cases.push_back(
      {"instr_vslh",
       "test_vslh_2",
       {{"v3", "", true, {"FFFFFFFF", "FFFFFFFF", "FFFFFFFF", "FFFFFFFF"}},
        {"v4", "", true, {"00010001", "00010001", "00010001", "00010001"}}},
       {{"v3", "", true, {"FFFEFFFE", "FFFEFFFE", "FFFEFFFE", "FFFEFFFE"}},
        {"v4", "", true, {"00010001", "00010001", "00010001", "00010001"}}}});
  cases.push_back(
      {"instr_vsrh",
       "test_vsrh_1",
       {{"v3", "", true, {"12345678", "87654321", "11223344", "55667788"}},
        {"v4", "", true, {"000D000D", "000D000D", "000D000D", "000D000D"}}},
       {{"v3", "", true, {"12345678", "87654321", "11223344", "55667788"}},
        {"v4", "", true, {"000D000D", "000D000D", "000D000D", "000D000D"}},
        {"v5", "", true, {"00000002", "00040002", "00000001", "00020003"}}}});
  cases.push_back(
      {"instr_vsrah",
       "test_vsrah_1",
       {{"v3", "", true, {"12345678", "87654321", "11223344", "55667788"}},
        {"v4", "", true, {"000D000D", "000D000D", "000D000D", "000D000D"}}},
       {{"v3", "", true, {"12345678", "87654321", "11223344", "55667788"}},
        {"v4", "", true, {"000D000D", "000D000D", "000D000D", "000D000D"}},
        {"v5", "", true, {"00000002", "FFFC0002", "00000001", "00020003"}}}});

  // --- Scalar D-form load/store (build_lwz/stw/lbz/std family, memory.cpp):
  // exercises REX_LOAD_U32/REX_STORE_U32/REX_LOAD_U8/REX_STORE_U8/
  // REX_LOAD_U64/REX_STORE_U64 through a real backing buffer (see
  // EmitMain's rex_test_mem), not just register-to-register moves. ---
  cases.push_back({"instr_ldst_scalar",
                   "test_ldst_1",
                   {},
                   {{"r5", "0x0000000012345678"},
                    {"r7", "0x00000000000000AB"},
                    {"r9", "0x1122334455667788"}}});

  return cases;
}

struct StemData {
  std::vector<uint8_t> bytes;
  std::map<size_t, std::string> symbols;  // address -> symbol name
};

StemData LoadStem(const std::string& bin_dir, const std::string& stem) {
  StemData data;
  std::ifstream file(fmt::format("{}/{}.bin", bin_dir, stem), std::ios::binary | std::ios::ate);
  if (!file) {
    fmt::print(stderr, "Failed to open {}/{}.bin\n", bin_dir, stem);
    std::exit(1);
  }
  auto size = file.tellg();
  file.seekg(0, std::ios::beg);
  data.bytes.resize(static_cast<size_t>(size));
  file.read(reinterpret_cast<char*>(data.bytes.data()), size);

  rex::runtime::MapParseOptions options;
  options.base_address = kTestBaseAddress;
  auto result = rex::runtime::ParseNmMap(fmt::format("{}/{}.map", bin_dir, stem), options);
  if (!result) {
    fmt::print(stderr, "Failed to parse {}/{}.map\n", bin_dir, stem);
    std::exit(1);
  }
  for (const auto& sym : *result) {
    if (!sym.name.empty() && sym.name[0] != '.')
      data.symbols[sym.address] = sym.name;
  }
  return data;
}

// Emits the shared per-test-case body (input setup, call, output checks) in
// either language. `ctx_dot` is "." for C++ (ctx is a value) and "." is also
// correct for C here: the harness owns `ctx` as a plain local struct in both
// languages (only *generated function bodies* see a reference/pointer).
void EmitMain(std::ostream& out, const std::vector<TestCase>& cases,
             const std::map<std::string, std::map<std::string, uint32_t>>& label_addr,
             bool is_c) {
  out << "int main(void) {\n";
  out << "  int failures = 0;\n";
  out << "  rex_test_mem = (uint8_t*)mmap(NULL, REX_TEST_MEM_SIZE, PROT_READ | PROT_WRITE, "
         "MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);\n";
  out << "  if (rex_test_mem == MAP_FAILED) { printf(\"mmap failed\\n\"); return 2; }\n";
  for (const auto& tc : cases) {
    uint32_t addr = label_addr.at(tc.stem).at(tc.label);
    std::string fn_name = fmt::format("{}_{:X}", tc.stem, addr);
    std::string test_id = fmt::format("{}.{}", tc.stem, tc.label);

    out << "  {\n";
    out << "    PPCContext ctx;\n";
    out << "    memset(&ctx, 0, sizeof(ctx));\n";
    // rex_test_mem is a single mmap'd (zero-filled) region reused across all
    // test cases: REX_RAW_ADDR adds the *raw* guest address (not translated
    // through a guest arena like the generic recompile-tests harness has) to
    // this pointer, so it must be sized/offset to cover any address a memory
    // test uses (see main()'s kRexTestMemBase/kRexTestMemSize). Only
    // instr_ldst_scalar currently touches memory and always writes before
    // reading, so no per-test reset is needed.
    // Reset the real host MXCSR to the default masked state before every
    // test. FPSCRRegister::enableFlushModeUnconditional() (include/rex/ppc/
    // context.h, outside this node's ownership; x360_fpscr_enable_flush_mode
    // in x360rt/ppc.h has the same shape) does `csr |= FlushMask;
    // setcsr(csr)` against its *cached* csr field, which starts at 0 for a
    // freshly memset PPCContext -- writing that to the real MXCSR clobbers
    // the exception-mask bits (7-12), unmasking FP exceptions process-wide
    // until something re-masks them. A prior test's flush-mode toggle can
    // therefore leave later, unrelated tests (e.g. an inexact
    // int64_t<->double conversion) to SIGFPE instead of producing Inf/NaN.
    // See findings for the InitHost()-before-first-use fix this papers over.
    out << "    simde_mm_setcsr(0x1F80u);\n";
    for (const auto& in : tc.inputs) {
      if (in.is_vector) {
        out << "    ctx." << in.reg << ".u32[3] = 0x" << in.vec_hex_hi_lo[0] << ";\n";
        out << "    ctx." << in.reg << ".u32[2] = 0x" << in.vec_hex_hi_lo[1] << ";\n";
        out << "    ctx." << in.reg << ".u32[1] = 0x" << in.vec_hex_hi_lo[2] << ";\n";
        out << "    ctx." << in.reg << ".u32[0] = 0x" << in.vec_hex_hi_lo[3] << ";\n";
      } else {
        out << "    ctx." << in.reg << ".u64 = " << in.literal << ";\n";
      }
    }
    if (is_c)
      out << "    " << fn_name << "(&ctx, rex_test_mem);\n";
    else
      out << "    " << fn_name << "(ctx, rex_test_mem);\n";
    for (const auto& o : tc.outputs) {
      if (o.is_vector) {
        for (int i = 0; i < 4; i++) {
          out << "    PRINTREG(\"" << test_id << "\", \"" << o.reg << ".u32[" << (3 - i)
              << "]\", ctx." << o.reg << ".u32[" << (3 - i) << "]);\n";
          out << "    if (ctx." << o.reg << ".u32[" << (3 - i) << "] != 0x" << o.vec_hex_hi_lo[i]
              << "u) { failures++; PRINTFAIL(\"" << test_id << "\", \"" << o.reg << ".u32["
              << (3 - i) << "]\"); }\n";
        }
      } else {
        out << "    PRINTREG(\"" << test_id << "\", \"" << o.reg << "\", ctx." << o.reg
            << ".u64);\n";
        out << "    if (ctx." << o.reg << ".u64 != (uint64_t)(" << o.literal << ")) { failures++; "
               "PRINTFAIL(\""
            << test_id << "\", \"" << o.reg << "\"); }\n";
      }
    }
    out << "  }\n";
  }
  out << "  if (failures) { printf(\"FAILURES: %d\\n\", failures); return 1; }\n";
  out << "  printf(\"ALL PASS\\n\");\n";
  out << "  return 0;\n";
  out << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string bin_dir, out_dir;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--bin-dir" && i + 1 < argc)
      bin_dir = argv[++i];
    else if (arg == "--out-dir" && i + 1 < argc)
      out_dir = argv[++i];
  }
  if (bin_dir.empty() || out_dir.empty()) {
    fmt::print(stderr, "usage: generate_harness --bin-dir DIR --out-dir DIR\n");
    return 2;
  }

  auto cases = BuiltinTestCases();
  std::vector<std::string> stems{"instr_add",       "seq_branch_carry", "instr_vaddfp",
                                 "instr_cmp",       "instr_cmpi",       "instr_cmpl",
                                 "instr_cmpli",     "instr_fmadd",      "instr_fnmsub",
                                 "instr_fctixz",    "instr_vslh",       "instr_vsrh",
                                 "instr_vsrah",     "instr_ldst_scalar"};

  std::map<std::string, std::map<std::string, uint32_t>> label_addr;
  std::string body_cpp, body_c;
  std::vector<std::string> referenced_names;  // for the C harness's forward decls

  for (const auto& stem : stems) {
    auto data = LoadStem(bin_dir, stem);

    codegen::TestModule module;
    module.Load(kTestBaseAddress, data.bytes.data(), data.bytes.size());
    module.set_name(stem);

    codegen::RecompilerConfig config;
    config.outDirectoryPath = out_dir;
    auto ctx = codegen::CodegenContext::Create(codegen::BinaryView::fromModule(module), config);
    codegen::AnalyzeTestBinary(ctx, stem, data.symbols, kTestBaseAddress, data.bytes.data(),
                               data.bytes.size());

    for (const auto& [addr, name] : data.symbols) {
      if (name.starts_with("test_"))
        label_addr[stem][name] = static_cast<uint32_t>(addr);
    }

    std::vector<const codegen::FunctionNode*> functions;
    for (const auto& [addr, node] : ctx.graph.functions())
      functions.push_back(node.get());
    std::sort(functions.begin(), functions.end(),
              [](const auto* a, const auto* b) { return a->base() < b->base(); });

    codegen::EmitContext emitCtx{ctx.binary(), ctx.Config(), ctx.graph, 0, nullptr};

    rex::cvar::SetFlagByName("codegen_language", "cpp");
    for (const auto* fn : functions) {
      body_cpp += fn->emitCpp(emitCtx);
      referenced_names.push_back(fmt::format("{}_{:X}", stem, fn->base()));
    }
    rex::cvar::SetFlagByName("codegen_language", "c");
    for (const auto* fn : functions) {
      body_c += fn->emitCpp(emitCtx);
    }
    rex::cvar::SetFlagByName("codegen_language", "cpp");  // restore default
  }

  // --- C++ harness ---
  {
    std::ofstream out(fmt::format("{}/harness_cpp.cpp", out_dir));
    out << "// Generated by tests/ppc/c_harness/generate_harness.cpp - DO NOT EDIT\n";
    out << "#include <cstdint>\n#include <cstdio>\n#include <cstring>\n#include <sys/mman.h>\n";
    out << "#include <simde/x86/avx.h>\n#include <simde/x86/avx2.h>\n#include <simde/x86/sse.h>\n#include <simde/x86/sse4.1.h>\n";
    out << "#include <rex/ppc/context.h>\n";
    out << "#include <rex/ppc/intrinsics.h>\n";
    out << "#define REX_FUNC_PROLOGUE() ((void)0)\n";
    out << "#ifndef DEFINE_REX_FUNC\n";
    out << "#define DEFINE_REX_FUNC(name) void name(PPCContext& ctx, uint8_t* base)\n";
    out << "#endif\n";
    out << "#define PRINTREG(test, reg, val) printf(\"REG %s %s %016llx\\n\", test, reg, "
           "(unsigned long long)(val))\n";
    out << "#define PRINTFAIL(test, reg) printf(\"FAIL %s %s\\n\", test, reg)\n";
    out << "using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t; using u64 = "
           "uint64_t;\n";
    // REX_LOAD_*/REX_STORE_*/REX_RAW_ADDR: mirrors
    // resources/templates/codegen/pch_h.inja's Memory Access section
    // (non-MMIO path only; this harness's memory tests never hit MMIO
    // addresses). base is a real backing buffer (rex_test_mem below), not
    // guest memory, so no MMIO branch is needed.
    out << "#define REX_RAW_ADDR(x) (base + (u32)(x))\n";
    out << "#define REX_LOAD_U8(x) (*(volatile u8*)(base + (u32)(x)))\n";
    out << "#define REX_LOAD_U16(x) __builtin_bswap16(*(volatile u16*)(base + (u32)(x)))\n";
    out << "#define REX_LOAD_U32(x) __builtin_bswap32(*(volatile u32*)(base + (u32)(x)))\n";
    out << "#define REX_LOAD_U64(x) __builtin_bswap64(*(volatile u64*)(base + (u32)(x)))\n";
    out << "#define REX_STORE_U8(x, y) (*(volatile u8*)(base + (u32)(x)) = (y))\n";
    out << "#define REX_STORE_U16(x, y) (*(volatile u16*)(base + (u32)(x)) = "
           "__builtin_bswap16(y))\n";
    out << "#define REX_STORE_U32(x, y) (*(volatile u32*)(base + (u32)(x)) = "
           "__builtin_bswap32(y))\n";
    out << "#define REX_STORE_U64(x, y) (*(volatile u64*)(base + (u32)(x)) = "
           "__builtin_bswap64(y))\n";
    out << "static uint8_t* rex_test_mem;\n";
out << "#define REX_TEST_MEM_SIZE 0x20000000u /* 512MB: covers guest addrs used by\n"
          "                                       * memory tests (see instr_ldst_scalar.s) */\n";
    out << body_cpp << "\n";
    EmitMain(out, cases, label_addr, /*is_c=*/false);
  }

  // --- C harness ---
  {
    std::ofstream out(fmt::format("{}/harness_c.c", out_dir));
    out << "/* Generated by tests/ppc/c_harness/generate_harness.cpp - DO NOT EDIT */\n";
    out << "#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include <sys/mman.h>\n";
    out << "#include <simde/x86/avx.h>\n#include <simde/x86/avx2.h>\n#include <simde/x86/sse.h>\n#include <simde/x86/sse4.1.h>\n";
    out << "#include <x360rt/ppc.h>\n";
    out << "#define REX_FUNC_PROLOGUE() ((void)0)\n";
    out << "#ifndef DEFINE_REX_FUNC\n";
    out << "#define DEFINE_REX_FUNC(name) void name(PPCContext* restrict ctx, u8* base)\n";
    out << "#endif\n";
    out << "#define PRINTREG(test, reg, val) printf(\"REG %s %s %016llx\\n\", test, reg, "
           "(unsigned long long)(val))\n";
    out << "#define PRINTFAIL(test, reg) printf(\"FAIL %s %s\\n\", test, reg)\n";
    out << "/* x360_* runtime hooks: minimal stand-ins for this harness only. Real signatures/\n"
           " * bodies live in resources/templates/codegen/pch_h_c.inja (s32/u32 defined there;\n"
           " * s64/u64/f64 mirror x360rt/ppc.h, which this minimal stub doesn't include). */\n";
    out << "static void x360_cr_compare_s32(PPCCRRegister* cr, s32 left, s32 right, const "
           "PPCXERRegister* xer) {\n"
           "  cr->lt = left < right; cr->gt = left > right; cr->eq = left == right; cr->so = "
           "xer->so;\n}\n";
    out << "static void x360_cr_compare_u32(PPCCRRegister* cr, u32 left, u32 right, const "
           "PPCXERRegister* xer) {\n"
           "  cr->lt = left < right; cr->gt = left > right; cr->eq = left == right; cr->so = "
           "xer->so;\n}\n";
    out << "static void x360_cr_compare_s64(PPCCRRegister* cr, s64 left, s64 right, const "
           "PPCXERRegister* xer) {\n"
           "  cr->lt = left < right; cr->gt = left > right; cr->eq = left == right; cr->so = "
           "xer->so;\n}\n";
    out << "static void x360_cr_compare_u64(PPCCRRegister* cr, u64 left, u64 right, const "
           "PPCXERRegister* xer) {\n"
           "  cr->lt = left < right; cr->gt = left > right; cr->eq = left == right; cr->so = "
           "xer->so;\n}\n";
    out << "static void x360_cr_compare_f64(PPCCRRegister* cr, double left, double right) {\n"
           "  int un = __builtin_isnan(left) || __builtin_isnan(right);\n"
           "  cr->lt = !un && left < right; cr->gt = !un && left > right; cr->eq = !un && left "
           "== right; cr->so = (u8)un;\n}\n";
    out << "static void x360_fpscr_enable_flush_mode(PPCFPSCRRegister* fpscr) { (void)fpscr; }\n";
    out << "static void x360_fpscr_enable_flush_mode_unconditional(PPCFPSCRRegister* fpscr) { "
           "(void)fpscr; }\n";
    out << "static void x360_fpscr_disable_flush_mode(PPCFPSCRRegister* fpscr) { (void)fpscr; }\n";
    out << "static void x360_fpscr_disable_flush_mode_unconditional(PPCFPSCRRegister* fpscr) { "
           "(void)fpscr; }\n";
    // REX_LOAD_*/REX_STORE_*/REX_RAW_ADDR: see the matching C++ harness block above.
    out << "#define REX_RAW_ADDR(x) (base + (u32)(x))\n";
    out << "#define REX_LOAD_U8(x) (*(volatile u8*)(base + (u32)(x)))\n";
    out << "#define REX_LOAD_U16(x) __builtin_bswap16(*(volatile u16*)(base + (u32)(x)))\n";
    out << "#define REX_LOAD_U32(x) __builtin_bswap32(*(volatile u32*)(base + (u32)(x)))\n";
    out << "#define REX_LOAD_U64(x) __builtin_bswap64(*(volatile u64*)(base + (u32)(x)))\n";
    out << "#define REX_STORE_U8(x, y) (*(volatile u8*)(base + (u32)(x)) = (y))\n";
    out << "#define REX_STORE_U16(x, y) (*(volatile u16*)(base + (u32)(x)) = "
           "__builtin_bswap16(y))\n";
    out << "#define REX_STORE_U32(x, y) (*(volatile u32*)(base + (u32)(x)) = "
           "__builtin_bswap32(y))\n";
    out << "#define REX_STORE_U64(x, y) (*(volatile u64*)(base + (u32)(x)) = "
           "__builtin_bswap64(y))\n";
    out << "static uint8_t* rex_test_mem;\n";
out << "#define REX_TEST_MEM_SIZE 0x20000000u /* 512MB: covers guest addrs used by\n"
          "                                       * memory tests (see instr_ldst_scalar.s) */\n";
    // x360_simde_mm_{sllv,srlv,srav}_epi16: same widen/shift/narrow port as
    // resources/templates/codegen/pch_h_c.inja (mirrors
    // include/rex/ppc/intrinsics.h's rex::ppc:: originals).
    out << "static simde__m128i x360_simde_mm_sllv_epi16(simde__m128i a, simde__m128i count) {\n"
           "  simde__m128i zero = simde_mm_setzero_si128();\n"
           "  simde__m128i a_lo = simde_mm_unpacklo_epi16(a, zero);\n"
           "  simde__m128i a_hi = simde_mm_unpackhi_epi16(a, zero);\n"
           "  simde__m128i s_lo = simde_mm_unpacklo_epi16(count, zero);\n"
           "  simde__m128i s_hi = simde_mm_unpackhi_epi16(count, zero);\n"
           "  simde__m128i r_lo = simde_mm_sllv_epi32(a_lo, s_lo);\n"
           "  simde__m128i r_hi = simde_mm_sllv_epi32(a_hi, s_hi);\n"
           "  simde__m128i mask16 = simde_mm_set1_epi32(0xFFFF);\n"
           "  r_lo = simde_mm_and_si128(r_lo, mask16);\n"
           "  r_hi = simde_mm_and_si128(r_hi, mask16);\n"
           "  return simde_mm_packus_epi32(r_lo, r_hi);\n}\n";
    out << "static simde__m128i x360_simde_mm_srlv_epi16(simde__m128i a, simde__m128i count) {\n"
           "  simde__m128i zero = simde_mm_setzero_si128();\n"
           "  simde__m128i a_lo = simde_mm_unpacklo_epi16(a, zero);\n"
           "  simde__m128i a_hi = simde_mm_unpackhi_epi16(a, zero);\n"
           "  simde__m128i s_lo = simde_mm_unpacklo_epi16(count, zero);\n"
           "  simde__m128i s_hi = simde_mm_unpackhi_epi16(count, zero);\n"
           "  simde__m128i r_lo = simde_mm_srlv_epi32(a_lo, s_lo);\n"
           "  simde__m128i r_hi = simde_mm_srlv_epi32(a_hi, s_hi);\n"
           "  return simde_mm_packus_epi32(r_lo, r_hi);\n}\n";
    out << "static simde__m128i x360_simde_mm_srav_epi16(simde__m128i a, simde__m128i count) {\n"
           "  simde__m128i zero = simde_mm_setzero_si128();\n"
           "  simde__m128i a_lo = simde_mm_srai_epi32(simde_mm_unpacklo_epi16(a, a), 16);\n"
           "  simde__m128i a_hi = simde_mm_srai_epi32(simde_mm_unpackhi_epi16(a, a), 16);\n"
           "  simde__m128i s_lo = simde_mm_unpacklo_epi16(count, zero);\n"
           "  simde__m128i s_hi = simde_mm_unpackhi_epi16(count, zero);\n"
           "  simde__m128i r_lo = simde_mm_srav_epi32(a_lo, s_lo);\n"
           "  simde__m128i r_hi = simde_mm_srav_epi32(a_hi, s_hi);\n"
           "  return simde_mm_packs_epi32(r_lo, r_hi);\n}\n";
    out << "static simde__m128i x360_simde_mm_sllv_epi8(simde__m128i a, simde__m128i count) {\n"
           "  simde__m128i zero = simde_mm_setzero_si128();\n"
           "  simde__m128i a_lo = simde_mm_unpacklo_epi8(a, zero);\n"
           "  simde__m128i a_hi = simde_mm_unpackhi_epi8(a, zero);\n"
           "  simde__m128i s_lo = simde_mm_unpacklo_epi8(count, zero);\n"
           "  simde__m128i s_hi = simde_mm_unpackhi_epi8(count, zero);\n"
           "  simde__m128i r_lo = x360_simde_mm_sllv_epi16(a_lo, s_lo);\n"
           "  simde__m128i r_hi = x360_simde_mm_sllv_epi16(a_hi, s_hi);\n"
           "  simde__m128i mask8 = simde_mm_set1_epi16(0xFF);\n"
           "  r_lo = simde_mm_and_si128(r_lo, mask8);\n"
           "  r_hi = simde_mm_and_si128(r_hi, mask8);\n"
           "  return simde_mm_packus_epi16(r_lo, r_hi);\n}\n";
    out << body_c << "\n";
    EmitMain(out, cases, label_addr, /*is_c=*/true);
  }

  fmt::print("Generated {}/harness_cpp.cpp and {}/harness_c.c ({} test cases, {} functions)\n",
            out_dir, out_dir, cases.size(), referenced_names.size());
  return 0;
}
