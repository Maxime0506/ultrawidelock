<!-- generated documentation — edit the source, not this file -->
# `scripts/deadcode-graph.sh`

deadcode-graph.sh — find functions nothing calls, using the documate code graph.
Why this exists rather than -Wl,--print-gc-sections: that flag lists what the
linker THREW AWAY, which by definition is not in flash. The dead code worth
finding is what survives gc-sections because something references it without
ever calling it -- a function in an ops table, a callback registered into a
struct nobody dispatches. deps/dw3000's interface_rx_enable was exactly that:
present in the shipped ELF, zero callers, kept because a dwt_mcps_ops table
names it. No linker flag can see that; a call graph can.
Three tiers, because "the graph shows no callers" is not evidence of death:
A  zero inbound CALLS, referenced nowhere else in the tree, AND absent from
the unindexed upstream. The only tier worth calling a candidate.
B  zero inbound CALLS, but referenced somewhere in-tree -- an ops table, a
SYS_INIT/SHELL_CMD registration, a header declaration. Zephyr registers
through linker arrays constantly, so most of tier B is alive. This is NOT
a delete list; it is where table-registered dead code hides, and reading
the reference is the only way to tell which.
U  zero inbound CALLS in-tree, but the fetched upstream calls it. Live API.
Tier U exists because the first version of this script did not have it and
proposed deleting nine woz_aliro_stack methods -- the module reimplements the
Nordic Aliro API, and every one of them is called from
workspace/ncs-door-lock-and-access-control, which documate does not index.
CLAUDE.md warns about exactly this: fetched upstream is not in the graph.
Without a workspace to check, tier A is unverifiable and says so.
Needs .documate/graph.db, which `make docs` builds and .gitignore excludes.

## API

### `is_entry_point()`
`scripts/deadcode-graph.sh:58`

Entry points, and shapes invoked by generated or vendor code rather than by
any C call site we could see.
