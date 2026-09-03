test_fmadd_1:
  #_ REGISTER_IN f1 0.0
  #_ REGISTER_IN f2 5.0
  #_ REGISTER_IN f3 5.0
  #_ REGISTER_IN f4 0.0
  fmadd f1, f2, f3, f4
  blr
  #_ REGISTER_OUT f1 25.0
  #_ REGISTER_OUT f2 5.0
  #_ REGISTER_OUT f3 5.0
  #_ REGISTER_OUT f4 0.0

test_fmadd_2:
  #_ REGISTER_IN f1 0.0
  #_ REGISTER_IN f2 5.0
  #_ REGISTER_IN f3 0.0
  #_ REGISTER_IN f4 15.0
  fmadd f1, f2, f3, f4
  blr
  #_ REGISTER_OUT f1 15.0
  #_ REGISTER_OUT f2 5.0
  #_ REGISTER_OUT f3 0.0
  #_ REGISTER_OUT f4 15.0

test_fmadd_3:
  #_ REGISTER_IN f1 0.0
  #_ REGISTER_IN f2 5.0
  #_ REGISTER_IN f3 5.0
  #_ REGISTER_IN f4 15.0
  fmadd f1, f2, f3, f4
  blr
  #_ REGISTER_OUT f1 40.0
  #_ REGISTER_OUT f2 5.0
  #_ REGISTER_OUT f3 5.0
  #_ REGISTER_OUT f4 15.0

test_fmadd_4:
  #_ REGISTER_IN f1 0.0
  #_ REGISTER_IN f2 9999.99
  #_ REGISTER_IN f3 9999.99
  #_ REGISTER_IN f4 9999.99
  fmadd f1, f2, f3, f4
  blr
  #_ REGISTER_OUT f1 100009799.9901
  #_ REGISTER_OUT f2 9999.99
  #_ REGISTER_OUT f3 9999.99
  #_ REGISTER_OUT f4 9999.99

# Hex-bit-pattern variant of test_fmadd_1 (2.0*3.0+1.0=7.0), added for
# tests/ppc/c_harness's hand-written C/C++ parity harness (generate_harness.cpp),
# which only supports raw-bits REGISTER_IN/OUT literals, not decimal floats.
test_fmadd_hex_1:
  #_ REGISTER_IN f2 0x4000000000000000
  #_ REGISTER_IN f3 0x4008000000000000
  #_ REGISTER_IN f4 0x3FF0000000000000
  fmadd f1, f2, f3, f4
  blr
  #_ REGISTER_OUT f1 0x401C000000000000
  #_ REGISTER_OUT f2 0x4000000000000000
  #_ REGISTER_OUT f3 0x4008000000000000
  #_ REGISTER_OUT f4 0x3FF0000000000000
