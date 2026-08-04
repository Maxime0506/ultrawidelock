<!-- generated documentation — edit the source, not this file -->
# `bot/eval/headers.ts`

@file §7.2 tier 1: a deterministic keyword header on every chunk.
This is query expansion pointed at the index instead of the question. A chunk
holding `CONFIG_UART_CONSOLE=n` gains the words `uart` and `console`, and
through the alias table `serial`, `port`, `tty`, `vcom` — so a question that
says "serial port" can reach a line that never uses either word.
Symbol names and the file path are the only sources: no prose is invented,
nothing is written by hand per symbol, and there is nothing here that could
have been tuned against a particular golden question.
Measured as dominated on the self-written golden set and kept anyway, because
the independent set later showed why that verdict was premature. Questions
like "how much stack does the main thread get" fail there even though `main`,
`stack` and `size` are all inside `CONFIG_MAIN_STACK_SIZE`: those words are so
common in this tree that idf buries the one chunk that matters. Repeating a
symbol's own words in a header does not add vocabulary, but it does add term
frequency exactly where the answer is, which is a different lever from the one
the self-written set was able to test.

**depends on** [`bot/eval/corpus.ts`](corpus.ts.md), [`bot/eval/expand.ts`](expand.ts.md)  ·  **used by** [`bot/eval/independent.ts`](independent.ts.md), [`bot/eval/stage1-probe.ts`](stage1-probe.ts.md)

<details><summary>Undocumented (1)</summary>

- `withHeaders`

</details>
