/**
 * @file        rexcodegen/builders/arithmetic.cpp
 * @brief       PPC arithmetic instruction code generation
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

//=============================================================================
// Addition
//=============================================================================

bool build_add(BuilderContext& ctx) {
  ctx.println("\t{}.u64 = {}.u64 + {}.u64;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[2]));
  emitRecordFormCompare(ctx);
  return true;
}

bool build_adde(BuilderContext& ctx) {
  ctx.println("\t{}.u8 = ({}.u32 + {}.u32 < {}.u32) | ({}.u32 + {}.u32 + {}.ca < {}.ca);",
              ctx.temp(), ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[2]),
              ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[2]),
              ctx.xer(), ctx.xer());
  ctx.println("\t{}.u64 = {}.u64 + {}.u64 + {}.ca;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[2]), ctx.xer());
  ctx.println("\t{}.ca = {}.u8;", ctx.xer(), ctx.temp());
  emitRecordFormCompare(ctx);
  return true;
}

bool build_addi(BuilderContext& ctx) {
  ctx.print("\t{}.s64 = ", ctx.r(ctx.insn.operands[0]));
  if (ctx.insn.operands[1] != 0)
    ctx.print("{}.s64 + ", ctx.r(ctx.insn.operands[1]));
  ctx.println("{};", static_cast<int32_t>(ctx.insn.operands[2]));
  return true;
}

bool build_addic(BuilderContext& ctx) {
  ctx.println("\t{}.ca = {}.u32 > {};", ctx.xer(), ctx.r(ctx.insn.operands[1]),
              ~ctx.insn.operands[2]);
  ctx.println("\t{}.s64 = {}.s64 + {};", ctx.r(ctx.insn.operands[0]), ctx.r(ctx.insn.operands[1]),
              static_cast<int32_t>(ctx.insn.operands[2]));
  emitRecordFormCompare(ctx);
  return true;
}

bool build_addis(BuilderContext& ctx) {
  ctx.print("\t{}.s64 = ", ctx.r(ctx.insn.operands[0]));
  if (ctx.insn.operands[1] != 0)
    ctx.print("{}.s64 + ", ctx.r(ctx.insn.operands[1]));
  ctx.println("{};", static_cast<int32_t>(ctx.insn.operands[2] << 16));
  return true;
}

bool build_addze(BuilderContext& ctx) {
  ctx.println("\t{}.s64 = {}.s64 + {}.ca;", ctx.temp(), ctx.r(ctx.insn.operands[1]), ctx.xer());
  ctx.println("\t{}.ca = {}.u32 < {}.u32;", ctx.xer(), ctx.temp(), ctx.r(ctx.insn.operands[1]));
  ctx.println("\t{}.s64 = {}.s64;", ctx.r(ctx.insn.operands[0]), ctx.temp());
  emitRecordFormCompare(ctx);
  return true;
}

bool build_addme(BuilderContext& ctx) {
  // addme: rD = rA + CA - 1 (which is rA + CA + 0xFFFFFFFFFFFFFFFF)
  ctx.println("\t{}.u8 = ({}.u32 + 0xFFFFFFFFu < {}.u32) | ({}.u32 + 0xFFFFFFFFu + {}.ca < {}.ca);",
              ctx.temp(), ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[1]),
              ctx.r(ctx.insn.operands[1]), ctx.xer(), ctx.xer());
  ctx.println("\t{}.u64 = {}.u64 + {}.ca + 0xFFFFFFFFFFFFFFFFull;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[1]), ctx.xer());
  ctx.println("\t{}.ca = {}.u8;", ctx.xer(), ctx.temp());
  emitRecordFormCompare(ctx);
  return true;
}

bool build_addc(BuilderContext& ctx) {
  // addc: rD = rA + rB, CA = carry out
  ctx.println("\t{}.ca = {}.u32 + {}.u32 < {}.u32;", ctx.xer(), ctx.r(ctx.insn.operands[1]),
              ctx.r(ctx.insn.operands[2]), ctx.r(ctx.insn.operands[1]));
  ctx.println("\t{}.u64 = {}.u64 + {}.u64;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[2]));
  emitRecordFormCompare(ctx);
  return true;
}

//=============================================================================
// Division
//=============================================================================
// PPC division instructions do NOT trap on divide-by-zero - they produce undefined results.
// We generate safe division that returns 0 when the divisor is zero.

bool build_divd(BuilderContext& ctx) {
  // divd rD,rA,rB: rD = rA / rB (64-bit signed)
  // Safe division: return 0 if divisor is zero or INT64_MIN / -1 (UB in C)
  auto rD = ctx.r(ctx.insn.operands[0]);
  auto rA = ctx.r(ctx.insn.operands[1]);
  auto rB = ctx.r(ctx.insn.operands[2]);
  ctx.println(
      "\t{}.s64 = ({}.s64 && !({}.s64 == INT64_MIN && {}.s64 == -1)) ? {}.s64 / {}.s64 : 0;", rD,
      rB, rA, rB, rA, rB);
  emitRecordFormCompare(ctx);
  return true;
}

bool build_divdu(BuilderContext& ctx) {
  // divdu rD,rA,rB: rD = rA / rB (64-bit unsigned)
  // Safe division: return 0 if divisor is zero
  auto rD = ctx.r(ctx.insn.operands[0]);
  auto rA = ctx.r(ctx.insn.operands[1]);
  auto rB = ctx.r(ctx.insn.operands[2]);
  ctx.println("\t{}.u64 = {}.u64 ? {}.u64 / {}.u64 : 0;", rD, rB, rA, rB);
  emitRecordFormCompare(ctx);
  return true;
}

bool build_divw(BuilderContext& ctx) {
  // divw rD,rA,rB: rD = rA / rB (32-bit signed)
  // Safe division: return 0 if divisor is zero or INT32_MIN / -1 (UB in C)
  auto rD = ctx.r(ctx.insn.operands[0]);
  auto rA = ctx.r(ctx.insn.operands[1]);
  auto rB = ctx.r(ctx.insn.operands[2]);
  ctx.println(
      "\t{}.u64 = {};", rD,
      numCast(ctx, "uint32_t",
              fmt::format("({0}.s32 && !({1}.s32 == INT32_MIN && {0}.s32 == -1)) ? {1}.s32 / "
                          "{0}.s32 : 0",
                          rB, rA)));
  emitRecordFormCompare(ctx);
  return true;
}

bool build_divwu(BuilderContext& ctx) {
  // divwu rD,rA,rB: rD = rA / rB (32-bit unsigned)
  // Safe division: return 0 if divisor is zero
  auto rD = ctx.r(ctx.insn.operands[0]);
  auto rA = ctx.r(ctx.insn.operands[1]);
  auto rB = ctx.r(ctx.insn.operands[2]);
  ctx.println("\t{}.u64 = {};", rD,
              numCast(ctx, "uint32_t", fmt::format("{0}.u32 ? {1}.u32 / {0}.u32 : 0", rB, rA)));
  emitRecordFormCompare(ctx);
  return true;
}

//=============================================================================
// Multiplication
//=============================================================================

bool build_mulhw(BuilderContext& ctx) {
  auto a = ctx.r(ctx.insn.operands[1]);
  auto b = ctx.r(ctx.insn.operands[2]);
  ctx.println("\t{}.s64 = ({} * {}) >> 32;", ctx.r(ctx.insn.operands[0]),
              numCast(ctx, "int64_t", fmt::format("{}.s32", a)),
              numCast(ctx, "int64_t", fmt::format("{}.s32", b)));
  emitRecordFormCompare(ctx);
  return true;
}

bool build_mulhwu(BuilderContext& ctx) {
  // C++ keeps the functional-cast spelling byte-identical to prior output;
  // functional-style casts are C++-only, so C uses a C-style cast instead.
  const char* cast64 = ctx.language() == Language::C ? "(uint64_t)" : "uint64_t";
  ctx.println("\t{}.u64 = ({}({}.u32) * {}({}.u32)) >> 32;", ctx.r(ctx.insn.operands[0]), cast64,
              ctx.r(ctx.insn.operands[1]), cast64, ctx.r(ctx.insn.operands[2]));
  emitRecordFormCompare(ctx);
  return true;
}

bool build_mulld(BuilderContext& ctx) {
  // Use unsigned multiplication to avoid signed overflow UB (PPC wraps on overflow)
  ctx.println("\t{}.s64 = {}({}.u64 * {}.u64);", ctx.r(ctx.insn.operands[0]), ctx.cast("int64_t"),
              ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[2]));
  emitRecordFormCompare(ctx);
  return true;
}

bool build_mulli(BuilderContext& ctx) {
  // Use unsigned multiplication to avoid signed overflow UB (PPC wraps on overflow)
  ctx.println("\t{}.s64 = {}({}.u64 * {}({}));", ctx.r(ctx.insn.operands[0]), ctx.cast("int64_t"),
              ctx.r(ctx.insn.operands[1]), ctx.cast("uint64_t"),
              static_cast<int32_t>(ctx.insn.operands[2]));
  return true;
}

bool build_mullw(BuilderContext& ctx) {
  // C++ keeps the functional-cast spelling byte-identical to prior output;
  // functional-style casts are C++-only, so C uses a C-style cast instead.
  const char* cast64 = ctx.language() == Language::C ? "(int64_t)" : "int64_t";
  ctx.println("\t{}.s64 = {}({}.s32) * {}({}.s32);", ctx.r(ctx.insn.operands[0]), cast64,
              ctx.r(ctx.insn.operands[1]), cast64, ctx.r(ctx.insn.operands[2]));
  emitRecordFormCompare(ctx);
  return true;
}

bool build_mulhd(BuilderContext& ctx) {
  // mulhd: rD = high 64 bits of (rA * rB) (signed)
  ctx.println(
      "\t{}.s64 = {}(({}({}({}.s64)) * "
      "{}({}({}.s64))) >> 64);",
      ctx.r(ctx.insn.operands[0]), ctx.cast("int64_t"), ctx.cast("__int128"), ctx.cast("int64_t"),
      ctx.r(ctx.insn.operands[1]), ctx.cast("__int128"), ctx.cast("int64_t"),
      ctx.r(ctx.insn.operands[2]));
  emitRecordFormCompare(ctx);
  return true;
}

bool build_mulhdu(BuilderContext& ctx) {
  // mulhdu: rD = high 64 bits of (rA * rB) (unsigned)
  ctx.println(
      "\t{}.u64 = {}(({}({}.u64) * "
      "{}({}.u64)) >> 64);",
      ctx.r(ctx.insn.operands[0]), ctx.cast("uint64_t"), ctx.cast("__uint128_t"),
      ctx.r(ctx.insn.operands[1]), ctx.cast("__uint128_t"), ctx.r(ctx.insn.operands[2]));
  emitRecordFormCompare(ctx);
  return true;
}

//=============================================================================
// Negation
//=============================================================================

bool build_neg(BuilderContext& ctx) {
  // Use unsigned negation to avoid signed overflow UB when negating INT64_MIN
  ctx.println("\t{}.s64 = {}(-{}.u64);", ctx.r(ctx.insn.operands[0]), ctx.cast("int64_t"),
              ctx.r(ctx.insn.operands[1]));
  emitRecordFormCompare(ctx);
  return true;
}

//=============================================================================
// Subtraction
//=============================================================================

bool build_subf(BuilderContext& ctx) {
  ctx.println("\t{}.u64 = {}.u64 - {}.u64;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[2]), ctx.r(ctx.insn.operands[1]));
  emitRecordFormCompare(ctx);
  return true;
}

bool build_subfc(BuilderContext& ctx) {
  ctx.println("\t{}.ca = {}.u32 >= {}.u32;", ctx.xer(), ctx.r(ctx.insn.operands[2]),
              ctx.r(ctx.insn.operands[1]));
  ctx.println("\t{}.u64 = {}.u64 - {}.u64;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[2]), ctx.r(ctx.insn.operands[1]));
  emitRecordFormCompare(ctx);
  return true;
}

bool build_subfe(BuilderContext& ctx) {
  ctx.println("\t{}.u8 = (~{}.u32 + {}.u32 < ~{}.u32) | (~{}.u32 + {}.u32 + {}.ca < {}.ca);",
              ctx.temp(), ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[2]),
              ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[2]),
              ctx.xer(), ctx.xer());
  ctx.println("\t{}.u64 = ~{}.u64 + {}.u64 + {}.ca;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[2]), ctx.xer());
  ctx.println("\t{}.ca = {}.u8;", ctx.xer(), ctx.temp());
  emitRecordFormCompare(ctx);
  return true;
}

bool build_subfic(BuilderContext& ctx) {
  ctx.println("\t{}.ca = {}.u32 <= {};", ctx.xer(), ctx.r(ctx.insn.operands[1]),
              ctx.insn.operands[2]);
  ctx.println("\t{}.u64 = {}({}) - {}.u64;", ctx.r(ctx.insn.operands[0]), ctx.cast("uint64_t"),
              static_cast<int32_t>(ctx.insn.operands[2]), ctx.r(ctx.insn.operands[1]));
  return true;
}

bool build_subfze(BuilderContext& ctx) {
  // subfze: rD = ~rA + CA (subtract from zero extended)
  ctx.println("\t{}.u8 = ~{}.u32 + {}.ca < ~{}.u32;", ctx.temp(), ctx.r(ctx.insn.operands[1]),
              ctx.xer(), ctx.r(ctx.insn.operands[1]));
  ctx.println("\t{}.u64 = ~{}.u64 + {}.ca;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[1]), ctx.xer());
  ctx.println("\t{}.ca = {}.u8;", ctx.xer(), ctx.temp());
  emitRecordFormCompare(ctx);
  return true;
}

bool build_subfme(BuilderContext& ctx) {
  // subfme: rD = ~rA + CA - 1 (subtract from minus one extended)
  ctx.println(
      "\t{}.u8 = (~{}.u32 + 0xFFFFFFFFu < ~{}.u32) | (~{}.u32 + 0xFFFFFFFFu + {}.ca < {}.ca);",
      ctx.temp(), ctx.r(ctx.insn.operands[1]), ctx.r(ctx.insn.operands[1]),
      ctx.r(ctx.insn.operands[1]), ctx.xer(), ctx.xer());
  ctx.println("\t{}.u64 = ~{}.u64 + {}.ca + 0xFFFFFFFFFFFFFFFFull;", ctx.r(ctx.insn.operands[0]),
              ctx.r(ctx.insn.operands[1]), ctx.xer());
  ctx.println("\t{}.ca = {}.u8;", ctx.xer(), ctx.temp());
  emitRecordFormCompare(ctx);
  return true;
}

}  // namespace rex::codegen
