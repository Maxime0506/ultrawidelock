<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/shell/aliro_shell.c`

@file aliro_shell.c — `aliro` UART shell command: colored console over the UWB engine.

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](../modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_min.h`](../modules.woz_uwb.src.driver/uwb_min.h.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](../modules.woz_uwb.src.driver/uwb_rxdiag.h.md), [`modules/woz_uwb/src/facade/flight_recorder.h`](../modules.woz_uwb.src.facade/flight_recorder.h.md), [`modules/woz_uwb/src/facade/uwb_cirdiag.h`](../modules.woz_uwb.src.facade/uwb_cirdiag.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](../modules.woz_uwb.src.fira/fira_session.h.md)  ·  **discussed in** [`docs/porting.md`](../../porting.md)

## API

### `static void hdr(const struct shell *sh, const char *title)`
`modules/woz_uwb/src/shell/aliro_shell.c:32`

@brief Section header: green "aliro · <title>" over a dim rule.

**called by** `cmd_chip`, `cmd_range`, `cmd_rx`, `cmd_selftest`, `cmd_status`, `cmd_version`

### `static int cmd_chip(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:50`

@brief Read and display the DW3110 DEV_ID register; verify chip identification.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success; nonzero SPI error code on read failure.

**calls** `hdr`

### `static int cmd_rx(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:76`

@brief Display RX/TX frame tally and error counters.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_range(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:100`

@brief Display the last valid DS-TWR distance measurement and metadata.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_selftest(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:135`

@brief Run radio TX/RX self-test and display results.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success; nonzero error code if self-test fails.

**calls** `hdr`

### `static int cmd_log(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:164`

@brief Enable, disable, or query the ranging heartbeat log output stream.
@param sh Shell context.
@param argc Argument count; if ≥2, argv[1] must be "on" or "off".
@param argv Command arguments; argv[1] optionally specifies "on" or "off".
@return 0 on success; -EINVAL if argv[1] is neither "on" nor "off".

### `static int cmd_frames(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:188`

@brief Enable, disable, or query the per-block distance stream output.
@param sh Shell context.
@param argc Argument count; if ≥2, argv[1] must be "on" or "off".
@param argv Command arguments; argv[1] optionally specifies "on" or "off".
@return 0 on success; -EINVAL if argv[1] is neither "on" nor "off".

### `static int cmd_frec(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:214`

@brief Arm/disarm the flight recorder, or dump/clear its RAM ring.
@param sh Shell context.
@param argc Argument count; if ≥2, argv[1] is on|off|dump|clear.
@param argv Command arguments.
@return 0 on success; -EINVAL on an unrecognised sub-argument.

### `static int cmd_cir(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:247`

@brief Control the per-reception CIA diagnostics stream ([ALAB] uwb.diag) and, via the `dump`
sub-form, the windowed-CIR tap dump ([ALAB] uwb.cir).
@param sh Shell context.
@param argc Argument count. `cir [on|off]` toggles the summary stream; `cir dump [on|off]`
toggles the windowed-CIR dump; `cir probe` runs the one-shot accumulator read diagnostic.
@param argv Command arguments.
@return 0 on success; -EINVAL on a malformed argument.

### `static int cmd_version(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:297`

@brief Display the build commit SHA.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_status(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:315`

@brief Display all system status: chip ID, CCC bind state, URSK provisioning, last range, RX
tally, and stream state.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_aliro(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:377`

@brief Print aliro shell command help and list all available subcommands.
@param sh Shell context.
@param argc Argument count; if >1, returns error for unknown subcommand.
@param argv Argument vector; argv[1] if present must be empty or help request.
@return 0 on success; -EINVAL if argc >1.
