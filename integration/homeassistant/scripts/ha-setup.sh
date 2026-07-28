#!/usr/bin/env bash
# One command from nothing to a working OpenAliro Home Assistant agent.
#
# Generates the broker TLS material, installs it into the Home Assistant
# Mosquitto add-on over SSH, writes the agent configuration, and runs doctor.
# Every step is idempotent: re-running repairs whatever drifted.
#
# Override any default with an environment variable, for example
#   HA_SSH=my-hass BROKER_HOST=hass.lan ./ha-setup.sh
set -euo pipefail

HA_SSH="${HA_SSH:-homeassistant}"
BROKER_HOST="${BROKER_HOST:-homeassistant.local}"
BROKER_PORT="${BROKER_PORT:-8883}"
MQTT_USER="${MQTT_USER:-openaliro_agent}"
DEVICE_ID="${DEVICE_ID:-front-door}"
CONFIG_DIR="${CONFIG_DIR:-$HOME/.config/openaliro-ha}"
CERT_DAYS="${CERT_DAYS:-825}"

TLS_DIR="$CONFIG_DIR/tls"
CERT="$TLS_DIR/openaliro-mqtt.crt"
KEY="$TLS_DIR/openaliro-mqtt.key"
PASSWORD_FILE="$CONFIG_DIR/mqtt-password"
CONFIG="$CONFIG_DIR/agent.toml"
TOTAL=6

step() { printf '[%d/%d] %s\n' "$1" "$TOTAL" "$2"; }
fail() { printf 'ha-setup: %s\n' "$1" >&2; exit 1; }

# 1. Prerequisites -----------------------------------------------------------
step 1 'checking prerequisites'
for tool in openssl ssh scp python3; do
	command -v "$tool" >/dev/null || fail "$tool is required but not installed"
done
command -v openaliro-ha >/dev/null ||
	fail 'openaliro-ha is not on PATH; install it with pipx or pip first'
ssh -o BatchMode=yes -o ConnectTimeout=10 "$HA_SSH" true 2>/dev/null ||
	fail "cannot reach '$HA_SSH' over SSH without a password; check ~/.ssh/config"
mkdir -p "$TLS_DIR"
chmod 700 "$CONFIG_DIR" "$TLS_DIR"

# 2. TLS material ------------------------------------------------------------
# Reused while it still has a month of life and matches the broker name, so a
# repair run does not needlessly invalidate the copy the agent already trusts.
reuse_cert=0
if [ -s "$CERT" ] && [ -s "$KEY" ] &&
	openssl x509 -in "$CERT" -noout -checkend 2592000 >/dev/null 2>&1 &&
	openssl x509 -in "$CERT" -noout -ext subjectAltName 2>/dev/null | grep -q "DNS:$BROKER_HOST"; then
	reuse_cert=1
fi
if [ "$reuse_cert" = 1 ]; then
	step 2 "reusing TLS certificate (SAN $BROKER_HOST)"
else
	step 2 "generating TLS certificate (SAN $BROKER_HOST, ${CERT_DAYS}d)"
	san="DNS:$BROKER_HOST"
	broker_ip="$(ping -c 1 -t 2 "$BROKER_HOST" 2>/dev/null |
		sed -n '1s/.*(\([0-9.]*\)).*/\1/p')"
	[ -n "$broker_ip" ] && san="$san,IP:$broker_ip"
	openssl req -x509 -newkey rsa:2048 -sha256 -days "$CERT_DAYS" -nodes \
		-keyout "$KEY" -out "$CERT" \
		-subj "/CN=$BROKER_HOST" \
		-addext "subjectAltName=$san" \
		-addext "basicConstraints=critical,CA:TRUE" \
		-addext "keyUsage=critical,digitalSignature,keyCertSign" \
		-addext "extendedKeyUsage=serverAuth" >/dev/null 2>&1
fi
chmod 600 "$CERT" "$KEY"

# 3. Install the certificate on the broker -----------------------------------
# Mosquitto reads certfile and keyfile from /ssl. If they are missing its TLS
# listener never starts and port 8883 refuses connections while 1883 stays up.
step 3 "installing certificate into $HA_SSH:/ssl"
scp -O -q "$CERT" "$HA_SSH:/ssl/openaliro-mqtt.crt"
scp -O -q "$KEY" "$HA_SSH:/ssl/openaliro-mqtt.key"

# 4. Configure and restart the broker ----------------------------------------
step 4 'configuring the Mosquitto add-on'
if [ -z "${MQTT_PASSWORD:-}" ]; then
	printf 'MQTT password for %s: ' "$MQTT_USER" >&2
	read -rs MQTT_PASSWORD
	printf '\n' >&2
fi
[ -n "$MQTT_PASSWORD" ] || fail 'an MQTT password is required'
# The `ha` CLI has no options subcommand on current releases, so go through the
# Supervisor API. It replaces the option set wholesale and rejects a body that
# omits a required key, so merge into whatever the add-on already has rather
# than sending only the keys we care about. The body travels over stdin rather
# than argv, which keeps the password out of the process list on the box.
current_options="$(
	ssh -o BatchMode=yes "$HA_SSH" 'curl -sS -H "Authorization: Bearer $SUPERVISOR_TOKEN" \
		http://supervisor/addons/core_mosquitto/info' |
		python3 -c 'import json, sys; print(json.dumps(json.load(sys.stdin)["data"]["options"]))'
)" || fail 'cannot read the current Mosquitto add-on options'
options_result="$(
	MQTT_USER="$MQTT_USER" MQTT_PASSWORD="$MQTT_PASSWORD" \
		CURRENT_OPTIONS="$current_options" python3 -c '
import json, os
options = json.loads(os.environ["CURRENT_OPTIONS"])
options.update({
    "certfile": "openaliro-mqtt.crt",
    "keyfile": "openaliro-mqtt.key",
    "require_certificate": False,
    "logins": [{"username": os.environ["MQTT_USER"], "password": os.environ["MQTT_PASSWORD"]}],
})
print(json.dumps({"options": options}))' |
		ssh -o BatchMode=yes "$HA_SSH" 'curl -sS -X POST \
			-H "Authorization: Bearer $SUPERVISOR_TOKEN" \
			-H "Content-Type: application/json" \
			-d @- http://supervisor/addons/core_mosquitto/options'
)"
case "$options_result" in
*'"result": "ok"'* | *'"result":"ok"'*) ;;
*) fail "the Mosquitto add-on rejected the options: $options_result" ;;
esac
ssh -o BatchMode=yes "$HA_SSH" 'ha apps restart core_mosquitto' >/dev/null
printf '      waiting for the broker'
for _ in $(seq 1 30); do
	if openssl s_client -connect "$BROKER_HOST:$BROKER_PORT" -CAfile "$CERT" \
		-brief </dev/null 2>&1 | grep -q 'Verification: OK'; then
		printf ' ok\n'
		break
	fi
	printf '.'
	sleep 2
done

# 5. Agent configuration -----------------------------------------------------
# The password lives in a 0600 file rather than an environment variable, so no
# shell export is needed and it never reaches a process listing.
step 5 "writing $CONFIG"
umask 077
printf '%s' "$MQTT_PASSWORD" >"$PASSWORD_FILE"
rm -f "$CONFIG"
openaliro-ha --config "$CONFIG" configure \
	--device-id "$DEVICE_ID" \
	--mqtt-host "$BROKER_HOST" \
	--mqtt-port "$BROKER_PORT" \
	--mqtt-username "$MQTT_USER" \
	--mqtt-password-file "$PASSWORD_FILE" \
	--mqtt-ca "$CERT"

# 6. Verify ------------------------------------------------------------------
step 6 'running doctor'
openaliro-ha --config "$CONFIG" doctor

printf '\nReady. Start the agent with:\n  openaliro-ha --config %s run\n' "$CONFIG"
