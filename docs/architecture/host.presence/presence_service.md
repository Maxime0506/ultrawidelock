<!-- generated documentation — edit the source, not this file -->
# `host/presence/presence_service.py`

Fresh, pinned presence proofs behind an owner-only Unix socket.

**depends on** [`tools/presence_git.py`](../tools/presence_git.md), [`tools/presence_verify.py`](../tools/presence_verify.md)  ·  **used by** [`host/presence/presence-enroll`](presence-enroll.md), [`host/presence/presence_client.py`](presence_client.md), [`host/presence/presenced`](presenced.md)

## API

### `class ServiceError(RuntimeError)`
`host/presence/presence_service.py:33`

Configuration or startup failure that keeps the daemon fail-closed.

**called by** `PresenceService.__init__`, `_prepare_socket_path`, `_secure_directory`, `connect_engine`, `enroll_device`, `select_enrollment`

### `class Enrollment`
`host/presence/presence_service.py:38`

One pinned device point and the only credential it may prove.

**called by** `enroll_device`, `select_enrollment`

### `select_enrollment(path: str, key_id: str | None=None) -> Enrollment`
`host/presence/presence_service.py:47`

Load exactly one enrollment, or select one explicitly by key id.

**called by** `daemon_main`  ·  **calls** `Enrollment`, `ServiceError`

### `enroll_device(port: str, path: str=DEFAULT_ENROLLED, replace: bool=False)`
`host/presence/presence_service.py:82`

Pin the attached device and credential into one owner-only local file.

**called by** `enroll_main`  ·  **calls** `Enrollment`, `PresenceEngine.close`, `ServiceError`, `_secure_directory`

### `class PresenceEngine`
`host/presence/presence_service.py:132`

Own the serial device and serialize fresh challenge/proof transactions.

**called by** `connect_engine`

#### `PresenceEngine.prove(self, max_cm: int) -> dict`
`host/presence/presence_service.py:145`

Mint one nonce, acquire one fresh proof, and return a safe verdict.

**called by** `PresenceService.handle`

### `connect_engine(port: str, enrolled: Enrollment, openssl: str='openssl') -> PresenceEngine`
`host/presence/presence_service.py:184`

Open the device once and pin both identities before serving requests.

**called by** `daemon_main`  ·  **calls** `PresenceEngine`, `PresenceEngine.close`, `ServiceError`

### `class PresenceService`
`host/presence/presence_service.py:204`

Validate socket requests before allowing them to touch the serial device.

**called by** `daemon_main`

### `class PresenceUnixServer(socketserver.UnixStreamServer)`
`host/presence/presence_service.py:316`

Single-worker Unix server; every serial proof is inherently serialized.

**called by** `daemon_main`

<details><summary>Undocumented (17)</summary>

- `_secure_directory`
- `PresenceEngine.__init__`
- `PresenceEngine.close`
- `PresenceService.__init__`
- `PresenceService.handle`
- `_RequestHandler`
- `_RequestHandler._send`
- `_RequestHandler.handle`
- `_prepare_socket_path`
- `PresenceUnixServer.__init__`
- `PresenceUnixServer.close_and_unlink`
- `positive_cm`
- `build_daemon_parser`
- `build_enroll_parser`
- `enroll_main`
- `daemon_main`
- `stop`

</details>
