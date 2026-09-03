/**
 * @file        tests/ppc/c_harness/x360rt/ppc.h
 * @brief       Minimal C11 PPCContext replica for cgen-c-backend's own
 *              verification harness. NOT the production runtime header.
 *
 * The real `<x360rt/ppc.h>` (referenced by the C backend's generated PCH,
 * resources/templates/codegen/pch_h_c.inja) is owned by a different part of
 * this workstream and is expected to define these same names with the same
 * layout described in include/rex/ppc/context.h. This stand-in exists only
 * so the C backend's own tests/ppc/c_harness/ target can compile and run
 * generated C function bodies in isolation, without depending on unfinished
 * sibling work. Field names/sizes mirror include/rex/ppc/context.h closely
 * enough for the register accessors under test (r/f/v/cr/ctr/xer/reserved),
 * but this header is intentionally not a byte-for-byte ABI match: nothing
 * else links against it.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

/* Struct/union member names live in their own per-type namespace in C (as in
 * C++), so naming a member `s8` here does not collide with the file-scope
 * `s8` typedef above; `reg.s8` and `reg.u8` are unambiguous. */
typedef union {
  s8 s8;
  u8 u8;
  s16 s16;
  u16 u16;
  s32 s32;
  u32 u32;
  s64 s64;
  u64 u64;
  float f32;
  double f64;
} PPCRegister;

typedef struct {
  u8 so;
  u8 ov;
  u8 ca;
} PPCXERRegister;

typedef struct {
  u8 lt;
  u8 gt;
  u8 eq;
  u8 so;
} PPCCRRegister;

typedef struct {
  u32 csr;
} PPCFPSCRRegister;

typedef union __attribute__((aligned(16))) {
  s8 s8x16[16];
  u8 u8[16];
  s16 s16x8[8];
  u16 u16[8];
  s32 s32x4[4];
  u32 u32[4];
  s64 s64x2[2];
  u64 u64[2];
  float f32[4];
  double f64[2];
} PPCVRegister;

typedef struct __attribute__((aligned(64))) PPCContext {
  PPCRegister r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15;
  PPCRegister r16, r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r28, r29, r30, r31;
  u64 lr;
  PPCRegister ctr;
  PPCXERRegister xer;
  PPCRegister reserved;
  u32 msr;
  PPCCRRegister cr0, cr1, cr2, cr3, cr4, cr5, cr6, cr7;
  PPCFPSCRRegister fpscr;
  u8 vscr_sat;
  u32 last_indirect_target;
  PPCRegister f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15;
  PPCRegister f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31;
  PPCVRegister v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15;
  PPCVRegister v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31;
  PPCVRegister v32, v33, v34, v35, v36, v37, v38, v39, v40, v41, v42, v43, v44, v45, v46, v47;
  PPCVRegister v48, v49, v50, v51, v52, v53, v54, v55, v56, v57, v58, v59, v60, v61, v62, v63;
  PPCVRegister v64, v65, v66, v67, v68, v69, v70, v71, v72, v73, v74, v75, v76, v77, v78, v79;
  PPCVRegister v80, v81, v82, v83, v84, v85, v86, v87, v88, v89, v90, v91, v92, v93, v94, v95;
  PPCVRegister v96, v97, v98, v99, v100, v101, v102, v103, v104, v105, v106, v107, v108, v109, v110, v111;
  PPCVRegister v112, v113, v114, v115, v116, v117, v118, v119, v120, v121, v122, v123, v124, v125, v126, v127;
} PPCContext;

typedef void PPCFunc(PPCContext* ctx, u8* base);

typedef struct {
  u32 address;
  PPCFunc* func;
} PPCFuncMapping;

typedef struct {
  u64 code_base;
  u64 code_size;
  u64 image_base;
  u64 image_size;
  PPCFuncMapping* func_mappings;
  int rexcrt_heap;
  void (*register_modules)(void*);
  struct {
    int skip_lr;
    int ctr_as_local;
    int xer_as_local;
    int reserved_as_local;
    int skip_msr;
    int cr_as_local;
    int non_argument_as_local;
    int non_volatile_as_local;
  } codegen_flags;
} PPCImageInfo;
