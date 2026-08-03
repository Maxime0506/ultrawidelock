<!-- generated documentation — edit the source, not this file -->
# `firmware/src/matter_thread_port.c`

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
`firmware/src/matter_thread_port.c:125`

Is this an address something off the Thread mesh could route to?
NOT a test for "is it global". A border router's off-mesh-routable prefix is
very often a unique-local one, indistinguishable from the mesh-local prefix
by its first byte -- an earlier version of this checked fc00::/7 and would
have reported a perfectly routable OMR address as unreachable. The only
sound test is against the mesh-local prefix this network actually uses.

**called by** `count_offmesh`, `log_addresses`

### `static int count_offmesh(otInstance *ot)`
`firmware/src/matter_thread_port.c:143`

Count the number of preferred off-mesh unicast addresses this node holds. Iterates over Thread
unicast addresses and counts those marked preferred that route to a destination not on the mesh.

**called by** `log_addresses`, `matter_thread_wait_attached`  ·  **calls** `addr_is_offmesh`

### `static void log_addresses(otInstance *ot)`
`firmware/src/matter_thread_port.c:166`

Every address this node holds, and whether any of them is reachable.
A registered SRP name is not the same as a reachable node. Auto host address
mode publishes the PREFERRED unicast addresses, and falls back to the
mesh-local EID when there are none -- and a mesh-local address does not leave
the Thread mesh, so a commissioner on Wi-Fi resolves the name and then routes
nowhere. That failure is invisible from the SRP result, which is why it is
printed here instead of assumed.

**called by** `matter_thread_advertise`, `matter_thread_wait_attached`, `srp_cb`  ·  **calls** `addr_is_offmesh`, `count_offmesh`

### `struct srp_reg`
`firmware/src/matter_thread_port.c:258`

One registration per fabric, because a node on two fabrics has two names.
The instance name is derived from the compressed fabric id and this node's id
ON that fabric, so the second administrator resolving the first fabric's name
finds an address it cannot open a session to. A single slot here published
whichever fabric registered last and left the other unreachable.

### `static int host_id_read(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)`
`firmware/src/matter_thread_port.c:275`

Settings callback to read a 32-bit host ID from persistent storage. Reads exactly
sizeof(uint32_t) bytes into the output parameter, returning 0 on all paths.

### `static uint32_t srp_host_id(void)`
`firmware/src/matter_thread_port.c:295`

The host-name suffix: read it, or mint one and keep it. See SRP_HOST_ID_KEY.
Zero is the "not stored" marker, so it is never a valid id -- which costs one
value out of 2^32 and saves carrying a separate "have I got one" flag through
the settings backend.

**called by** `matter_thread_advertise`

### `void matter_thread_peer_current(struct matter_thread_peer *out)`
`firmware/src/matter_thread_port.c:349`

Copy the current inbound peer address and port to the caller's buffer. Called by Matter protocol
handlers to discover where a datagram came from. Returns without effect if out is NULL.

### `int matter_thread_send_to(const struct matter_thread_peer *peer, const uint8_t *msg, size_t len)`
`firmware/src/matter_thread_port.c:362`

Send a datagram to a peer over Thread UDP. Returns MATTER_E_STATE if the peer is invalid, the
address family is unsupported, the message buffer cannot be allocated, or the send fails;
MATTER_E_NOSPACE if the message buffer is full; MATTER_OK on success.

### `static void udp_rx(void *ctx, otMessage *msg, const otMessageInfo *info)`
`firmware/src/matter_thread_port.c:404`

OpenThread UDP RX callback. Reads the incoming datagram, logs its size, invokes
matter_thread_on_datagram to generate a reply, and sends the reply back to the peer. Uses static
buffers sized for Sigma3 (RX) and full subscription reports (reply) respectively. Temporarily
publishes the peer address so the Matter handler can discover where traffic arrived from.

### `static void srp_cb(otError err, const otSrpClientHostInfo *host, const otSrpClientService *services, const otSrpClientService *removed, void *ctx)`
`firmware/src/matter_thread_port.c:504`

The SRP server's verdict, which otSrpClientAddService() cannot give.

**calls** `log_addresses`

### `void matter_thread_advertise_reset(void)`
`firmware/src/matter_thread_port.c:526`

Release all SRP registrations for the host and services. Clears both the SRP client state and the
local registration cache, so the next advertise re-registers from scratch. Called when fabrics
are rolled back to avoid leaving dangling registrations under old names.

### `int matter_thread_advertise(const char *instance_name, uint16_t port)`
`firmware/src/matter_thread_port.c:708`

Advertise this node's services to SRP. Returns MATTER_E_STATE; Thread is not built into this
image.

**calls** `log_addresses`, `srp_host_id`

### `int matter_thread_start(const uint8_t *dataset, size_t len)`
`firmware/src/matter_thread_port.c:720`

Start Thread with the provided operational dataset. Returns MATTER_E_STATE; Thread is not built
into this image.

### `int matter_thread_wait_attached(uint32_t timeout_ms)`
`firmware/src/matter_thread_port.c:733`

Stub: always returns MATTER_E_TIMEOUT. Thread attachment checking is not implemented on this
target.

**calls** `count_offmesh`, `log_addresses`
