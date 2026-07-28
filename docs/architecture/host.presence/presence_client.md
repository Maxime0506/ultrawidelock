<!-- generated documentation — edit the source, not this file -->
# `host/presence/presence_client.py`

Client and command gate for the local presenced Unix socket.

**depends on** [`host/presence/presence_service.py`](presence_service.md)  ·  **used by** [`host/presence/presence-run`](presence-run.md)

## API

### `request_presence(path: str, max_cm: int, timeout: float=15.0) -> dict`
`host/presence/presence_client.py:58`

Request one fresh proof. The daemon, never the client, mints the nonce.

**called by** `client_main`  ·  **calls** `PresenceClientError`, `_validate_response`, `_validate_socket`

### `execute_command(response: dict, command: list[str], runner=subprocess.run) -> int`
`host/presence/presence_client.py:95`

Run exact argv only after success, preserving exit and signal status.

**called by** `client_main`  ·  **calls** `PresenceClientError`, `PresenceDenied`, `_validate_response`

<details><summary>Undocumented (8)</summary>

- `PresenceClientError`
- `PresenceDenied`
- `_validate_response`
- `_validate_socket`
- `positive_cm`
- `positive_timeout`
- `build_client_parser`
- `client_main`

</details>
