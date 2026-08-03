/* nfcfake: aliro/aliro.h — the stack singleton as a recording double.
 *
 * The transport's whole obligation to the stack is three calls in the right
 * order: CreateSession on activation, HandleSessionData per response,
 * DestroySession on loss or failure. This counts them and keeps the last
 * payload; it never interprets an APDU. */
#ifndef NFCFAKE_ALIRO_ALIRO_H
#define NFCFAKE_ALIRO_ALIRO_H

#include <aliro/connection_handle.h>
#include <aliro/errors.h>
#include <aliro/types.h>

namespace Aliro
{

/** Recording stand-in for the Aliro stack singleton. */
class AliroStack
{
      public:
	static AliroStack &Instance();

	AliroError CreateSession(ConnectionHandle handle);
	void HandleSessionData(ConnectionHandle handle, Data data);
	void DestroySession(ConnectionHandle handle);
};

} // namespace Aliro

#endif /* NFCFAKE_ALIRO_ALIRO_H */
