# BLE RSSI witness for INSIDE / OUTSIDE / THRESHOLD differential measurements.
#
# Hardware: nRF52840 DK or dongle. One board per role. Place:
#   - outside: approach side of the door, ~0.3–1 m from the plane
#   - inside: protected side, mirrored placement
#   - threshold: lintel / frame, in the door plane
#
# Build / flash examples:
#
#   make witness-build WITNESS_ROLE=outside WITNESS_BOARD=nrf52840dk/nrf52840
#   make witness-build WITNESS_ROLE=inside  WITNESS_BOARD=nrf52840dk/nrf52840
#   make witness-build WITNESS_ROLE=threshold WITNESS_BOARD=nrf52840dongle/nrf52840
#
# (WITNESS_ROLE, not ROLE — ROLE is reserved for the UWB anchor pair.)
# UART lines look like:
#   WR1 role=outside obs=3 filt=0000 n=12 mean=-61 min=-70 max=-52 var=18
#
# The witness never commands an unlock. Correlate on obs + packet timing on the
# Raspberry Pi; the DWM3001CDK remains authoritative.
