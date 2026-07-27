# Privacy

Last updated: 2026-07-27

openaliro does not operate a telemetry service, user-data backend, account system, or advertising service. Project maintainers do not receive, store, sell, or access lock credentials, pairing data, ranging measurements, access events, or capture files.

## Local operation

- Firmware processes credentials, authentication, and ranging on the lock and paired device.
- The browser flasher accesses only the serial device selected by the operator; openaliro site code does not upload serial data.
- Flight logs and `.frc` captures can contain ephemeral ranging material; keep them private.
- Experimental passive-carry work processes data locally and is opt-in per lock.

## Optional integrations

The Home Assistant bridge runs only when the operator starts it. It reads the operator's local serial log and publishes distance and access-granted/denied events only to the MQTT broker configured by that operator. openaliro does not receive, host, or control that broker or its data.

## Website services

The documentation site is hosted by GitHub Pages. It loads Google Fonts, the browser flasher loads ESP Web Tools from unpkg, and the documentation site may request public repository statistics from GitHub. Those providers receive ordinary web-request data under their own terms. The project does not send lock or serial data to them.

## Questions

Contact the maintainer through the project repository. Do not include credentials, keys, or captures in public issues.
