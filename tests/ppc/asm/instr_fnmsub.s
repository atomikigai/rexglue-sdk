# Minimal coverage for fnmsub: frD = -(frA*frC - frB). No existing instr_f*.s
# file covers it (only fmadd/fmadds have dedicated files); values chosen to
# be exact in double precision (no rounding), matching the style of
# tests/ppc/asm/instr_fmadd.s.
test_fnmsub_1:
  #_ REGISTER_IN f2 0x4000000000000000
  #_ REGISTER_IN f3 0x4008000000000000
  #_ REGISTER_IN f4 0x3FF0000000000000
  fnmsub f1, f2, f3, f4
  blr
  #_ REGISTER_OUT f1 0xC014000000000000
  #_ REGISTER_OUT f2 0x4000000000000000
  #_ REGISTER_OUT f3 0x4008000000000000
  #_ REGISTER_OUT f4 0x3FF0000000000000
