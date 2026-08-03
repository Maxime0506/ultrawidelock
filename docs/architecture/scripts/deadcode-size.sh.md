<!-- generated documentation — edit the source, not this file -->
# `scripts/deadcode-size.sh`

deadcode-size.sh — flash cost of the functions nothing calls.
deadcode-graph.sh answers "what has no callers". This answers "and what does
that cost", by joining that list against the symbol sizes in the linked image.
A zero-caller function that the linker already discarded costs nothing and is
not worth an argument; one that survived into .text is real flash.
That distinction is the whole reason -Wl,--print-gc-sections is the wrong tool
for this: it lists what was REMOVED. What is in the image and unreachable never
appears in its output.
./scripts/deadcode-size.sh          rank uncalled symbols by flash bytes
./scripts/deadcode-size.sh --serve  puncover's interactive view instead
puncover renders callers/callees and stack depth per symbol from the DWARF,
which is worth more than any text report once you are chasing a specific
function. It is a server: it does not exit, so it is not scriptable. Its
--generate-report writes stack-usage entries only, not symbol sizes.
