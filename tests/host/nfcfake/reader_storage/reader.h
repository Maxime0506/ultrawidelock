/* nfcfake: reader_storage/reader.h — where the ECP frame's Reader Identifier
 * comes from. Both answers are knobs, because an unprovisioned reader must
 * still emit a (zero) identifier rather than skip the beacon. */
#ifndef NFCFAKE_READER_STORAGE_READER_H
#define NFCFAKE_READER_STORAGE_READER_H

#include <aliro/errors.h>
#include <aliro/types.h>

namespace DoorLock
{
namespace ReaderStorage
{

bool IsIdentifierSet(void);
AliroError GetIdentifier(Aliro::Identifier &out);

} // namespace ReaderStorage
} // namespace DoorLock

#endif /* NFCFAKE_READER_STORAGE_READER_H */
