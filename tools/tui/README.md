# OpenAliro Bench TUI

The OpenAliro Bench TUI is a quiet command workspace for the nRF5340 DK and ESP32 reader
benches. Release binaries are built from the corresponding repository commit; this
directory contains the complete source for local customization.

## Run from source

```sh
make openaliro
```

`make openaliro` installs the pinned dependencies on its first run, then opens the guided
workspace. The wizard distinguishes the nRF5340 DK Matter lock, ESP32-S3 Matter lock, and
standalone ESP32-S3 reader. It checks prerequisites, existing artifacts, compatible serial
ports, and pairing support before it offers an action. Selecting a target automatically opens
its preferred unused serial console and requests a read-only status update. If no console is
available, the wizard keeps the scan and manual port choices available.

Use arrow keys and Enter in the wizard. Tab moves to the expert command prompt. TUI commands,
notices, errors, and help stay in Command Output; firmware boot logs and replies stay in the
separate Serial Terminal; build and setup logs stay in Jobs; pairing QR data stays in Pairing.
PageUp and PageDown scroll Command Output, Shift with those keys scrolls Serial Terminal, and
Ctrl with those keys scrolls Jobs. Live serial uses `tio`, which `make tools-install` offers
on supported package managers. `wizard off` hides the guide and expands the workspace;
`wizard on` restores it; `wizard` returns it to the guided home screen. `pane off` fully closes
the side pane and `pane on` restores the most recent one. Press `q` while wizard choices have
focus to hide only the wizard. `help` or `?` lists every direct command and subcommand, and
`quit`, `exit`, or Ctrl+C closes the application. `make tui` is retained as a compatibility
alias.

Every action that programs the board or destroys state it holds stops on a confirmation
screen first: `flash`, `flash-erase`, `rebuild-flash-erase`, and `factoryreset`. The screen
is the same shape each time, drawn in the terminal's danger colour, with declining as the
first choice and Left Arrow returning to where the request came from. Typing the command at
the prompt is not the confirmation; `send <command>` remains the one deliberate bypass.
`factoryreset` asks the connected firmware to erase its own credentials and reboot, which
needs no rebuild to recover from. ESP-IDF and esp-matter still follow their official
installation paths; the TUI detects and explains them but does not silently install
toolchains outside this repository.

## Customize or add a board

Board adapters live in `src/devices.ts`. An adapter declares its discovery hints, command
catalog, parser, and state projection. Add recorded console lines to `test/fixtures/` and
cover the adapter with parser tests before exposing it in the workspace.

## Build a local executable

```sh
make tui-release
```

The command builds macOS arm64 and Linux x64 executables in `tools/tui/dist/`. Tag release
automation tests this exact checkout, builds with the same script, and publishes both
executables with the repository release.
