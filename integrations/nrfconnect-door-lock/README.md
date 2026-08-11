# Nordic door-lock integration

This integration adapts Nordic's door-lock and access-control application for
the nRF5340 DK UltraWideLock lock. The upstream repository is pinned and fetched by
`make bootstrap`; it is not copied into this repository.

`patches/` contains the tracked changes applied to the upstream application,
NCS, and Matter trees. The bootstrap script checks those fetched trees, resets
them to their pinned revisions, and then applies the patch set. It records a
fingerprint of the patch contents and HA mode; the product build rejects a
workspace fingerprint from an older checkout.

The product-owned launcher and overlays live in
[`apps/nrf5340dk-lock/`](../../apps/nrf5340dk-lock/). Build the integrated
product with:

```sh
make bootstrap
make nrf-build
```
