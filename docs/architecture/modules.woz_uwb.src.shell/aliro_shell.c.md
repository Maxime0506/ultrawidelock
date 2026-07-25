<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/shell/aliro_shell.c`

@file aliro_shell.c — `aliro` UART shell command: colored console over the UWB engine.

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](../modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_min.h`](../modules.woz_uwb.src.driver/uwb_min.h.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](../modules.woz_uwb.src.driver/uwb_rxdiag.h.md), [`modules/woz_uwb/src/facade/flight_recorder.h`](../modules.woz_uwb.src.facade/flight_recorder.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](../modules.woz_uwb.src.fira/fira_session.h.md)  ·  **discussed in** [`docs/porting.md`](../../porting.md)

## API

### `static void hdr(const struct shell *sh, const char *title)`
`modules/woz_uwb/src/shell/aliro_shell.c:31`

@brief Section header: green "aliro · <title>" over a dim rule.

**called by** `cmd_chip`, `cmd_range`, `cmd_rx`, `cmd_selftest`, `cmd_status`, `cmd_version`

### `static int cmd_chip(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:49`

@brief Read and display the DW3110 DEV_ID register; verify chip identification.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success; nonzero SPI error code on read failure.

**calls** `hdr`

### `static int cmd_rx(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:75`

@brief Display RX/TX frame tally and error counters.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_range(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:99`

@brief Display the last valid DS-TWR distance measurement and metadata.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_selftest(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:134`

@brief Run radio TX/RX self-test and display results.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success; nonzero error code if self-test fails.

**calls** `hdr`

### `static int cmd_log(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:163`

@brief Enable, disable, or query the ranging heartbeat log output stream.
@param sh Shell context.
@param argc Argument count; if ≥2, argv[1] must be "on" or "off".
@param argv Command arguments; argv[1] optionally specifies "on" or "off".
@return 0 on success; -EINVAL if argv[1] is neither "on" nor "off".

### `static int cmd_frames(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:187`

@brief Enable, disable, or query the per-block distance stream output.
@param sh Shell context.
@param argc Argument count; if ≥2, argv[1] must be "on" or "off".
@param argv Command arguments; argv[1] optionally specifies "on" or "off".
@return 0 on success; -EINVAL if argv[1] is neither "on" nor "off".

### `static int cmd_frec(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:213`

@brief Arm/disarm the flight recorder, or dump/clear its RAM ring.
@param sh Shell context.
@param argc Argument count; if ≥2, argv[1] is on|off|dump|clear.
@param argv Command arguments.
@return 0 on success; -EINVAL on an unrecognised sub-argument.

### `static int cmd_version(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:243`

@brief Display the build commit SHA.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_status(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:261`

@brief Display all system status: chip ID, CCC bind state, URSK provisioning, last range, RX
tally, and stream state.
@param sh Shell context.
@param argc Argument count (unused).
@param argv Argument vector (unused).
@return 0 on success.

**calls** `hdr`

### `static int cmd_aliro(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:323`

@brief Print aliro shell command help and list all available subcommands.
@param sh Shell context.
@param argc Argument count; if >1, returns error for unknown subcommand.
@param argv Argument vector; argv[1] if present must be empty or help request.
@return 0 on success; -EINVAL if argc >1.
