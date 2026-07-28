<!-- generated documentation — edit the source, not this file -->
# `tools/tui/src/jobs.ts`

**depends on** [`tools/tui/src/types.ts`](types.ts.md)  ·  **used by** [`tools/tui/src/app.tsx`](app.tsx.md)

<details><summary>Undocumented (10)</summary>

- `JobRunner`
- `JobRunner.onChange` — tested: :cancels queued work during tui shutdown@l43; :serializes bench jobs and captures their output@l4
- `JobRunner.publish`
- `JobRunner.isCancelled`
- `JobRunner.run` — tested: :a failed job does not stop the next queued job from running@l33; :cancels queued work during tui shutdown@l43; :reports a nonzero exit as failed and keeps the output that explains it@l16; :reports an unstartable command as failed instead of throwing@l26; :serializes bench jobs and captures their output@l4
- `JobRunner.execute`
- `JobRunner.appendOutput`
- `JobRunner.collect`
- `JobRunner.cancel`
- `JobRunner.cancelAll` — tested: :cancels queued work during tui shutdown@l43

</details>
