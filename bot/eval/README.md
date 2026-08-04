# Stage 0: should this project build its own retrieval stack?

The `/ask` design called for a custom RAG pipeline: AST-aware chunkers, D1 FTS5, Vectorize,
a reranker and a bounded agentic hop. Before writing any of it, Stage 0 measures whether a
managed service already clears the bar, and records the numbers so the decision can be
audited later — including by whoever asks why this project did or did not build its own.

**Outcome: no managed option clears the bar. Build the chunkers and the router, and
keep the managed bot out of the request path.** The reasoning is at the bottom; the
numbers it rests on are above it.

```sh
cd bot
node eval/golden.ts        # gate: every gold anchor still resolves at HEAD
node eval/stage0.ts        # baseline D: the lexical floor, $0, no API key, no network
node eval/deepwiki.ts      # baseline A: queries DeepWiki, caches every answer
node eval/score-deepwiki.ts # re-score the cache without re-querying
```

## The golden set

183 questions, 189 anchors, stratified. Built by reading the tree, not from memory.

| stratum | n | what it tests |
|---|---:|---|
| `config` | 55 | a single-line `CONFIG_*` fact, asked in prose (`"why is there no serial port"`) |
| `config-exact` | 30 | the **same anchors**, asked with the identifier (`"what is CONFIG_UART_CONSOLE set to"`) |
| `identifier` | 49 | make targets, `#define` values, function definitions |
| `error-string` | 16 | a pasted console line |
| `cross-file` | 15 | needs two or more files to answer correctly |
| `conceptual` | 10 | a "why is it built this way" question |
| `negative` | 8 | the repo genuinely cannot answer; `"I don't know"` is correct |

`config` and `config-exact` share gold anchors and differ only in phrasing. That pairing is
the experiment: it isolates *how a question is worded* from *what it is about*.

### Gold labels key on `expect` substrings, never line numbers

This is not a style preference, it is a measured requirement. The design brief this eval was
written against cited `mk/cdk.mk:261-262`. Commit `3737673` added 71 lines to that file and
moved every anchor in its back half by a uniform **+20**: `261-262 -> 281-282`,
`263-264 -> 283-284`, `293-294 -> 313-314`, `305 -> 325`, `319-328 -> 339-348`. A golden set
keyed on line numbers would have silently scored every baseline against the wrong lines,
one day after being written.

`bot/src/citations.ts` was not affected, because `scripts/check-citations.ts` re-reads every
cited line and fails when the `expect` substring is no longer on it. `eval/golden.ts` is the
same mechanism, one level stricter: it also reports an `expect` that matches more than one
line, since an ambiguous anchor inflates recall.

## Baselines

| | baseline | status | overall | weighted | citations | latency |
|---|---|---|---:|---:|---:|---:|
| A | DeepWiki `ask_question` | done | 0.709 | 0.557 | **0.000** | 37.7 s |
| B | kapa.ai | **skipped**, maintainer decision | — | — | — | — |
| C | Cloudflare AI Search | **skipped**, architectural + account | — | — | — | — |
| D | Lexical (ripgrep + idf) | done | **0.689** | **0.507** | 1.000 by construction | 48 ms |
| D | Lexical (D1 FTS5, default tokenizer) | done | 0.660 | 0.393 | 1.000 by construction | 2 ms |
| D | Lexical (D1 FTS5, `tokenchars '_'`) | done | 0.551 | 0.264 | 1.000 by construction | 1 ms |

"Weighted" is `config` + `cross-file`, the strata Amendment 2 weights heaviest.

**A and D do not measure the same thing, and the table cannot make them.** D is *retrieval*:
did a returned chunk cover the gold line. That is an upper bound on what a generator fed
those chunks could then say. A is *end to end*: did the finished answer carry the fact. So A
is doing strictly more work for a comparable number, and D's figure would fall once a
generator was placed behind it. Both are compared against the 0.85 gate rather than against
each other.

For baseline D, citation correctness needs no separate column: a hit *is* a chunk covering
the anchor line at the pinned SHA. For baseline A it is measured directly, and it is zero.

Measured at `7cfa6d7`, over 1,397 indexable files and 8,068 chunks (9.11 MB; generated
output, `assets.generated.ts`, `twin.js` and binaries excluded).

### Baseline D, per stratum

| stratum | n | rg + idf | FTS5 default | FTS5 `tokenchars '_'` |
|---|---:|---:|---:|---:|
| `config-exact` | 30 | **0.900** | **0.933** | 0.633 |
| `conceptual` | 10 | 0.800 | 0.900 | 0.900 |
| `error-string` | 16 | 0.875 | 0.813 | 0.813 |
| `identifier` | 49 | 0.735 | 0.776 | 0.755 |
| `config` | 55 | 0.545 | 0.382 | 0.218 |
| `cross-file` | 15 | 0.367 | 0.433 | 0.433 |

### Baseline A, per stratum

Two metrics, because one of them is unfair on its own. `strict` asks whether the gold text
appears verbatim; `fair` accepts a correct paraphrase (for a `SYMBOL=VALUE` anchor: the
symbol named and the value asserted within 120 characters; for a prose anchor: 80% of its
distinctive tokens). `fair` is the honest read. `strict` is kept because it needs no
judgement, and the gap between the two is itself the finding.

| stratum | n | strict | fair | cite | mean s |
|---|---:|---:|---:|---:|---:|
| `identifier` | 49 | 0.020 | **0.898** | 0.000 | 41.9 |
| `config-exact` | 30 | 0.400 | **0.833** | 0.000 | 31.7 |
| `error-string` | 16 | 0.625 | **0.750** | 0.000 | 44.3 |
| `config` | 55 | 0.418 | **0.564** | 0.000 | 33.4 |
| `cross-file` | 15 | 0.200 | **0.533** | 0.000 | 42.1 |
| `conceptual` | 10 | 0.100 | **0.400** | 0.000 | 40.9 |
| **all answerable** | 175 | 0.286 | **0.709** | **0.000** | 37.7 |

`negative`: **8 of 8 refused cleanly.** No fabricated price, no invented Android support, no
Aliro specification prose when invited to quote section 14. This is a real strength and the
one criterion where a managed service beat everything else measured here.

Latency is measured under four-way concurrency against a free tier that throttles, so it is
not DeepWiki's best case. It is, however, 700× the lexical index and well past the 10 s
first-answer target on its own.

#### Two bugs in these metrics, both caught before they were reported

Recording them because both would have produced a confident wrong number, which is the
failure this whole exercise exists to avoid.

1. `strict` scored the `identifier` stratum at **0.020**, which reads as total failure. It is
   an artifact: those anchors are make doc-comment lines (`selftest: one-shot UWB init
   self-test at boot`) that no prose answer reproduces verbatim. The answers were largely
   correct. `fair` puts the same stratum at 0.898.
2. The first version of `fair` matched a Kconfig value by substring. `y` and `n` are one
   character long, so "is **en**abled" satisfied a test for `=n`, inflating most of the config
   stratum. Booleans now match as whole words or by prose equivalent, and `=1` is no longer
   satisfied by the `1` in `1024`.

### Why B and C were skipped

**kapa.ai** needs an Open Source Program application by email and an ongoing account
relationship with a company. The eligibility question is real — project code is ISC but the
tree vendors Qorvo `LicenseRef-QORVO-2` material — but it was not the deciding factor. An
ongoing account relationship is a decision Amendment 4 itself places with the maintainer
rather than treating as a technical gate.

**Cloudflare AI Search** was skipped on architecture first and account second. It is
prose-oriented, with no AST chunking and no `file:line`-at-a-SHA anchors, so it cannot
satisfy the third weighted criterion by construction — an afternoon of setup would confirm a
property already known from its design. The Cloudflare account is also identity-bearing.

Neither is ruled out permanently. If baseline A misses and the custom path looks expensive,
B is the cheapest remaining managed option to revisit.

## What baseline D established

**1. Phrasing decides lexical recall, not topic.** Across the same 30 config anchors,
identifier phrasing scores **0.933** and prose phrasing **0.382** on the identical index — a
0.55 gap from wording alone. `firmware/prj.conf:226` is `CONFIG_UART_CONSOLE=n`, its comment
says "Console stays on, but over RTT rather than UART", and the question a contributor
actually asks is "why is there no serial port when I plug it in". The words *serial* and
*plug* appear nowhere near the fact. This is a vocabulary gap, not a chunking gap, and it is
the one failure mode lexical retrieval cannot fix by tuning.

So §2's adversarial premise is **half right, and the half matters**: agentic grep is
excellent when the user types the token (0.90–0.93, no embeddings needed) and weak when they
describe a symptom (0.38–0.55). The router is the highest-ROI component as the brief claims,
but its job is larger than the brief states — it must branch on *how the question is
phrased*, not only on what entity it names.

**2. `tokenchars '_'` is a trap, and this contradicts the hypothesis it was added to test.**
Making `_` a token character was expected to help, since it keeps `CONFIG_UART_CONSOLE`
whole. It made every stratum worse or equal, and overall recall fell 0.660 to 0.551. The
reason: SQLite's default `unicode61` splits `CONFIG_UART_CONSOLE` into `config`, `uart` and
`console`, and that accidental split is the *only* bridge a prose question has to a bare
config line. Keeping the identifier whole removes it. Do not set `tokenchars '_'` on a
single index; if exact-identifier precision is wanted later, carry both tokenizations and
let the router choose.

**3. Cross-file is the floor at 0.367–0.433**, worst in every configuration. Top-10 fills
with chunks from whichever file matches more query terms, so the second file never surfaces.
Single-shot top-k cannot fix this; it needs either the bounded agentic hop or per-file result
capping.

**4. Negatives are not separable by retrieval score.** Mean top-1 for negatives is 7.82
against 10.40 for answerable questions — overlapping distributions. Abstention has to come
from the generation step refusing on weak evidence, not from a score threshold.

**5. The harness had to be made deterministic before it could gate anything.** ripgrep walks
files on multiple threads, so its output order is not stable, and ties broken on score alone
moved recall@5 by 0.07 between identical runs. Ranking now breaks ties on chunk identity.

## What baseline A established

**6. DeepWiki never emits a line number. Not once in 183 answers.** It names files well —
0.833 file-identification on `config-exact` — and links wiki anchors (`/wiki/openaliro/
openaliro#1.3`), but `path:line` appears nowhere. The detector was unit-tested against
`firmware/prj.conf:226`, bare `prj.conf:226` and GitHub `#L226` blob URLs before the zero was
believed, so it is a real zero rather than a broken metric.

This alone is disqualifying for adoption. Hard constraint 8 requires every answer to state
the SHA it is grounded in, and this repository's own rule is that a diagnostic answer carries
a `file:line` into the tree. A component that cannot cite a line cannot satisfy either, at
any recall.

**7. It produced a confident wrong diagnosis on a real triage question.** Asked why a
`RELEASE=1` board stops with nothing in the log, it answered that `RELEASE=1` "disables RTT
logging to save RAM". `firmware/overlay-release.conf` sets exactly two symbols —
`CONFIG_SEGGER_RTT_BUFFER_SIZE_UP=1024` and `CONFIG_INIT_STACKS=n`. Logging is not disabled;
the ring shrinks from 8 KB to 1 KB and `NO_BLOCK_SKIP` drops the newest lines, which is why
the fault message is the part that goes missing (`mk/cdk.mk:281-282`). The answer is adjacent
to the truth, fluent, and wrong in the direction that sends someone looking for a logging
switch that does not exist. `bot/README.md` names this exact failure as the thing the triage
table exists not to do.

**8. It is strong where lexical is strong, and weak where lexical is weak.** `identifier`
0.898, `config-exact` 0.833; `config` 0.564, `cross-file` 0.533. That is the same shape
baseline D has. The managed option does not complement the lexical one — the two fail on the
same questions, which are the ones this repository is actually hard about.

**9. Its weakest stratum is the one it was supposed to win.** `conceptual` scores 0.400, the
lowest of any answerable stratum. That is the "prose docs" case a hybrid would delegate to it.
Low confidence — n=10, and prose anchors are the hardest case for token-coverage scoring — but
it points the opposite way from the assumption behind Gate 2's remedy.

**10. Clean refusal, 8 of 8.** Including declining to reproduce Aliro specification prose.
Worth naming plainly: this is better than anything else measured, and abstention is a genuinely
hard property to build.

## Recommendation

**Build the custom chunkers and the router. Do not put a managed bot in the request path.**

Amendment 3's gates, against the numbers:

- **Gate 1, adopt — fails, on two independent grounds.** Weighted `fair` is 0.557 against a
  0.85 bar, and citation correctness is 0.000. Either one is disqualifying; the citation zero
  is structural rather than a tuning problem.
- **Gate 2, hybrid — its diagnosis fits, its remedy does not.** The diagnosis is right: the
  managed bot handles general questions (`identifier` 0.898) and misses config and cross-file.
  But the remedy is "use the managed bot for prose docs", and `conceptual` is its *worst*
  answerable stratum at 0.400, it cannot cite a line anywhere, and it produced a confident
  wrong diagnosis. Delegating prose to it would import a component that cannot meet the
  project's own citation rule.
- **Gate 3, build as specified — this is where the numbers land**, with one amendment of its
  own: build the Stage 1 scope (custom Kconfig/devicetree/Makefile chunkers, deterministic
  headers, regex router, lexical index, Haiku with the Citations API), and do not build
  Stage 2's Vectorize/rerank layer until the config-prose stratum is measured again with real
  chunkers. The vocabulary gap in finding 1 is the argument *for* dense retrieval, and it is
  also the only argument for it, so it should be tested against the improved lexical floor
  rather than assumed.

**This is the outcome the exercise was set up to avoid wanting, and it should be said
plainly: the free managed option did not win.** That would have saved weeks. It lost on a
property that has nothing to do with answer quality — it answers general questions better
than the lexical floor does — and everything to do with this repository's requirement that an
answer point at a line at a commit.

One thing worth keeping from baseline A regardless: **DeepWiki refuses cleanly 8 times out of
8**, which is better than the lexical baseline can do, since finding 4 shows retrieval scores
do not separate answerable from unanswerable. Its refusal behaviour is worth studying when the
`/ask` system prompt is written.

## Stage 1 probe: which candidate fix actually earns its place

`node eval/stage1-probe.ts`. Same golden set, same 175 answerable questions, both retrievers
fused with Reciprocal Rank Fusion (k=60) so `@5` means the top 5 of one ranked list.

| variant | chunks | config@10 | config@5 | config MRR | cross-file@10 | all@10 |
|---|---:|---:|---:|---:|---:|---:|
| naive (baseline) | 8068 | 0.509 | 0.364 | 0.270 | 0.467 | 0.703 |
| kconfig chunker | 8118 | 0.473 | 0.400 | 0.262 | 0.467 | 0.709 |
| **query expansion** | 8068 | **0.618** | **0.491** | **0.351** | 0.433 | **0.734** |
| expansion, suspect aliases held out | 8068 | 0.618 | 0.491 | 0.351 | 0.433 | 0.734 |
| kconfig + expansion | 8118 | 0.455 | 0.364 | 0.247 | 0.433 | 0.700 |
| deterministic headers (tier 1) | 8068 | 0.564 | 0.382 | 0.278 | 0.467 | 0.720 |
| headers + query expansion | 8068 | 0.618 | 0.455 | 0.351 | 0.433 | 0.734 |
| kconfig + headers + expansion | 8118 | 0.473 | 0.327 | 0.245 | 0.433 | 0.700 |

Three results, none of them the one the design brief expected:

- **Do not build the Kconfig chunker.** It was the headline Stage 1 item and it makes config
  recall *worse* at k=10 (0.509 -> 0.473), buying 0.036 at k=5 for it. Combined with anything
  else it is worse still. The prediction written into `chunk-kconfig.ts` before it was run —
  that precise chunking cannot add a word the file never uses — held.
- **Build query expansion.** It is the only variant that moves config recall, by 0.109 at k=10
  and 0.127 at k=5, and it lifts the overall number too. It is a 35-entry table, costs nothing
  at query time, and needs no index rebuild.
- **The overfitting hold-out came back clean.** Removing the six aliases that could have been
  reverse-engineered from the one miss analysed in detail changes the result by exactly zero
  in all four columns. The gain is not the answer key leaking into the table.

Deterministic headers (§7.2 tier 1) are real but dominated: +0.055 config@10 alone, and adding
them on top of expansion changes nothing at k=10 while *costing* 0.036 at k=5. They and query
expansion are the same idea pointed at opposite ends of the pipeline, and the query end is
cheaper, so tier 1 headers are not worth an index-side rebuild here.

**Nothing tested moves `cross-file`**, which stays at 0.433-0.467 and is now the worst stratum.
Every variant that helps config either leaves it alone or nudges it down, because expansion
adds terms that let one strongly-matching file take more of the top 10. That is a per-file cap
or a bounded second hop, not a chunking or vocabulary problem.

## The independent set, and what it overturned

Everything above rests on 183 questions written by the same agent that then graded them.
That is the one weakness a harness cannot measure about itself: a question written just
after reading the line that answers it tends to borrow that line's vocabulary, and lexical
retrieval is precisely the technique that rewards such a bias.

`independent.jsonl` is the control. Three separate agents, each scoped to one slice of the
tree (`firmware/`+`mk/`+`scripts/`, `modules/`+`deps/`+`tests/`, `docs/`+`ports/`+the
integrations), each instructed to draft every question from an imagined situation **before**
opening any file that might answer it, and none of them permitted to read `bot/eval/`.
90 questions, 105 anchors, `node eval/independent.ts`. All 90 survived validation with zero
rejected anchors, which is itself a result: the anchor discipline transfers to authors who
were only told the rules.

The self-written set is restricted to questions whose anchors land in the same top-level
areas, so the comparison is authorship rather than topic mix.

| set | n | recall@10 | recall@5 | MRR |
|---|---:|---:|---:|---:|
| independent | 90 | **0.361** | 0.300 | 0.258 |
| self-written, same areas | 171 | 0.702 | 0.599 | 0.404 |
| self-written, whole repo | 175 | 0.703 | 0.603 | 0.408 |

**The self-written golden set overstates this repository's lexical retrieval by 0.341
recall@10.** Path-matching moves the self-written number by 0.001, so topic mix explains
none of it.

### Both Stage 1 levers were measuring their own author

| variant | independent@10 | self-written@10 |
|---|---:|---:|
| naive chunks | 0.361 | 0.702 |
| query expansion | 0.333 (**-0.028**) | 0.734 (**+0.032**) |
| deterministic headers | 0.361 (**0.000**) | 0.720 (**+0.018**) |
| headers + expansion | 0.328 (**-0.033**) | 0.734 (**+0.032**) |

Query expansion, the one fix the probe above told you to build, **loses** 0.028 on questions
it was not tuned against. The clean overfitting hold-out did not catch this, and could not
have: it tested only the six aliases already under suspicion, not the shape of the table.
Deterministic headers do exactly nothing. Neither lever survives.

### Where the misses actually fail

Every anchor lands in one bucket: retrieved in the top 10 the bot would serve; missed there
but present in the top 200 of a deeper search; or absent even from 200.

| set | anchors | in top 10 | buried 11-200 | not in 200 |
|---|---:|---:|---:|---:|
| independent, naive chunks | 105 | 37 (35%) | **44 (42%)** | 24 (23%) |
| independent, headers | 105 | 37 (35%) | 47 (45%) | 21 (20%) |
| self-written, same areas | 185 | 126 (68%) | 51 (28%) | 8 (4%) |

**65% of the independent set's misses are ranking failures, not retrieval failures.** The
lexical index already holds the right chunk within 200 for 77% of anchors and orders it
below 10. A reranker over a deep lexical candidate list is therefore the single largest
available lever, worth up to 0.35 -> 0.77 recall@10 at its ceiling, and it needs no
embeddings.

The remaining 23% is the real vocabulary gap, and it is nearly six times what the
self-written set reported (4%). Even there the failure is usually discrimination rather
than vocabulary: *"how much stack does the main thread get?"* misses
`CONFIG_MAIN_STACK_SIZE=4096` even though `main`, `stack` and `size` are all inside the
symbol, because those words are so common in this tree that idf buries the one chunk that
matters. That it survives the header treatment (24 -> 21) says term frequency is not the
lever either.

### Consequences for Stage 1 and Stage 2

1. **Do not build query expansion.** The probe's recommendation is withdrawn.
2. **Do not build deterministic headers.** Measured at exactly zero.
3. **Build the phrasing router.** It is the one finding the independent set strengthens
   rather than demolishes: identifier-phrased 0.625 against prose-phrased 0.304, a gap of
   0.321 where the self-written set showed 0.104.
4. **Promote the reranker ahead of Vectorize.** Stage 2 listed them together; the bucket
   split separates them, since reranking addresses 65% of misses and embeddings at most
   the remaining 35%.
5. **Treat 0.361, not 0.703, as the floor every future change is measured against.**

### On the blindness of the independent set

By instruction, not by sandbox. Each agent was told not to read `bot/eval/` or `.claude/`
and reported compliance; that cannot be proved from inside the measurement. Two things
argue it held: one agent independently rediscovered a question already in the self-written
set, which is what non-contamination looks like on an obvious question, and a contaminated
agent would not have produced a set that scores 0.341 *lower*. The risk that remains runs
the other way — agents told to write symptom-phrased questions may overshoot what a real
person asks, so the true figure is likely between 0.361 and 0.702. Real questions from the
Discord channel are the only thing that settles it, and collecting them is now the highest
value item in the docs-gap queue.

## Regardless of outcome

Per Amendment 5, these survive adoption of a managed bot, because none of them come with one:
the golden set and this harness (a managed bot regresses too, and nothing else would notice),
the docs-gap queue from feedback, the signature-table handoff so a known console string
returns its cited triage entry instead of a generated answer, and the message context-menu
entry point.
