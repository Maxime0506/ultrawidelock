/* stackfake: aliro/connection_handle.h.
 *
 * Two things matter to the code under test: whether a handle is NFC or BLE,
 * and whether two handles are the same session. Upstream carries a bt_conn
 * pointer for the BLE case; an id serves the same purpose here and lets a
 * suite hold several distinct BLE sessions at once, which is the only way to
 * exercise the session table filling up. */
#ifndef STACKFAKE_ALIRO_CONNECTION_HANDLE_H
#define STACKFAKE_ALIRO_CONNECTION_HANDLE_H

#include <cstdint>

namespace Aliro
{

/** Identifies one link the stack holds a session on. */
class ConnectionHandle
{
      public:
	static ConnectionHandle Nfc() { return ConnectionHandle(kNfc, 0); }
	static ConnectionHandle Ble(uint8_t id) { return ConnectionHandle(kBle, id); }

	bool IsNfc() const { return mKind == kNfc; }
	bool IsBle() const { return mKind == kBle; }
	uint8_t Id() const { return mId; }

	friend bool operator==(const ConnectionHandle &lhs, const ConnectionHandle &rhs)
	{
		return lhs.mKind == rhs.mKind && lhs.mId == rhs.mId;
	}
	friend bool operator!=(const ConnectionHandle &lhs, const ConnectionHandle &rhs)
	{
		return !(lhs == rhs);
	}

      private:
	enum Kind : uint8_t { kNfc, kBle };
	ConnectionHandle(Kind kind, uint8_t id) : mKind(kind), mId(id) {}

	Kind mKind;
	uint8_t mId;
};

} // namespace Aliro

#endif /* STACKFAKE_ALIRO_CONNECTION_HANDLE_H */
