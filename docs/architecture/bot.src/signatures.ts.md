<!-- generated documentation — edit the source, not this file -->
# `bot/src/signatures.ts`

@file The console-output matcher.
A lookup table, not a model. Every entry below is a literal string that this
repository already documents, with the line that documents it. Nothing here
was written from what a log "probably" means: if the tree does not say it,
there is no entry, and the bot escalates instead.
Patterns are deliberately literal and distinctive. A loose pattern that
matches half the pastes in a channel is worse than no pattern at all,
because it produces a confident answer that happens to be wrong, and the
person believes it for an evening before going back to the start.
Ranking is by the length of the text that matched, longest first, so a
specific error string outranks a short token that happened to appear. Ties
keep declaration order. All matches are shown, never just the best one:
`URSK_Unavailable` legitimately has two documented causes and choosing
between them is the reader's job, not this table's.

**depends on** [`bot/src/citations.ts`](citations.ts.md)  ·  **used by** [`bot/src/citations.ts`](citations.ts.md), [`bot/src/commands/help-me.ts`](../bot.src.commands/help-me.ts.md)

## API

### `export function matchSignatures(text: string): Match[]`
`bot/src/signatures.ts:236`

Every signature that matches, most specific first.
"Most specific" is the length of the matched text. It is a crude measure and
deliberately so: anything cleverer would be a scoring model, and a scoring
model is the thing this file exists not to be.

**called by** `onModalSubmit`

### `export function signatureCitations(): Citation[]`
`bot/src/signatures.ts:249`

Citations from every signature, for the drift gate.

**called by** `allCitations`
