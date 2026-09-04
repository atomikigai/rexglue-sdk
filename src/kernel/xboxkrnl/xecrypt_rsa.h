/**
 ******************************************************************************
 * ReXGlue portable Xbox 360 RSA helpers.
 ******************************************************************************
 */

#ifndef REX_KERNEL_XBOXKRNL_XECRYPT_RSA_H_
#define REX_KERNEL_XBOXKRNL_XECRYPT_RSA_H_

#include <cstdint>

namespace rex::kernel::xboxkrnl {

// Xbox big numbers contain big-endian uint64_t limbs in least-significant
// limb-first order. Returns 1 on success and 0 when the operation is invalid.
uint32_t XeCryptBnQwNeRsaPubCryptPortable(const uint8_t* input, uint8_t* output,
                                          const uint8_t* modulus, uint32_t num_qwords,
                                          uint32_t exponent);

}  // namespace rex::kernel::xboxkrnl

#endif  // REX_KERNEL_XBOXKRNL_XECRYPT_RSA_H_
