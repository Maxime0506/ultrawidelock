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

### `_secure_directory(path: str)`
`host/presence/presence_service.py:71`

Create a directory with 0o700 permissions (owner-only), validating that it is owned by the current user. Raises ServiceError if the directory exists with different ownership or permissions.

**called by** `_prepare_socket_path`, `enroll_device`  ·  **calls** `ServiceError`

### `enroll_device(port: str, path: str=DEFAULT_ENROLLED, replace: bool=False)`
`host/presence/presence_service.py:83`

Pin the attached device and credential into one owner-only local file.

**called by** `enroll_main`  ·  **calls** `Enrollment`, `PresenceEngine.close`, `ServiceError`, `_secure_directory`

### `class PresenceEngine`
`host/presence/presence_service.py:133`

Own the serial device and serialize fresh challenge/proof transactions.

**called by** `connect_engine`

#### `PresenceEngine.__init__(self, serial, point: bytes, cred_id: bytes, openssl: str='openssl')`
`host/presence/presence_service.py:136`

Initialize a presence proof engine with a serial port, curve point, and credential ID. Stores the serial port, point, credential ID, and openssl binary name for use in later prove operations.

**called by** `PresenceUnixServer.__init__`

#### `PresenceEngine.close(self)`
`host/presence/presence_service.py:144`

Close the serial port held by this presence engine.

**called by** `_prepare_socket_path`, `connect_engine`, `daemon_main`, `enroll_device`

#### `PresenceEngine.prove(self, max_cm: int) -> dict`
`host/presence/presence_service.py:148`

Mint one nonce, acquire one fresh proof, and return a safe verdict.

**called by** `PresenceService.handle`

### `connect_engine(port: str, enrolled: Enrollment, openssl: str='openssl') -> PresenceEngine`
`host/presence/presence_service.py:187`

Open the device once and pin both identities before serving requests.

**called by** `daemon_main`  ·  **calls** `PresenceEngine`, `PresenceEngine.close`, `ServiceError`

### `class PresenceService`
`host/presence/presence_service.py:207`

Validate socket requests before allowing them to touch the serial device.

**called by** `daemon_main`

#### `PresenceService.__init__(self, engine: PresenceEngine, max_cm: int=40)`
`host/presence/presence_service.py:210`

Initialize a presence service with an engine and maximum distance policy. Validates that max_cm is a positive integer, raising ServiceError otherwise.

**calls** `ServiceError`

#### `PresenceService.handle(self, request) -> dict`
`host/presence/presence_service.py:217`

Handle an incoming presence proof request. Validates the request contains only op and max_cm, that op is prove, and that the requested distance is a positive integer not exceeding the daemon's policy. Returns ok=true with the proof on success or ok=false with a code and reason on validation failure.

**called by** `_RequestHandler.handle`  ·  **calls** `PresenceEngine.prove`

### `class _RequestHandler(socketserver.StreamRequestHandler)`
`host/presence/presence_service.py:248`

Socket request handler for presence proof requests. Reads one JSON request line with timeout, parses it, invokes the presence service, and writes the response as JSON back to the client. Silently handles broken pipes and malformed input by responding with structured error codes rather than closing the connection.

#### `_RequestHandler._send(self, response)`
`host/presence/presence_service.py:250`

Send a JSON response line to the client. Serializes the response dict to JSON, appends a newline, and writes it to the socket. Silently handles broken pipe and connection reset errors so the server does not crash on client disconnect.

**called by** `_RequestHandler.handle`

#### `_RequestHandler.handle(self)`
`host/presence/presence_service.py:258`

Parse one incoming JSON request line, validate it contains op and max_cm fields, dispatch to PresenceService.handle, and send a JSON response line to the client; silently handles client disconnect.

**calls** `PresenceService.handle`, `_RequestHandler._send`

### `_prepare_socket_path(path: str)`
`host/presence/presence_service.py:295`

Prepare a Unix socket path by securing its parent directory, validating the path is either absent or is a stale socket owned by the current user, and unlinking stale sockets; raises ServiceError if the socket is active or unowned.

**called by** `PresenceUnixServer.__init__`  ·  **calls** `PresenceEngine.close`, `ServiceError`, `_secure_directory`

### `class PresenceUnixServer(socketserver.UnixStreamServer)`
`host/presence/presence_service.py:325`

Single-worker Unix server; every serial proof is inherently serialized.

**called by** `daemon_main`

#### `PresenceUnixServer.__init__(self, path: str, service: PresenceService, request_timeout: float=DEFAULT_REQUEST_TIMEOUT_S)`
`host/presence/presence_service.py:328`

Initialize a Unix socket presence server: prepares the socket path, stores service reference and request timeout, and locks down socket permissions to 0o600. Raises ServiceError if socket path is active or unowned by the current user.

**calls** `PresenceEngine.__init__`, `_prepare_socket_path`

#### `PresenceUnixServer.close_and_unlink(self)`
`host/presence/presence_service.py:344`

Close the Unix socket server and unlink the socket file, but only if the file at the expected path is still the same socket (by device and inode). Returns without unlinking if the file is no longer present or has been replaced.

**called by** `daemon_main`

### `positive_cm(value: str) -> int`
`host/presence/presence_service.py:358`

Parse and validate a distance threshold from a string argument. Returns the integer value if it is between 1 and 65534 inclusive, raising ArgumentTypeError otherwise.

### `build_daemon_parser()`
`host/presence/presence_service.py:369`

Build the argument parser for the presenced daemon. Defines options for serial port, socket path, enrollment file, key selection, maximum distance policy, and openssl binary path.

**called by** `daemon_main`

### `build_enroll_parser()`
`host/presence/presence_service.py:395`

Build the argument parser for the presence enrollment tool. Defines options for serial port, output enrollment file, and a flag to replace an existing enrollment.

**called by** `enroll_main`

### `enroll_main(argv=None) -> int`
`host/presence/presence_service.py:415`

Main entry point for the presence enrollment tool: parse arguments and enroll the connected device by pinning it to an owner-only file.

**calls** `build_enroll_parser`, `enroll_device`

### `daemon_main(argv=None) -> int`
`host/presence/presence_service.py:427`

Main entry point for the presenced daemon: parse arguments, connect to the device, start the Unix socket server, and handle SIGINT/SIGTERM by raising KeyboardInterrupt for clean shutdown.

**calls** `PresenceEngine.close`, `PresenceService`, `PresenceUnixServer`, `PresenceUnixServer.close_and_unlink`, `build_daemon_parser`, `connect_engine`, `select_enrollment`

### `stop(_signum, _frame)`
`host/presence/presence_service.py:439`

Signal handler that converts a SIGTERM or SIGINT into a KeyboardInterrupt to shut down the daemon cleanly.
