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

## Visual direction

The workspace imposes no colour scheme. Everything is drawn in the terminal's own default
foreground and ANSI palette, so it inherits whatever theme is already set, including
transparent backgrounds. Hierarchy comes from weight instead: bold marks the one thing that
matters in a region, dim marks what supports it.

Panel labels live in the border rule rather than on a row above it, and hints live in the
bottom rule. That is worth knowing before editing a panel, because OpenTUI drops a border
title that does not fit rather than clipping it. Every title goes through `fitRule` in
`src/motion.ts`, which drops whole entries from the tail; a new one that skips it will
silently render no label at all on a narrow terminal.

Motion only ever reports that something changed. Focus moves and wizard stages ease over
180 ms, new output settles over 400 ms, the panels stagger in once on launch, and a braille
spinner with elapsed seconds appears only while a job runs. An idle workspace is completely
still and schedules nothing. Animations change colour, never layout, and the only one that
changes characters is the spinner, so a frame captured before anything moves is still a
correct screen. Every animated value rests on the theme token rather than a blend, which is
what keeps a settled panel at the terminal's real palette colour.

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
