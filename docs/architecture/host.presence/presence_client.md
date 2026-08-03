<!-- generated documentation — edit the source, not this file -->
# `host/presence/presence_client.py`

Client and command gate for the local presenced Unix socket.

**depends on** [`host/presence/presence_service.py`](presence_service.md)  ·  **used by** [`host/presence/presence-run`](presence-run.md)

## API

### `class PresenceClientError(RuntimeError)`
`host/presence/presence_client.py:19`

Base exception for client-side presence proof errors (socket, validation, daemon rejection).

**called by** `_validate_response`, `_validate_socket`, `execute_command`, `request_presence`

### `class PresenceDenied(PresenceClientError)`
`host/presence/presence_client.py:24`

Exception raised when the presence daemon denies the proof request (proof failed, stale, or out of range).

**called by** `client_main`, `execute_command`

### `_validate_response(response) -> dict`
`host/presence/presence_client.py:29`

Validate a presenced response JSON object. Raises PresenceClientError if the response structure is invalid or contradictory (e.g., ok:true with missing/invalid distance, or ok:false with missing code/reason). Returns the validated response dict.

**called by** `execute_command`, `request_presence`  ·  **calls** `PresenceClientError`

### `_validate_socket(path: str)`
`host/presence/presence_client.py:48`

Validate the presenced socket path: must exist, be a Unix socket, be owned by the current user, and have no group or other access. Raises PresenceClientError if any check fails.

**called by** `request_presence`  ·  **calls** `PresenceClientError`

### `request_presence(path: str, max_cm: int, timeout: float=15.0) -> dict`
`host/presence/presence_client.py:62`

Request one fresh proof. The daemon, never the client, mints the nonce.

**called by** `client_main`  ·  **calls** `PresenceClientError`, `_validate_response`, `_validate_socket`

### `execute_command(response: dict, command: list[str], runner=subprocess.run) -> int`
`host/presence/presence_client.py:99`

Run exact argv only after success, preserving exit and signal status.

**called by** `client_main`  ·  **calls** `PresenceClientError`, `PresenceDenied`, `_validate_response`

### `positive_cm(value: str) -> int`
`host/presence/presence_client.py:112`

Parse and validate a positive integer argument in cm for the distance threshold.

### `positive_timeout(value: str) -> float`
`host/presence/presence_client.py:123`

Parse and validate a positive float argument in seconds for the socket and proof deadline.

### `build_client_parser()`
`host/presence/presence_client.py:134`

Build an argparse parser for the presence-run CLI: socket path, distance threshold in cm, timeout in seconds, and the command to run.

**called by** `client_main`

### `client_main(argv=None) -> int`
`host/presence/presence_client.py:152`

Main entry point for presence-run CLI. Request a fresh proof from presenced, confirm it succeeded, print the distance, then execute the given command only if presence succeeded. Returns 0 on success, 1 on proof denial, 2 on unavailable socket/daemon, 126 on execution error, 127 on command not found.

**calls** `PresenceDenied`, `build_client_parser`, `execute_command`, `request_presence`
