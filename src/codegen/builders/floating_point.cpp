/**
 * @file        rexcodegen/builders/floating_point.cpp
 * @brief       PPC floating point instruction code generation
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "builder_context.h"
#include "helpers.h"

namespace rex::codegen {

namespace {

/// `double(float(expr))` in C++, `(double)((float)(expr))` in C. Rounds
/// through single precision, matching the guest's single-precision FP ops.
std::string doubleOfFloat(BuilderContext& ctx, std::string_view expr) {
  return numCast(ctx, "double", numCast(ctx, "float", expr));
}

}  // namespace

//=============================================================================
// Sign Manipulation
//=============================================================================

bool build_fabs(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.u64 = {}.u64 & ~0x8000000000000000;", ctx.f(ctx.insn.operands[0]),
              ctx.f(ctx.insn.operands[1]));
  return true;
}

bool build_fnabs(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.u64 = {}.u64 | 0x8000000000000000;", ctx.f(ctx.insn.operands[0]),
              ctx.f(ctx.insn.operands[1]));
  return true;
}

bool build_fneg(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.u64 = {}.u64 ^ 0x8000000000000000;", ctx.f(ctx.insn.operands[0]),
              ctx.f(ctx.insn.operands[1]));
  return true;
}

//=============================================================================
// Move and Conversion
//=============================================================================

bool build_fmr(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = {}.f64;", ctx.f(ctx.insn.operands[0]), ctx.f(ctx.insn.operands[1]));
  return true;
}

bool build_fcfid(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  ctx.println("\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
              numCast(ctx, "double", fmt::format("{}.s64", a)));
  return true;
}

bool build_fctid(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println(
      "\t{0}.s64 = {2}isnan({1}.f64) ? {3} : "
      "({1}.f64 > {4}) ? LLONG_MAX : "
      "simde_mm_cvtsd_si64(simde_mm_load_sd(&{1}.f64));",
      ctx.f(ctx.insn.operands[0]), ctx.f(ctx.insn.operands[1]), ctx.cppNs(),
      numCast(ctx, "int64_t", "0x8000000000000000ULL"), numCast(ctx, "double", "LLONG_MAX"));
  return true;
}

bool build_fctidz(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println(
      "\t{0}.s64 = {2}isnan({1}.f64) ? {3} : "
      "({1}.f64 > {4}) ? LLONG_MAX : "
      "simde_mm_cvttsd_si64(simde_mm_load_sd(&{1}.f64));",
      ctx.f(ctx.insn.operands[0]), ctx.f(ctx.insn.operands[1]), ctx.cppNs(),
      numCast(ctx, "int64_t", "0x8000000000000000ULL"), numCast(ctx, "double", "LLONG_MAX"));
  return true;
}

bool build_fctiw(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println(
      "\t{0}.s64 = {2}isnan({1}.f64) ? {3} : "
      "({1}.f64 >= {4}) ? INT_MAX : "
      "simde_mm_cvtsd_si32(simde_mm_load_sd(&{1}.f64));",
      ctx.f(ctx.insn.operands[0]), ctx.f(ctx.insn.operands[1]), ctx.cppNs(),
      numCast(ctx, "int64_t", "0x80000000U"), numCast(ctx, "double", "INT_MAX"));
  return true;
}

bool build_fctiwz(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println(
      "\t{0}.s64 = {2}isnan({1}.f64) ? {3} : "
      "({1}.f64 >= {4}) ? INT_MAX : "
      "simde_mm_cvttsd_si32(simde_mm_load_sd(&{1}.f64));",
      ctx.f(ctx.insn.operands[0]), ctx.f(ctx.insn.operands[1]), ctx.cppNs(),
      numCast(ctx, "int64_t", "0x80000000U"), numCast(ctx, "double", "INT_MAX"));
  return true;
}

bool build_frsp(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  ctx.println("\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
              doubleOfFloat(ctx, fmt::format("{}.f64", a)));
  return true;
}

//=============================================================================
// Comparison
//=============================================================================

bool build_fcmpu(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto cr = ctx.cr(ctx.insn.operands[0]);
  auto a = ctx.f(ctx.insn.operands[1]);
  auto b = ctx.f(ctx.insn.operands[2]);
  if (ctx.language() == Language::C) {
    // CRRegister::compare(double, double) is a C++ member overload; the C
    // backend calls the free-function equivalent (defined in the C PCH,
    // matching CRRegister's un/lt/gt/eq semantics from
    // include/rex/ppc/context.h).
    ctx.println("\tx360_cr_compare_f64(&{}, {}.f64, {}.f64);", cr, a, b);
  } else {
    ctx.println("\t{}.compare({}.f64, {}.f64);", cr, a, b);
  }
  return true;
}

bool build_fcmpo(BuilderContext& ctx) {
  // fcmpo is identical to fcmpu for recompilation purposes.
  // The difference is that fcmpo sets FPSCR exception flags for SNaN operands,
  // which we don't need to emulate.
  return build_fcmpu(ctx);
}

//=============================================================================
// Addition
//=============================================================================

bool build_fadd(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = {}.f64 + {}.f64;", ctx.f(ctx.insn.operands[0]),
              ctx.f(ctx.insn.operands[1]), ctx.f(ctx.insn.operands[2]));
  return true;
}

bool build_fadds(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  auto b = ctx.f(ctx.insn.operands[2]);
  ctx.println("\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
              doubleOfFloat(ctx, fmt::format("{}.f64 + {}.f64", a, b)));
  return true;
}

//=============================================================================
// Subtraction
//=============================================================================

bool build_fsub(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = {}.f64 - {}.f64;", ctx.f(ctx.insn.operands[0]),
              ctx.f(ctx.insn.operands[1]), ctx.f(ctx.insn.operands[2]));
  return true;
}

bool build_fsubs(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  auto b = ctx.f(ctx.insn.operands[2]);
  ctx.println("\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
              doubleOfFloat(ctx, fmt::format("{}.f64 - {}.f64", a, b)));
  return true;
}

//=============================================================================
// Multiplication
//=============================================================================

bool build_fmul(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = {}.f64 * {}.f64;", ctx.f(ctx.insn.operands[0]),
              ctx.f(ctx.insn.operands[1]), ctx.f(ctx.insn.operands[2]));
  return true;
}

bool build_fmuls(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  auto b = ctx.f(ctx.insn.operands[2]);
  ctx.println("\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
              doubleOfFloat(ctx, fmt::format("{}.f64 * {}.f64", a, b)));
  return true;
}

//=============================================================================
// Division
//=============================================================================

bool build_fdiv(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = {}.f64 / {}.f64;", ctx.f(ctx.insn.operands[0]),
              ctx.f(ctx.insn.operands[1]), ctx.f(ctx.insn.operands[2]));
  return true;
}

bool build_fdivs(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  auto b = ctx.f(ctx.insn.operands[2]);
  ctx.println("\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
              doubleOfFloat(ctx, fmt::format("{}.f64 / {}.f64", a, b)));
  return true;
}

//=============================================================================
// Fused Multiply-Add
//=============================================================================

bool build_fmadd(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = {}fma({}.f64, {}.f64, {}.f64);", ctx.f(ctx.insn.operands[0]),
              ctx.cppNs(), ctx.f(ctx.insn.operands[1]), ctx.f(ctx.insn.operands[2]),
              ctx.f(ctx.insn.operands[3]));
  return true;
}

bool build_fmadds(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  auto b = ctx.f(ctx.insn.operands[2]);
  auto c = ctx.f(ctx.insn.operands[3]);
  ctx.println(
      "\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
      doubleOfFloat(ctx, fmt::format("{}fma({}.f64, {}.f64, {}.f64)", ctx.cppNs(), a, b, c)));
  return true;
}

bool build_fmsub(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = {}fma({}.f64, {}.f64, -{}.f64);", ctx.f(ctx.insn.operands[0]),
              ctx.cppNs(), ctx.f(ctx.insn.operands[1]), ctx.f(ctx.insn.operands[2]),
              ctx.f(ctx.insn.operands[3]));
  return true;
}

bool build_fmsubs(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  auto b = ctx.f(ctx.insn.operands[2]);
  auto c = ctx.f(ctx.insn.operands[3]);
  ctx.println(
      "\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
      doubleOfFloat(ctx, fmt::format("{}fma({}.f64, {}.f64, -{}.f64)", ctx.cppNs(), a, b, c)));
  return true;
}

bool build_fnmadd(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = -{}fma({}.f64, {}.f64, {}.f64);", ctx.f(ctx.insn.operands[0]),
              ctx.cppNs(), ctx.f(ctx.insn.operands[1]), ctx.f(ctx.insn.operands[2]),
              ctx.f(ctx.insn.operands[3]));
  return true;
}

bool build_fnmadds(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  auto b = ctx.f(ctx.insn.operands[2]);
  auto c = ctx.f(ctx.insn.operands[3]);
  ctx.println(
      "\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
      doubleOfFloat(ctx, fmt::format("-{}fma({}.f64, {}.f64, {}.f64)", ctx.cppNs(), a, b, c)));
  return true;
}

bool build_fnmsub(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = -{}fma({}.f64, {}.f64, -{}.f64);", ctx.f(ctx.insn.operands[0]),
              ctx.cppNs(), ctx.f(ctx.insn.operands[1]), ctx.f(ctx.insn.operands[2]),
              ctx.f(ctx.insn.operands[3]));
  return true;
}

bool build_fnmsubs(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  auto b = ctx.f(ctx.insn.operands[2]);
  auto c = ctx.f(ctx.insn.operands[3]);
  ctx.println(
      "\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
      doubleOfFloat(ctx, fmt::format("-{}fma({}.f64, {}.f64, -{}.f64)", ctx.cppNs(), a, b, c)));
  return true;
}

//=============================================================================
// Reciprocal and Square Root
//=============================================================================

bool build_fres(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  ctx.println("\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
              doubleOfFloat(ctx, fmt::format("1.0 / {}.f64", a)));
  return true;
}

bool build_frsqrte(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  ctx.println("\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
              doubleOfFloat(ctx, fmt::format("1.0 / sqrt({}.f64)", a)));
  return true;
}

bool build_fsqrt(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = sqrt({}.f64);", ctx.f(ctx.insn.operands[0]), ctx.f(ctx.insn.operands[1]));
  return true;
}

bool build_fsqrts(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  auto a = ctx.f(ctx.insn.operands[1]);
  ctx.println("\t{}.f64 = {};", ctx.f(ctx.insn.operands[0]),
              doubleOfFloat(ctx, fmt::format("sqrt({}.f64)", a)));
  return true;
}

//=============================================================================
// Selection
//=============================================================================

bool build_fsel(BuilderContext& ctx) {
  ctx.emit_set_flush_mode(false);
  ctx.println("\t{}.f64 = {}.f64 >= 0.0 ? {}.f64 : {}.f64;", ctx.f(ctx.insn.operands[0]),
              ctx.f(ctx.insn.operands[1]), ctx.f(ctx.insn.operands[2]),
              ctx.f(ctx.insn.operands[3]));
  return true;
}

}  // namespace rex::codegen
