<!-- generated documentation — edit the source, not this file -->
# `ports/nrf5340dk/initiator/src/prepoll_tx.h`

Device-side CCC Pre-POLL transmitter: turns the URSK this board just agreed
over BLE into the SP0 data frame that opens a ranging block, and puts it on
the air on the session's channel.

**used by** [`ports/nrf5340dk/initiator/src/prepoll_tx.c`](prepoll_tx.c.md), [`ports/nrf5340dk/initiator/src/ranging.c`](ranging.c.md)

## API

### `struct prepoll_tx_params`
`ports/nrf5340dk/initiator/src/prepoll_tx.h:35`

The ranging-setup values a Pre-POLL needs, gathered across M1, M3 and M4.
