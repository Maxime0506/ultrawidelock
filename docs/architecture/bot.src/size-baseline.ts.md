<!-- generated documentation — edit the source, not this file -->
# `bot/src/size-baseline.ts`

@file The recorded CDK size baseline, as six numbers.
Not a live import of `firmware/size-baseline.json`: that file carries a
per-symbol breakdown for every recorded config (3,200+ lines) to answer a
question `/size` needs six numbers for, and bundling it whole nearly
tripled the Worker (see `scripts/build-size-baseline.ts`). This reads the
generated extract instead, checked against a fresh read of the real file by
`test/size-baseline.test.ts`.
`primary` is the config `cdk-size.yml`'s own header identifies as the
shipping image: SMP=1 RELEASE=1 with LTO, the one `make release` builds and
`make fota` pushes.

**depends on** [`bot/src/size-baseline.generated.ts`](size-baseline.generated.ts.md)  ·  **used by** [`bot/scripts/size-baseline-extract.ts`](../bot.scripts/size-baseline-extract.ts.md), [`bot/src/commands/size.ts`](../bot.src.commands/size.ts.md), [`bot/src/size-baseline.generated.ts`](size-baseline.generated.ts.md)

## API

### `export function primaryBaseline(): SizeBaseline | null`
`bot/src/size-baseline.ts:34`

The shipping-image baseline. `null` only if the generated file were
hand-corrupted; `npm run size-baseline` always produces a valid one.

**called by** `handler`
