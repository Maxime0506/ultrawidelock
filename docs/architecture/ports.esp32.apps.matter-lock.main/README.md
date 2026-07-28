<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/apps/matter-lock/main/`

| subsystem | about |
|---|---|
| [`ports/esp32/apps/matter-lock/main/app_driver.cpp`](app_driver.cpp.md) | Board driver glue for the ESP32 Matter port: button input, WS2812 lock-status LED, and the |
| [`ports/esp32/apps/matter-lock/main/app_main.cpp`](app_main.cpp.md) | Matter application main: door lock endpoint setup, Matter lifecycle event handling, and (when |
| [`ports/esp32/apps/matter-lock/main/app_priv.h`](app_priv.h.md) |  |
| [`ports/esp32/apps/matter-lock/main/app_shell.cpp`](app_shell.cpp.md) | ESP32-IDF console shell for the Aliro Matter door lock app: registers status, range, aliro, lock/unlock, codes, factoryr |
| [`ports/esp32/apps/matter-lock/main/app_shell.h`](app_shell.h.md) |  |
| [`ports/esp32/apps/matter-lock/main/ha_mqtt.c`](ha_mqtt.c.md) | Native Home Assistant MQTT publisher for the ESP32 Matter lock — see ha_mqtt.h |
| [`ports/esp32/apps/matter-lock/main/ha_mqtt.h`](ha_mqtt.h.md) | *first commit: "Publish to Home Assistant MQTT natively from the ESP32 lock"* |
| [`ports/esp32/apps/matter-lock/main/lock_led.c`](lock_led.c.md) | Lock-state indicator LED: maps lock state (and Aliro activity) to an RGB colour for the single |
| [`ports/esp32/apps/matter-lock/main/lock_led.h`](lock_led.h.md) | Lock status LED color mapping: derives the RGB color for the lock indicator from the |
