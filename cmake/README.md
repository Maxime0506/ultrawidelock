# Shared CMake helpers

`cmake/` contains repository-owned CMake functions shared by more than one
build definition. It is not an application and does not contain generated
build output.

`woz_roles.cmake` reads declarative source-role manifests and returns resolved
source lists to Zephyr and ESP-IDF builds. This keeps source membership in one
place while allowing each framework to create its own targets.

Application entry points remain in their application directories. User-facing
build commands remain in the top-level `Makefile`.
