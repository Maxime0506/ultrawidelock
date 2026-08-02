<!-- generated documentation — edit the source, not this file -->
# `ports/dwm3001cdk/app/src/matter_thread_port.c`

@file matter_thread_port.c — matter_thread.h on top of Zephyr's OpenThread.
The dataset arrives from the commissioner as raw meshcop TLVs and
otDatasetSetActiveTlvs() takes raw meshcop TLVs, so nothing here has to
understand the format -- which is the point. This node parses exactly one
field out of it, the Extended PAN ID, and only so it can name the network
back to the commissioner.
Built into every image. Without CONFIG_NET_L2_OPENTHREAD it refuses honestly
rather than disappearing: matter_clusters.c calls it unconditionally, and a
link error would be a worse way to learn that Thread was configured out.

## API

### `static bool addr_is_offmesh(otInstance *ot, const otNetifAddress *a)`
`ports/dwm3001cdk/app/src/matter_thread_port.c:125`

Is this an address something off the Thread mesh could route to?
NOT a test for "is it global". A border router's off-mesh-routable prefix is
very often a unique-local one, indistinguishable from the mesh-local prefix
by its first byte -- an earlier version of this checked fc00::/7 and would
have reported a perfectly routable OMR address as unreachable. The only
sound test is against the mesh-local prefix this network actually uses.

**called by** `count_offmesh`, `log_addresses`

### `static void log_addresses(otInstance *ot)`
`ports/dwm3001cdk/app/src/matter_thread_port.c:162`

Every address this node holds, and whether any of them is reachable.
A registered SRP name is not the same as a reachable node. Auto host address
mode publishes the PREFERRED unicast addresses, and falls back to the
mesh-local EID when there are none -- and a mesh-local address does not leave
the Thread mesh, so a commissioner on Wi-Fi resolves the name and then routes
nowhere. That failure is invisible from the SRP result, which is why it is
printed here instead of assumed.

**called by** `matter_thread_advertise`, `matter_thread_wait_attached`, `srp_cb`  ·  **calls** `addr_is_offmesh`, `count_offmesh`

### `struct srp_reg`
`ports/dwm3001cdk/app/src/matter_thread_port.c:254`

One registration per fabric, because a node on two fabrics has two names.
The instance name is derived from the compressed fabric id and this node's id
ON that fabric, so the second administrator resolving the first fabric's name
finds an address it cannot open a session to. A single slot here published
whichever fabric registered last and left the other unreachable.

### `static uint32_t srp_host_id(void)`
`ports/dwm3001cdk/app/src/matter_thread_port.c:287`

The host-name suffix: read it, or mint one and keep it. See SRP_HOST_ID_KEY.
Zero is the "not stored" marker, so it is never a valid id -- which costs one
value out of 2^32 and saves carrying a separate "have I got one" flag through
the settings backend.

**called by** `matter_thread_advertise`

### `static void srp_cb(otError err, const otSrpClientHostInfo *host, const otSrpClientService *services, const otSrpClientService *removed, void *ctx)`
`ports/dwm3001cdk/app/src/matter_thread_port.c:481`

The SRP server's verdict, which otSrpClientAddService() cannot give.

**calls** `log_addresses`

<details><summary>Undocumented (9)</summary>

- `count_offmesh`
- `host_id_read`
- `matter_thread_peer_current`
- `matter_thread_send_to`
- `udp_rx`
- `matter_thread_advertise_reset`
- `matter_thread_advertise`
- `matter_thread_start`
- `matter_thread_wait_attached`

</details>
