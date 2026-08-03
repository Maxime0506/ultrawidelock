/* stackfake: aliro/protocol_version.h. */
#ifndef STACKFAKE_ALIRO_PROTOCOL_VERSION_H
#define STACKFAKE_ALIRO_PROTOCOL_VERSION_H

#include <cstdint>

namespace Aliro
{
/** Major byte then minor byte, big-endian on the wire. 0x0100 is Aliro 1.0. */
using ProtocolVersion = uint16_t;
} // namespace Aliro

#endif /* STACKFAKE_ALIRO_PROTOCOL_VERSION_H */
