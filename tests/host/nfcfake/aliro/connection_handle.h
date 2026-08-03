/* nfcfake: aliro/connection_handle.h — which link a session belongs to.
 * Only the NFC constructor is reachable from these transports. */
#ifndef NFCFAKE_ALIRO_CONNECTION_HANDLE_H
#define NFCFAKE_ALIRO_CONNECTION_HANDLE_H

#include <cstdint>

namespace Aliro
{

/** Identifies one link the stack has a session on. */
class ConnectionHandle
{
      public:
	static ConnectionHandle Nfc() { return ConnectionHandle(kNfc); }

	bool IsNfc() const { return mKind == kNfc; }
	bool IsBle() const { return mKind == kBle; }

	bool operator==(const ConnectionHandle &other) const { return mKind == other.mKind; }

      private:
	enum Kind : uint8_t { kNfc, kBle };
	explicit ConnectionHandle(Kind kind) : mKind(kind) {}
	Kind mKind;
};

} // namespace Aliro

#endif /* NFCFAKE_ALIRO_CONNECTION_HANDLE_H */
