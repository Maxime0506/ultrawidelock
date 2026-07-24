#!/usr/bin/env node
/* Self-test for the WASM twin firmware (web-twin/twin.js): replays the
 * scenario tests/host/test_twin.c asserts, through the same glue entry points
 * the page uses, plus the per-leg stepper. Run by `make test-twin` and CI;
 * the page's footer self-test runs the same sequence in the browser. */
"use strict";
const createTwin = require("./twin.js");

/* Quiet the firmware's DIAG stream here (the page surfaces it as its live
 * firmware console); errors stay visible. */
createTwin({ print: () => {} }).then((m) => {
  const NO_RANGE = -100000; /* twin_glue.c TWIN_NO_RANGE */
  const r = [];
  const ok = (name, cond) => r.push([name, !!cond]);

  ok("boot", m._twin_boot() === 0);

  /* legit approach: one full encrypted DS-TWR block latches 234 cm
   * (test_twin.c "legit approach ranges through the full DS-TWR pipeline") */
  m._twin_block(234);
  ok("range.cm", m._twin_last_cm() === 234);
  ok("range.plausible", m._twin_plausible(234) === 1);
  ok("one_block.not_trusted", m._twin_trusted_cm() === NO_RANGE);
  ok("latch.notified", m._twin_take_latches() === 1);

  /* Ghost-Peak spoof: a negative-ToF block through the same full-frame path
   * must not reduce the range, must not open, must not wake the unlock seam */
  m._twin_block(-400);
  ok("spoof.range_not_reduced", m._twin_last_cm() === 234);
  ok("spoof.not_trusted", m._twin_trusted_cm() === NO_RANGE);
  ok("spoof.trust_reset", m._twin_trust_level() === 0);
  ok("spoof.no_wake", m._twin_take_latches() === 0);

  /* only a sustained honest approach earns the unlock bit (K blocks) */
  m._twin_block(120);
  ok("earn.one", m._twin_trusted_cm() === NO_RANGE);
  m._twin_block(122);
  ok("earn.two", m._twin_trusted_cm() === NO_RANGE);
  m._twin_block(121);
  ok("earn.k_reached", m._twin_trusted_cm() !== NO_RANGE);
  ok("earn.distance", m._twin_trusted_cm() === 121);
  ok("earn.level_k", m._twin_trust_level() === m._twin_trust_k());

  /* the per-leg stepper drives the real RX state machine */
  ok("leg.start", m._twin_leg() === 0);
  m._twin_step(150);
  ok("leg.await_poll", m._twin_awaiting_poll() === 1);
  for (let i = 0; i < 4; i++) m._twin_step(150);
  ok("leg.wrapped", m._twin_leg() === 0);
  ok("step.latched", m._twin_last_cm() === 150);

  const fails = r.filter((x) => !x[1]);
  for (const [name] of fails) console.error("twin selftest FAIL:", name);
  if (fails.length) {
    console.error(`twin selftest: ${fails.length}/${r.length} FAILED`);
    process.exit(1);
  }
  console.log(`twin selftest: ${r.length}/${r.length} PASS (wasm firmware, test_twin.c scenario)`);
});
