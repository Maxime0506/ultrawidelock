/* ecpfake: minimal aliro/types.h — the 8-byte Reader Identifier container.
 * Only .data() and 8 bytes of storage matter to nfc_prop_ecp.cpp. */
#ifndef ECPFAKE_ALIRO_TYPES_H
#define ECPFAKE_ALIRO_TYPES_H

#include <array>
#include <cstdint>

namespace Aliro
{
using Identifier = std::array<uint8_t, 8>;
} // namespace Aliro

#endif /* ECPFAKE_ALIRO_TYPES_H */
