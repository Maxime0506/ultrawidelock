<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/shell/aliro_shell.c`

@file aliro_shell.c — `aliro` UART shell command: colored console over the UWB engine.

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](../modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_min.h`](../modules.woz_uwb.src.driver/uwb_min.h.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](../modules.woz_uwb.src.driver/uwb_rxdiag.h.md), [`modules/woz_uwb/src/facade/flight_recorder.h`](../modules.woz_uwb.src.facade/flight_recorder.h.md), [`modules/woz_uwb/src/facade/uwb_cirdiag.h`](../modules.woz_uwb.src.facade/uwb_cirdiag.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](../modules.woz_uwb.src.fira/fira_session.h.md), [`modules/woz_uwb/src/shell/aliro_shell.h`](aliro_shell.h.md)  ·  **discussed in** [`docs/porting.md`](../../porting.md)

## API

### `static void hdr(const struct shell *sh, const char *title)`
`modules/woz_uwb/src/shell/aliro_shell.c:33`

@brief Section header: green "aliro · <title>" over a dim rule.

**called by** `cmd_chip`, `cmd_factoryreset`, `cmd_range`, `cmd_rx`, `cmd_selftest`, `cmd_status`, `cmd_version`

### `static int cmd_chip(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:51`

@brief Read and display the DW3110 DEV_ID register; verify chip identification.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success; nonzero SPI error code on read failure.

**calls** `hdr`

### `static int cmd_rx(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:77`

@brief Display RX/TX frame tally and error counters.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_range(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:101`

@brief Display the last valid DS-TWR distance measurement and metadata.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_selftest(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:136`

@brief Run radio TX/RX self-test and display results.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success; nonzero error code if self-test fails.

**calls** `hdr`

### `static int cmd_log(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:165`

@brief Enable, disable, or query the ranging heartbeat log output stream.
@param sh Shell context.
@param argc Argument count; if ≥2, argv[1] must be "on" or "off".
@param argv Command arguments; argv[1] optionally specifies "on" or "off".
@return 0 on success; -EINVAL if argv[1] is neither "on" nor "off".

### `static int cmd_frames(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:189`

@brief Enable, disable, or query the per-block distance stream output.
@param sh Shell context.
@param argc Argument count; if ≥2, argv[1] must be "on" or "off".
@param argv Command arguments; argv[1] optionally specifies "on" or "off".
@return 0 on success; -EINVAL if argv[1] is neither "on" nor "off".

### `static int cmd_frec(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:215`

@brief Arm/disarm the flight recorder, or dump/clear its RAM ring.
@param sh Shell context.
@param argc Argument count; if ≥2, argv[1] is on|off|dump|clear.
@param argv Command arguments.
@return 0 on success; -EINVAL on an unrecognised sub-argument.

### `static int cmd_cir(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:248`

@brief Control the per-reception CIA diagnostics stream ([ALAB] uwb.diag) and, via the `dump`
sub-form, the windowed-CIR tap dump ([ALAB] uwb.cir).
@param sh Shell context.
@param argc Argument count. `cir [on|off]` toggles the summary stream; `cir dump [on|off]`
toggles the windowed-CIR dump; `cir probe` runs the one-shot accumulator read diagnostic.
@param argv Command arguments.
@return 0 on success; -EINVAL on a malformed argument.

### `void aliro_shell_set_factory_reset(void (*fn)(void))`
`modules/woz_uwb/src/shell/aliro_shell.c:296`

@brief Supply the handler that `aliro factoryreset yes` invokes.
A factory reset is a Matter/CHIP operation, and CHIP is C++. This file's
module is pure C and has no CHIP include paths, so the application registers
the call instead of this layer reaching up for it. Pass NULL to unregister.
The handler is expected to erase provisioning and reboot; it may not return.
Left unregistered (host tests, a build without the application) the command
refuses with -ENOTSUP rather than pretending to have done anything.
@param fn Reset handler, or NULL.

### `static int cmd_factoryreset(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:315`

@brief Erase every stored credential and reboot, but only on an explicit "yes".
The confirm word is the whole safety mechanism: this is reachable over a bare
UART with no undo, and `aliro fa<TAB>` completes to something that would
otherwise unpair the lock on Enter. Without it the command only explains what
it would destroy.
@param sh Shell context.
@param argc Argument count; argv[1] must be the literal "yes" to proceed.
@param argv Command arguments.
@return 0 once the reset is scheduled; -EINVAL when unconfirmed; -ENOTSUP when
the application never registered a handler.

**calls** `hdr`

### `static int cmd_version(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:342`

@brief Display the build commit SHA.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_status(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:360`

@brief Display all system status: chip ID, CCC bind state, URSK provisioning, last range, RX
tally, and stream state.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_aliro(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:422`

@brief Print aliro shell command help and list all available subcommands.
@param sh Shell context.
@param argc Argument count; if >1, returns error for unknown subcommand.
@param argv Argument vector; argv[1] if present must be empty or help request.
@return 0 on success; -EINVAL if argc >1.
