/* nfcfake: aliro/types.h — the reader identifier and the counted buffer the
 * transport seam passes around. */
#ifndef NFCFAKE_ALIRO_TYPES_H
#define NFCFAKE_ALIRO_TYPES_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace Aliro
{

/** 8-byte Reader Identifier: group id then group sub-id. */
using Identifier = std::array<uint8_t, 8>;

/** A mutable counted buffer, as the stack hands APDUs to a transport. */
struct Data {
	uint8_t *mData;
	size_t mLength;
};

} // namespace Aliro

#endif /* NFCFAKE_ALIRO_TYPES_H */
