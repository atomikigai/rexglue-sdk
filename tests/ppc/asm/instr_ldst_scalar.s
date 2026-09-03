# Minimal round-trip coverage for scalar D-form loads/stores (lwz/stw/lbz/std/ld).
# No existing instr_l*.s/instr_st*.s file covers plain scalar memory access
# (only the vector lvl/lvr/stvl/stvr/stvew/lvsl/lvsr/lvexx family exists), so
# this file is a fresh minimal addition per the C backend gap list.
#
# Base address picked to land in the same mapped guest region other memory
# tests use (see tests/ppc/asm/instr_lvl.s's 0x10001077); low/absolute
# offsets from r0 are unmapped guard space in the test harness's guest arena
# and SEGV, so this uses a real base register instead of rA=0.
test_ldst_1:
  lis r3, 0x1000
  ori r3, r3, 0x2000

  lis r4, 0x1234
  ori r4, r4, 0x5678
  stw r4, 16(r3)
  lwz r5, 16(r3)

  li r6, 0xAB
  stb r6, 20(r3)
  lbz r7, 20(r3)

  lis r8, 0x1122
  ori r8, r8, 0x3344
  sldi r8, r8, 32
  oris r8, r8, 0x5566
  ori r8, r8, 0x7788
  std r8, 32(r3)
  ld r9, 32(r3)

  blr
  #_ REGISTER_OUT r5 0x0000000012345678
  #_ REGISTER_OUT r7 0x00000000000000AB
  #_ REGISTER_OUT r9 0x1122334455667788
