/**
 * @file        rexcodegen/internal/helpers.h
 * @brief       Recompiler helper utilities
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include "builder_context.h"

#include <cstring>

#include <rex/codegen/function_scanner.h>
#include <rex/logging.h>

#include "../codegen_logging.h"

#include <dis-asm.h>
#include <ppc-inst.h>

namespace rex::codegen {

/**
 * Compute a 64-bit mask for PPC rotate/mask instructions.
 * @param mstart Starting bit position (0-63)
 * @param mstop Ending bit position (0-63)
 * @return 64-bit mask with bits set between mstart and mstop
 */
inline uint64_t compute_mask(uint32_t mstart, uint32_t mstop) {
  mstart &= 0x3F;
  mstop &= 0x3F;
  uint64_t value = (UINT64_MAX >> mstart) ^ ((mstop >= 63) ? 0 : UINT64_MAX >> (mstop + 1));
  return mstart <= mstop ? value : ~value;
}

//=============================================================================
// Language-Conditional Cast Helper
//=============================================================================

/**
 * Emit a functional-style numeric cast: `type(expr)` in C++ (byte-identical
 * to this backend's long-standing output), `(type)(expr)` in C (C has no
 * functional-cast syntax). `type` must be a name valid in both languages
 * (fundamental types, or a <stdint.h> typedef visible in both via the
 * project's PCH).
 */
inline std::string numCast(BuilderContext& ctx, std::string_view type, std::string_view expr) {
  if (ctx.language() == Language::C)
    return fmt::format("({})({})", type, expr);
  return fmt::format("{}({})", type, expr);
}

/**
 * Emit `cr6.setFromMask(mask_expr, imm)` in C++ (PPCCRRegister::setFromMask,
 * include/rex/ppc/context.h:103-117 -- byte-identical to this backend's
 * long-standing output). C has no member functions, so the C backend calls a
 * free-function equivalent instead (x360_cr_set_from_mask_ps/_epi8, defined
 * inline in the C PCH, pch_h_c.inja): `imm == 0xF` selects the
 * movemask_ps/__m128 overload (4-lane float compares), any other value
 * (every other call site passes 0xFFFF) selects the movemask_epi8/__m128i
 * overload (16-lane byte/word compares) -- the only two overloads
 * setFromMask has, and the only two imm values this codegen ever emits for
 * it (see builders/vector.cpp's CR6 vector-compare call sites).
 */
inline std::string crSetFromMask(BuilderContext& ctx, std::string_view mask_expr, int imm) {
  if (ctx.language() == Language::C) {
    const char* fn = (imm == 0xF) ? "x360_cr_set_from_mask_ps" : "x360_cr_set_from_mask_epi8";
    return fmt::format("{}(&{}, {}, {})", fn, ctx.cr(6), mask_expr, imm);
  }
  return fmt::format("{}.setFromMask({}, {})", ctx.cr(6), mask_expr, imm);
}

//=============================================================================
// CR Bit Helpers
//=============================================================================

/// Map PPC BI field bit index (0-3) to CRRegister member name.
inline const char* crBitName(uint32_t bi) {
  static constexpr const char* names[] = {"lt", "gt", "eq", "so"};
  return names[bi & 3];
}

//=============================================================================
// Record-Form Helpers
//=============================================================================

/**
 * Check if the current instruction is a record form (has '.' suffix).
 *
 * Record-form instructions update CR0 based on the result.
 *
 * @param insn The ppc_insn being processed
 * @return true if the instruction name contains '.' (record form)
 */
inline bool isRecordForm(const ppc_insn& insn) {
  return std::strchr(insn.opcode->name, '.') != nullptr;
}

/**
 * Emit CR0 comparison for record-form instructions.
 *
 * Record-form instructions (those with '.' suffix like add., and., etc.)
 * update CR0 based on the result compared to zero:
 *   CR0[LT] = result < 0
 *   CR0[GT] = result > 0
 *   CR0[EQ] = result == 0
 *   CR0[SO] = XER[SO]
 *
 * @param ctx The builder context containing the instruction being processed
 */
/**
 * Emit an int32_t compare-to-zero into the given CR field: `crN.compare<int32_t>(reg.s32, 0,
 * xer)` in C++, `x360_cr_compare_s32(&crN, reg.s32, 0, &xer)` in C. Shared by record-form
 * ("." suffixed) arithmetic/logical instructions (emitRecordFormCompare) and the two
 * immediate-form logical instructions that unconditionally set CR0 (andi., andis.).
 *
 * @param ctx The builder context
 * @param crField CR field index (0-7)
 * @param regExpr Pre-resolved register expression (e.g. from ctx.r(...))
 */
inline void emitCompareToZero(BuilderContext& ctx, size_t crField, const std::string& regExpr) {
  if (ctx.language() == Language::C) {
    // CRRegister::compare<T> is a C++ template method; the C backend calls a
    // free-function equivalent instead (defined inline in the C PCH,
    // pch_h_c.inja). Signature mirrors x360_cr_compare_s64/u64
    // (x360rt/ppc.h): takes the whole XER so the callee reads xer->so,
    // matching CRRegister::compare<T>'s semantics.
    ctx.println("\tx360_cr_compare_s32(&{}, {}.s32, 0, &{});", ctx.cr(crField), regExpr,
                ctx.xer());
  } else {
    ctx.println("\t{}.compare<int32_t>({}.s32, 0, {});", ctx.cr(crField), regExpr, ctx.xer());
  }
}

inline void emitRecordFormCompare(BuilderContext& ctx) {
  if (!isRecordForm(ctx.insn))
    return;
  emitCompareToZero(ctx, 0, ctx.r(ctx.insn.operands[0]));
}

/**
 * Emit a CR bit operation: crD = crA <op> crB
 *
 * CR bit operations work on individual CR bits (0-31). This helper:
 * - Maps bit indices to CR field (0-7) and field bit (0-3)
 * - Emits code to access CR fields by bit name
 *
 * @param ctx The builder context
 * @param op The operation symbol as a string (e.g., "|", "&", "^")
 * @param invertA If true, invert the value of crA before the operation
 * @param invertB If true, invert the value of crB before the operation
 * @param invertResult If true, invert the final result before storing in crD
 */
inline void emitCRBitOperation(BuilderContext& ctx, std::string_view op, bool invertA = false,
                               bool invertB = false, bool invertResult = false) {
  uint32_t crD = ctx.insn.operands[0];
  uint32_t crA = ctx.insn.operands[1];
  uint32_t crB = ctx.insn.operands[2];

  uint32_t crField_D = crD / 4;
  uint32_t crBit_D = crD % 4;
  uint32_t crField_A = crA / 4;
  uint32_t crBit_A = crA % 4;
  uint32_t crField_B = crB / 4;
  uint32_t crBit_B = crB % 4;

  std::string aExpr = fmt::format("{}.{}", ctx.cr(crField_A), crBitName(crBit_A));

  std::string bExpr = fmt::format("{}.{}", ctx.cr(crField_B), crBitName(crBit_B));

  if (invertA)
    aExpr = "!(" + aExpr + ")";
  if (invertB)
    bExpr = "!(" + bExpr + ")";

  std::string expr = fmt::format("{} {} {}", aExpr, op, bExpr);

  if (invertResult)
    expr = "!(" + expr + ")";

  ctx.println("\t{}.{} = {};", ctx.cr(crField_D), crBitName(crBit_D), expr);
}

//=============================================================================
// Comparison Instruction Helpers
//=============================================================================

/**
 * Free-function name for a C-mode `x360_cr_compare_*` call, given the same
 * type_name spelling used by the C++ `compare<T>()` template instantiation.
 * Defined inline in the C PCH (pch_h_c.inja): s64/u64 mirror x360rt/ppc.h's
 * own definitions; s32/u32 are this backend's own (x360rt has no 32-bit
 * guest-visible compare need outside codegen).
 */
inline const char* crCompareFuncName(const char* type_name) {
  if (std::strcmp(type_name, "int64_t") == 0)
    return "x360_cr_compare_s64";
  if (std::strcmp(type_name, "uint64_t") == 0)
    return "x360_cr_compare_u64";
  if (std::strcmp(type_name, "uint32_t") == 0)
    return "x360_cr_compare_u32";
  return "x360_cr_compare_s32";
}

/**
 * Emit register-to-register comparison.
 *
 * Pattern: crD.compare<T>(rA.field, rB.field, XER)
 * Used by: cmpd, cmpld, cmplw, cmpw
 *
 * @param ctx The builder context
 * @param type_name The comparison type (e.g., "int64_t", "uint32_t")
 * @param field The register field accessor (e.g., "s64", "u32")
 */
inline void emitCompareRegister(BuilderContext& ctx, const char* type_name, const char* field) {
  if (ctx.language() == Language::C) {
    ctx.println("\t{}(&{}, {}.{}, {}.{}, &{});", crCompareFuncName(type_name),
                ctx.cr(ctx.insn.operands[0]), ctx.r(ctx.insn.operands[1]), field,
                ctx.r(ctx.insn.operands[2]), field, ctx.xer());
    return;
  }
  ctx.println("\t{}.compare<{}>({}.{}, {}.{}, {});", ctx.cr(ctx.insn.operands[0]), type_name,
              ctx.r(ctx.insn.operands[1]), field, ctx.r(ctx.insn.operands[2]), field, ctx.xer());
}

/**
 * Emit register-to-immediate comparison.
 *
 * Pattern: crD.compare<T>(rA.field, imm, XER)
 * Used by: cmpdi, cmpldi, cmplwi, cmpwi
 *
 * @param ctx The builder context
 * @param type_name The comparison type (e.g., "int64_t", "uint32_t")
 * @param field The register field accessor (e.g., "s64", "u32")
 * @param sign_extend If true, sign-extend the immediate via static_cast<int32_t>
 */
inline void emitCompareImmediate(BuilderContext& ctx, const char* type_name, const char* field,
                                 bool sign_extend) {
  std::string imm = sign_extend
                        ? fmt::format("{}", static_cast<int32_t>(ctx.insn.operands[2]))
                        : fmt::format("{}", ctx.insn.operands[2]);
  if (ctx.language() == Language::C) {
    ctx.println("\t{}(&{}, {}.{}, {}, &{});", crCompareFuncName(type_name),
                ctx.cr(ctx.insn.operands[0]), ctx.r(ctx.insn.operands[1]), field, imm, ctx.xer());
    return;
  }
  ctx.println("\t{}.compare<{}>({}.{}, {}, {});", ctx.cr(ctx.insn.operands[0]), type_name,
              ctx.r(ctx.insn.operands[1]), field, imm, ctx.xer());
}

//=============================================================================
// Memory Operation Helpers
//=============================================================================

/**
 * Emit D-form load with update instruction.
 *
 * Pattern: EA = (rA) + d; rD = MEM[EA]; rA = EA
 * Used by: lbzu, lwzu, ldu, etc.
 *
 * @param ctx The builder context
 * @param load_macro The REX_LOAD_* macro to use (e.g., "REX_LOAD_U8")
 */
inline void emitLoadWithUpdate(BuilderContext& ctx, const char* load_macro) {
  // EA = displacement + rA
  ctx.println("\t{} = {} + {}.u32;", ctx.ea(), static_cast<int32_t>(ctx.insn.operands[1]),
              ctx.r(ctx.insn.operands[2]));
  // rD = MEM[EA]
  ctx.println("\t{}.u64 = {}({});", ctx.r(ctx.insn.operands[0]), load_macro, ctx.ea());
  // rA = EA (update)
  ctx.println("\t{}.u32 = {};", ctx.r(ctx.insn.operands[2]), ctx.ea());
}

/**
 * Emit X-form load with update instruction.
 *
 * Pattern: EA = rA + rB; rD = MEM[EA]; rA = EA
 * Used by: lbzux, lhzux, lwzux, ldux
 *
 * @param ctx The builder context
 * @param load_macro The REX_LOAD_* macro to use (e.g., "REX_LOAD_U8")
 */
inline void emitLoadXFormWithUpdate(BuilderContext& ctx, const char* load_macro) {
  ctx.println("\t{} = {}.u32 + {}.u32;", ctx.ea(), ctx.r(ctx.insn.operands[1]),
              ctx.r(ctx.insn.operands[2]));
  ctx.println("\t{}.u64 = {}({});", ctx.r(ctx.insn.operands[0]), load_macro, ctx.ea());
  ctx.println("\t{}.u32 = {};", ctx.r(ctx.insn.operands[1]), ctx.ea());
}

/**
 * Emit D-form store with update instruction.
 *
 * Pattern: EA = (rA) + d; MEM[EA] = rS; rA = EA
 * Used by: stbu, stwu, stdu, etc.
 *
 * @param ctx The builder context
 * @param store_macro The REX_STORE_* macro to use (e.g., "REX_STORE_U8")
 * @param field The register field to store (e.g., "u8", "u32", "u64")
 */
inline void emitStoreWithUpdate(BuilderContext& ctx, const char* store_macro, const char* field) {
  // EA = displacement + rA
  ctx.println("\t{} = {} + {}.u32;", ctx.ea(), static_cast<int32_t>(ctx.insn.operands[1]),
              ctx.r(ctx.insn.operands[2]));
  // MEM[EA] = rS
  ctx.println("\t{}({}, {}.{});", store_macro, ctx.ea(), ctx.r(ctx.insn.operands[0]), field);
  // rA = EA (update)
  ctx.println("\t{}.u32 = {};", ctx.r(ctx.insn.operands[2]), ctx.ea());
}

/**
 * Emit X-form store with update instruction.
 *
 * Pattern: EA = rA + rB; MEM[EA] = rS; rA = EA
 * Used by: stbux, sthux, stwux, stdux
 *
 * @param ctx The builder context
 * @param store_macro The REX_STORE_* normal macro (e.g., "REX_STORE_U8")
 * @param mmio_macro The REX_MM_STORE_* MMIO macro (e.g., "REX_MM_STORE_U8")
 * @param field The register field to store (e.g., "u8", "u32", "u64")
 */
inline void emitStoreXFormWithUpdate(BuilderContext& ctx, const char* store_macro,
                                     const char* mmio_macro, const char* field) {
  ctx.println("\t{} = {}.u32 + {}.u32;", ctx.ea(), ctx.r(ctx.insn.operands[1]),
              ctx.r(ctx.insn.operands[2]));
  ctx.println("\t{}({}, {}.{});", ctx.mmio_check_x_form() ? mmio_macro : store_macro, ctx.ea(),
              ctx.r(ctx.insn.operands[0]), field);
  ctx.println("\t{}.u32 = {};", ctx.r(ctx.insn.operands[1]), ctx.ea());
}

/**
 * Get the appropriate store macro based on MMIO context.
 *
 * @param ctx The builder context
 * @param normal_macro Normal store macro (e.g., "REX_STORE_U32")
 * @param mmio_macro MMIO store macro (e.g., "REX_MM_STORE_U32")
 * @return The appropriate macro string
 */
inline const char* getStoreMacro(BuilderContext& ctx, const char* normal_macro,
                                 const char* mmio_macro) {
  return ctx.mmio_check_d_form() ? mmio_macro : normal_macro;
}

//=============================================================================
// Atomic Operation Helpers
//=============================================================================

/**
 * Emit atomic load-and-reserve instruction (lwarx/ldarx pattern).
 *
 * Pattern: EA = rA + rB; reserved = *(T*)REX_RAW_ADDR(EA); rD = bswap(reserved)
 *
 * @param ctx The builder context
 * @param ptr_type The pointer type (e.g., "uint32_t", "uint64_t")
 * @param bswap_func The byte-swap builtin (e.g., "__builtin_bswap32")
 * @param reserved_field The reserved register field (e.g., "u32", "u64")
 */
inline void emitAtomicLoadReserve(BuilderContext& ctx, const char* ptr_type, const char* bswap_func,
                                  const char* reserved_field) {
  ctx.print("\t{} = ", ctx.ea());
  if (ctx.insn.operands[1] != 0)
    ctx.print("{}.u32 + ", ctx.r(ctx.insn.operands[1]));
  ctx.println("{}.u32;", ctx.r(ctx.insn.operands[2]));
  ctx.println("\t{}.{} = *({}*)REX_RAW_ADDR({});", ctx.reserved(), reserved_field, ptr_type,
              ctx.ea());
  ctx.println("\t{}.u64 = {}({}.{});", ctx.r(ctx.insn.operands[0]), bswap_func, ctx.reserved(),
              reserved_field);
}

/**
 * Emit atomic store-conditional instruction (stwcx./stdcx. pattern).
 *
 * Pattern: EA = rA + rB; cr0 = CAS(EA, reserved, bswap(rS))
 *
 * @param ctx The builder context
 * @param ptr_type The pointer type (e.g., "uint32_t", "uint64_t")
 * @param bswap_func The byte-swap builtin (e.g., "__builtin_bswap32")
 * @param field The register field (e.g., "s32", "s64")
 */
inline void emitAtomicStoreConditional(BuilderContext& ctx, const char* ptr_type,
                                       const char* bswap_func, const char* field) {
  ctx.print("\t{} = ", ctx.ea());
  if (ctx.insn.operands[1] != 0)
    ctx.print("{}.u32 + ", ctx.r(ctx.insn.operands[1]));
  ctx.println("{}.u32;", ctx.r(ctx.insn.operands[2]));
  ctx.println("\t{}.lt = 0;", ctx.cr(0));
  ctx.println("\t{}.gt = 0;", ctx.cr(0));
  // reinterpret_cast is C++-only; the C backend uses an equivalent C-style
  // pointer cast, which is byte-identical for reinterpreting an address.
  std::string castPrefix = ctx.language() == Language::C ? fmt::format("({}*)", ptr_type)
                                                          : fmt::format("reinterpret_cast<{}*>", ptr_type);
  ctx.println(
      "\t{}.eq = __sync_bool_compare_and_swap({}(REX_RAW_ADDR({})), "
      "{}.{}, {}({}.{}));",
      ctx.cr(0), castPrefix, ctx.ea(), ctx.reserved(), field, bswap_func,
      ctx.r(ctx.insn.operands[0]), field);
  ctx.println("\t{}.so = {}.so;", ctx.cr(0), ctx.xer());
}

//=============================================================================
// Sign-Extending Load Helpers
//=============================================================================

/**
 * Emit D-form sign-extending load instruction.
 *
 * Pattern: rD = sign_extend(LOAD(rA + d))
 * Used by: lha, lwa (halfword/word algebraic loads)
 *
 * @param ctx The builder context
 * @param cast_type The cast for sign extension (e.g., "int16_t", "int32_t")
 * @param load_macro The REX_LOAD_* macro to use
 */
inline void emitSignExtendLoadDForm(BuilderContext& ctx, const char* cast_type,
                                    const char* load_macro) {
  // numCast keeps C++ output byte-identical (functional-style `type(expr)`);
  // C has no functional-cast syntax, so it gets `(type)(expr)` instead (this
  // previously hardcoded the C++-only functional form for both languages,
  // which is a syntax error in C11).
  std::string addr = fmt::format("{}(", load_macro);
  if (ctx.insn.operands[2] != 0)
    addr += fmt::format("{}.u32 + ", ctx.r(ctx.insn.operands[2]));
  addr += fmt::format("{})", static_cast<int32_t>(ctx.insn.operands[1]));
  ctx.println("\t{}.s64 = {};", ctx.r(ctx.insn.operands[0]), numCast(ctx, cast_type, addr));
}

/**
 * Emit X-form sign-extending load instruction.
 *
 * Pattern: rD = sign_extend(LOAD(rA + rB))
 * Used by: lhax, lwax (halfword/word algebraic indexed loads)
 *
 * @param ctx The builder context
 * @param cast_type The cast for sign extension (e.g., "int16_t", "int32_t")
 * @param load_macro The REX_LOAD_* macro to use
 */
inline void emitSignExtendLoadXForm(BuilderContext& ctx, const char* cast_type,
                                    const char* load_macro) {
  // See emitSignExtendLoadDForm above: numCast keeps this a functional-style
  // cast in C++ (byte-identical) and a C-style cast in C (valid C11).
  std::string addr = fmt::format("{}(", load_macro);
  if (ctx.insn.operands[1] != 0)
    addr += fmt::format("{}.u32 + ", ctx.r(ctx.insn.operands[1]));
  addr += fmt::format("{}.u32)", ctx.r(ctx.insn.operands[2]));
  ctx.println("\t{}.s64 = {};", ctx.r(ctx.insn.operands[0]), numCast(ctx, cast_type, addr));
}

//=============================================================================
// MMIO Detection Helpers
//=============================================================================

/**
 * Check if an upper-16-bit immediate value corresponds to a known MMIO range.
 *
 * Xbox 360 hardware register ranges:
 * - GPU MMIO: 0x7FC80000-0x7FCFFFFF (upper bits: 0x7FC8-0x7FCF)
 * - XMA/APU MMIO: 0x7FEA0000-0x7FEAFFFF (upper bits: 0x7FEA)
 *
 * @param imm The upper 16 bits loaded by lis/oris
 * @return true if the value matches a known MMIO base address range
 */
inline bool isMMIOUpperBits(uint32_t imm) {
  return (imm >= 0x7FC8 && imm <= 0x7FCF) || imm == 0x7FEA;
}

//=============================================================================
// Branch Bounds-Checking Helper
//=============================================================================

/**
 * Emit a conditional branch with bounds checking.
 *
 * If the target is within the current function, emits a goto.
 * If outside, emits a warning and a return statement.
 *
 * @param ctx The builder context
 * @param target Target address of the branch
 * @param condition Pre-formatted condition expression (e.g., "ctr.u32 != 0")
 * @param instr_name Instruction mnemonic for the warning message
 */
inline void emitBranchWithBoundsCheck(BuilderContext& ctx, uint32_t target,
                                      std::string_view condition, std::string_view instr_name) {
  if (target < ctx.fn.base() || target >= ctx.fn.end()) {
    REXCODEGEN_WARN("{} at {:X} branches outside function to {:X}", instr_name, ctx.base, target);
    ctx.println("\tif ({}) {{ /* branch to 0x{:X} outside function */ return; }}", condition,
                target);
  } else {
    ctx.println("\tif ({}) goto loc_{:X};", condition, target);
  }
}

//=============================================================================
// Vector EA Calculation Helpers
//=============================================================================

/**
 * Emit aligned or unaligned vector effective address calculation to ea.
 *
 * Pattern: ea = (opt_rA + rB) [& ~align_mask]
 * Uses operands[1] as optional base register and operands[2] as index register.
 *
 * @param ctx The builder context
 * @param align_mask Alignment mask string (e.g., "0xF"), or nullptr for no alignment
 */
inline void emitVectorEA(BuilderContext& ctx, const char* align_mask = nullptr) {
  if (align_mask)
    ctx.print("\t{} = (", ctx.ea());
  else
    ctx.print("\t{} = ", ctx.ea());
  if (ctx.insn.operands[1] != 0)
    ctx.print("{}.u32 + ", ctx.r(ctx.insn.operands[1]));
  if (align_mask)
    ctx.println("{}.u32) & ~{};", ctx.r(ctx.insn.operands[2]), align_mask);
  else
    ctx.println("{}.u32;", ctx.r(ctx.insn.operands[2]));
}

/**
 * Emit unaligned vector effective address calculation to temp.
 *
 * Pattern: temp.u32 = opt_rA + rB
 * Uses operands[1] as optional base register and operands[2] as index register.
 *
 * @param ctx The builder context
 */
inline void emitVectorTempEA(BuilderContext& ctx) {
  ctx.print("\t{}.u32 = ", ctx.temp());
  if (ctx.insn.operands[1] != 0)
    ctx.print("{}.u32 + ", ctx.r(ctx.insn.operands[1]));
  ctx.println("{}.u32;", ctx.r(ctx.insn.operands[2]));
}

//=============================================================================
// Trap Instruction Helper
//=============================================================================

/**
 * Emit a PPC trap instruction: if (<condition>) ppc_trap(ctx, base, 0);
 *
 * @param to       5-bit TO field
 * @param aSigned  First operand, signed (e.g., "ctx.r3.s32")
 * @param aUnsigned First operand, unsigned (e.g., "ctx.r3.u32")
 * @param bSigned  Second operand, signed (e.g., "ctx.r4.s32" or "-1")
 * @param bUnsigned Second operand, unsigned (e.g., "ctx.r4.u32" or "4294967295u")
 */
inline void emitTrap(BuilderContext& ctx, uint32_t to, const std::string& aSigned,
                     const std::string& aUnsigned, const std::string& bSigned,
                     const std::string& bUnsigned) {
  if (to == 0)
    return;
  if (to == 0x1F) {
    ctx.println("\tppc_trap(ctx, base, 0);");
    return;
  }

  std::string cond;
  auto add = [&](std::string_view c) {
    if (!cond.empty())
      cond += " || ";
    cond += c;
  };
  if (to & 0x10)
    add(fmt::format("{} < {}", aSigned, bSigned));
  if (to & 0x08)
    add(fmt::format("{} > {}", aSigned, bSigned));
  if (to & 0x04)
    add(fmt::format("{} == {}", aSigned, bSigned));
  if (to & 0x02)
    add(fmt::format("{} < {}", aUnsigned, bUnsigned));
  if (to & 0x01)
    add(fmt::format("{} > {}", aUnsigned, bUnsigned));

  ctx.println("\tif ({}) ppc_trap(ctx, base, 0);", cond);
}

}  // namespace rex::codegen
