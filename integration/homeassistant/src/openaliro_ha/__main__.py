"""Module entry point for the HA=1-only staging command."""

from .cli import main


if __name__ == "__main__":
    raise SystemExit(main())
