# Contributing

Keep contributions source-only. Do not commit disc images, extracted game code
or assets, generated translations, captured overlays, proprietary BIOS files,
binaries, saves, captures, logs, credentials, or machine-specific paths.

Edit project source, configuration, seeds, tests, or `psxrecomp-overlay/`, then
regenerate locally. Every framework-overlay change must be listed in
`PSXRECOMP_OVERLAY_FILES.txt`; do not hand-edit `generated/*.c` or include
unrelated framework changes.

Before submitting a change:

- Run the relevant tests and review both the working diff and staged paths.
- Keep experimental visual features optional.
- Update [STATUS.md](docs/STATUS.md) when behavior, verified coverage, or a known
  limitation changes. Keep it focused on the current build.

For bug reports, include the level, steps to reproduce, render scale/aspect,
enabled enhancements or cheats, and whether restarting fixes the problem.
Never attach game data or proprietary BIOS files.

See [docs/BUILD.md](docs/BUILD.md) for setup and test commands, and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency licenses.
