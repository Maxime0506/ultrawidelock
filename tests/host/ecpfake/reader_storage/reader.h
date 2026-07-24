/* ecpfake: minimal reader_storage/reader.h — provisioning knobs live in
 * ecpfake (rfal_rf.h); implementations in test_nfc_ecp.cpp. */
#ifndef ECPFAKE_READER_STORAGE_READER_H
#define ECPFAKE_READER_STORAGE_READER_H

#include <aliro/types.h>

namespace DoorLock
{
namespace ReaderStorage
{
bool IsIdentifierSet(void);
int GetIdentifier(Aliro::Identifier &out);
} // namespace ReaderStorage
} // namespace DoorLock

#endif /* ECPFAKE_READER_STORAGE_READER_H */
