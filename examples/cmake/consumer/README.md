# Plain C consumer

This example verifies the installed OpenAliro CMake package and public include
layout. It includes `<openaliro/tlv.h>`, links the portable TLV codec, and
compiles `<openaliro/woz_hal.h>` with its installed support headers. It never
reaches into a module's private `src/` directory.

Install OpenAliro, then build the example against that prefix. This derives the
compatible SDK series from the repository's one version source:

```sh
cmake -S . -B build/sdk -DCMAKE_INSTALL_PREFIX="$PWD/build/sdk-install"
cmake --build build/sdk
cmake --install build/sdk
SDK_VERSION="$(sed -n '1p' VERSION)"
cmake -S examples/cmake/consumer -B build/sdk-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build/sdk-install" \
  -DOPENALIRO_REQUIRED_VERSION="${SDK_VERSION%.*}"
cmake --build build/sdk-consumer
```

An application that vendors the source tree can consume the same targets
without installing them first:

```sh
cmake -S examples/cmake/consumer -B build/sdk-consumer-source \
  -DOPENALIRO_SOURCE_DIR="$PWD"
cmake --build build/sdk-consumer-source
```

Use the Zephyr module or ESP-IDF components for complete firmware. The plain
CMake package supplies the public C contracts and the portable TLV target.
