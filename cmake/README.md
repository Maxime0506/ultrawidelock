# Shared CMake helpers

`cmake/` contains repository-owned CMake functions and package configuration.
It is not an application and does not contain generated build output.

`ultrawidelock_roles.cmake` reads declarative source-role manifests and returns resolved
source lists to Zephyr and ESP-IDF builds. This keeps source membership in one
place while allowing each framework to create its own targets.

`UltraWideLockConfig.cmake.in` defines the installed plain-CMake package consumed
through `find_package(UltraWideLock CONFIG REQUIRED)`. Its exported targets are
defined by the top-level `CMakeLists.txt`.

Application entry points remain in their application directories. User-facing
build commands remain in the top-level `Makefile`.
