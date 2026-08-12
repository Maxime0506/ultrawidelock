/*
 * ha_mqtt — native Home Assistant MQTT publishing for the ESP32 Matter lock.
 *
 * Publishes UWB distance and Aliro access verdicts on the topics and payloads
 * Home Assistant's MQTT Discovery expects, so an existing install picks this
 * board up with no host-side agent running:
 *
 *   homeassistant/sensor/<node>/distance/config   retained, QoS 1
 *   homeassistant/event/<node>/access/config      retained, QoS 1
 *   aliro/<node>/status    online | offline, retained, QoS 1, also the last will
 *   aliro/<node>/distance  bare integer millimetres, not retained, QoS 0
 *   aliro/<node>/access    {"event_type":"granted"|"denied"}, not retained, QoS 0
 *
 * Lock control stays on Matter; this carries only what Matter does not expose,
 * matching the split the agent already makes.
 *
 * Everything here is compiled out unless CONFIG_ENABLE_HA_MQTT is set. The
 * broker host, port, login, CA certificate and node name are runtime NVS config
 * provisioned with the `hamqtt` console command — nothing is compiled in.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bring the publisher up once the station has an address. Idempotent and
 *  cheap: it only spawns the publisher task, which then does the NVS read, the
 *  TLS connect and every publish. Safe to call from the Matter event callback.
 *  No-op when the broker has not been provisioned. */
void ha_mqtt_start_once(void);

/** Queue one conditioned approach distance, in centimetres (the estimate the
 *  unlock thresholds act on, not a raw block). Never blocks: a full queue drops
 *  the sample. Rate-limited to the agent's own interval before it is queued. */
void ha_mqtt_publish_distance_cm(int32_t cm);

/** Queue one credential-independent access verdict. Never blocks: a full queue
 *  drops the event. Matches ultrawidelock_reader's access-listener signature, so it can
 *  be registered directly. */
void ha_mqtt_publish_access(bool granted);

/** `hamqtt` console command: provision the broker, show state, start. Registered
 *  from app_shell.cpp alongside the other commands. */
int ha_mqtt_shell_cmd(int argc, char **argv);

#ifdef __cplusplus
}
#endif
