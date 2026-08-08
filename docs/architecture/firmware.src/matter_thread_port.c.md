<!-- generated documentation — edit the source, not this file -->
# `firmware/src/matter_thread_port.c`

@file matter_thread_port.c — matter_thread.h on top of Zephyr's OpenThread.
The dataset arrives from the commissioner as raw meshcop TLVs and
otDatasetSetActiveTlvs() takes raw meshcop TLVs, so nothing here has to
understand the format -- which is the point. This node parses exactly one
field out of it, the Extended PAN ID, and only so it can name the network
back to the commissioner.
Built into every image. Without CONFIG_OPENTHREAD it refuses honestly
rather than disappearing: matter_clusters.c calls it unconditionally, and a
link error would be a worse way to learn that Thread was configured out.

## API

### `static bool addr_is_offmesh(otInstance *ot, const otNetifAddress *a)`
`firmware/src/matter_thread_port.c:160`

Is this an address something off the Thread mesh could route to?
NOT a test for "is it global". A border router's off-mesh-routable prefix is
very often a unique-local one, indistinguishable from the mesh-local prefix
by its first byte -- an earlier version of this checked fc00::/7 and would
have reported a perfectly routable OMR address as unreachable. The only
sound test is against the mesh-local prefix this network actually uses.

**called by** `count_offmesh`, `log_addresses`

### `static int count_offmesh(otInstance *ot)`
`firmware/src/matter_thread_port.c:178`

Count the number of preferred off-mesh unicast addresses this node holds. Iterates over Thread
unicast addresses and counts those marked preferred that route to a destination not on the mesh.

**called by** `log_addresses`, `matter_thread_wait_attached`  ·  **calls** `addr_is_offmesh`

### `static void log_addresses(otInstance *ot)`
`firmware/src/matter_thread_port.c:201`

Every address this node holds, and whether any of them is reachable.
A registered SRP name is not the same as a reachable node. Auto host address
mode publishes the PREFERRED unicast addresses, and falls back to the
mesh-local EID when there are none -- and a mesh-local address does not leave
the Thread mesh, so a commissioner on Wi-Fi resolves the name and then routes
nowhere. That failure is invisible from the SRP result, which is why it is
printed here instead of assumed.

**called by** `matter_thread_advertise`, `matter_thread_wait_attached`, `srp_cb`  ·  **calls** `addr_is_offmesh`, `count_offmesh`

### `struct srp_reg`
`firmware/src/matter_thread_port.c:354`

One registration per fabric, because a node on two fabrics has two names.
The instance name is derived from the compressed fabric id and this node's id
ON that fabric, so the second administrator resolving the first fabric's name
finds an address it cannot open a session to. A single slot here published
whichever fabric registered last and left the other unreachable.

### `static int host_id_read(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)`
`firmware/src/matter_thread_port.c:382`

Settings callback to read a 32-bit host ID from persistent storage. Reads exactly
sizeof(uint32_t) bytes into the output parameter, returning 0 on all paths.

### `static uint32_t srp_host_id(void)`
`firmware/src/matter_thread_port.c:402`

The host-name suffix: read it, or mint one and keep it. See SRP_HOST_ID_KEY.
Zero is the "not stored" marker, so it is never a valid id -- which costs one
value out of 2^32 and saves carrying a separate "have I got one" flag through
the settings backend.

**called by** `srp_host_name_build`

### `void matter_thread_peer_current(struct matter_thread_peer *out)`
`firmware/src/matter_thread_port.c:456`

Copy the current inbound peer address and port to the caller's buffer. Called by Matter protocol
handlers to discover where a datagram came from. Returns without effect if out is NULL.

### `int matter_thread_send_to(const struct matter_thread_peer *peer, const uint8_t *msg, size_t len)`
`firmware/src/matter_thread_port.c:469`

Send a datagram to a peer over Thread UDP. Returns MATTER_E_STATE if the peer is invalid, the
address family is unsupported, the message buffer cannot be allocated, or the send fails;
MATTER_E_NOSPACE if the message buffer is full; MATTER_OK on success.

### `static void udp_rx(void *ctx, otMessage *msg, const otMessageInfo *info)`
`firmware/src/matter_thread_port.c:511`

OpenThread UDP RX callback. Reads the incoming datagram, logs its size, invokes
matter_thread_on_datagram to generate a reply, and sends the reply back to the peer. Uses static
buffers sized for Sigma3 (RX) and full subscription reports (reply) respectively. Temporarily
publishes the peer address so the Matter handler can discover where traffic arrived from.

### `static void srp_diag_state_changed(otChangedFlags flags, void *context)`
`firmware/src/matter_thread_port.c:629`

Whether auto-start ever FOUND a server, printed at the only moment that can change.
srp_cb() below is the verdict on a registration that was sent. It says nothing
about one that was never sent, and the two failures are indistinguishable from
every other log this firmware prints: host and services sit at ToAdd either
way, and srp_cb() is simply never called, so the "SRP registration FAILED"
line reads as absent-because-fine rather than absent-because-nothing-happened.
otSrpClientEnableAutoStartMode() picks its server out of Thread network data,
so network data changing is the only event that can turn "no server" into
"server". Hence OT_CHANGED_THREAD_NETDATA rather than a timer: a timer would
print the same answer repeatedly and still miss the transition.
Runs on the OpenThread thread with the API lock already held, which is why
nothing here takes openthread_mutex_lock() -- the same reason thread_gate.c's
callback does not.

### `static void srp_cb(otError err, const otSrpClientHostInfo *host, const otSrpClientService *services, const otSrpClientService *removed, void *ctx)`
`firmware/src/matter_thread_port.c:663`

The SRP server's verdict, which otSrpClientAddService() cannot give.

**calls** `log_addresses`

### `void matter_thread_advertise_reset(void)`
`firmware/src/matter_thread_port.c:685`

Release all SRP registrations for the host and services. Clears both the SRP client state and the
local registration cache, so the next advertise re-registers from scratch. Called when fabrics
are rolled back to avoid leaving dangling registrations under old names.

### `static void srp_host_name_build(otInstance *ot)`
`firmware/src/matter_thread_port.c:746`

The host name, built outside the OpenThread lock because srp_host_id() reaches
into the settings backend and that is not somewhere to go holding it.
The host name only has to be unique on the SRP server, and the EUI-64 already
is -- across boards. The suffix is what makes it unique across this board's
own erases; see SRP_HOST_ID_KEY.

**called by** `matter_thread_advertise`, `matter_thread_advertise_commissionable`  ·  **calls** `srp_host_id`

### `static otError srp_host_register(otInstance *ot)`
`firmware/src/matter_thread_port.c:766`

The HOST is registered once; every service hangs off it, operational and
commissionable alike, and whichever registers first brings it up for the
other. Caller holds the OpenThread lock and has already built s_host_name.
Calling otSrpClientSetHostName() again once the client is running returns
OT_ERROR_INVALID_STATE (13) and takes the whole registration down with it --
which is what refused the second fabric after its AddNOC was accepted, leaving
the new administrator a fabric it could not resolve.

**called by** `matter_thread_advertise`, `matter_thread_advertise_commissionable`

### `int matter_thread_advertise(const char *instance_name, uint16_t port)`
`firmware/src/matter_thread_port.c:1151`

Advertise this node's services to SRP. Returns MATTER_E_STATE; Thread is not built into this
image.

**calls** `log_addresses`, `srp_host_name_build`, `srp_host_register`

### `int matter_thread_advertise_commissionable(uint16_t discriminator, uint16_t port)`
`firmware/src/matter_thread_port.c:1162`

Publish the commissionable service. Returns MATTER_E_STATE; Thread is not built into this image.

**calls** `srp_host_name_build`, `srp_host_register`

### `int matter_thread_unadvertise(const char *instance_name)`
`firmware/src/matter_thread_port.c:1173`

Withdraw one operational service. Returns MATTER_OK; nothing was ever registered.

### `int matter_thread_unadvertise_commissionable(void)`
`firmware/src/matter_thread_port.c:1183`

Withdraw the commissionable service. Returns MATTER_OK; nothing was ever registered.

### `void matter_thread_dump_active_dataset(void)`
`firmware/src/matter_thread_port.c:1191`

Print the active dataset. No-op; Thread is not built into this image.

### `int matter_thread_start(const uint8_t *dataset, size_t len)`
`firmware/src/matter_thread_port.c:1199`

Start Thread with the provided operational dataset. Returns MATTER_E_STATE; Thread is not built
into this image.

### `bool matter_thread_attached_to(const uint8_t *xpanid)`
`firmware/src/matter_thread_port.c:1212`

Is this node already on that network? Always false; there is no Thread stack in this image, so
it is on no network at all.

### `int matter_thread_wait_attached(uint32_t timeout_ms)`
`firmware/src/matter_thread_port.c:1223`

Stub: always returns MATTER_E_TIMEOUT. Thread attachment checking is not implemented on this
target.

**calls** `count_offmesh`, `log_addresses`
