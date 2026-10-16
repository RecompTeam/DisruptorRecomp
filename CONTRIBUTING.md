# Contributing

Keep contributions source-only. Do not commit disc images, extracted game code
or assets, generated translations, captured overlays, proprietary BIOS files,
binaries, saves, captures, logs, credentials, or machine-specific paths.

Edit project source, configuration, seeds, tests, or the framework checkout,
then regenerate locally. Save a framework change with
`python tools/framework.py refresh`, and list a newly changed file in
`framework/FILES.txt` first. Do not hand-edit `generated/*.c` or include
unrelated framework changes.

## Changes

- Open a pull request against `main`. It merges by squash or rebase once the
  Linux (GCC) and Windows (MinGW-w64) checks pass.
- Write commit subjects in the Conventional Commits style: `feat:`, `fix:`,
  `docs:`, `build:`, `ci:`, `test:`, `refactor:` or `chore:`.
- Run the relevant tests and review both the working diff and staged paths.
- Keep experimental visual features optional.
- Show a change the player can see with a before and after pair: two pictures
  of the same moment, or a short video when it only shows in motion. A replay
  capsule (Ctrl+F12 in the game) keeps the moment. Add the pair to
  [COMPARISONS.md](docs/COMPARISONS.md).
- Update [STATUS.md](docs/STATUS.md) when behavior, verified coverage, or a known
  limitation changes. Keep it focused on the current build. Update the README
  and [PLAYING.md](docs/PLAYING.md) when a setting or its default changes.

## Versions

A release is tagged `vMAJOR.MINOR.PATCH` in the [Semantic
Versioning](https://semver.org) form, with a pre-release label until the game is
done.

- `0.MINOR.0` brings new features or changed behavior: the launcher, a platform,
  a default, save states that stop loading.
- `0.MINOR.PATCH` brings fixes only.
- `-alpha.N` while the full campaign has not been played through or features
  still change.
- `-beta.N` once the campaign has been played through with no known blocker.
- `-rc.N` for a candidate before 1.0.0.
- `1.0.0` when the whole campaign is verified and save states carry over between
  versions.

A release's section in [CHANGELOG.md](CHANGELOG.md) is headed `##
v0.2.0-alpha.1` and so on: the launcher and the packager find it by that
heading.

## Reporting a problem

- Press **Report a problem** in the launcher. It gathers the game's log and
  settings and leaves user names out. For a problem that is hard to catch,
  switch on the detailed log on the Troubleshooting page first.
- Say the level, the steps to reproduce, the render scale and aspect, the
  enhancements or cheats in use, and whether restarting fixes the problem.
- A replay capsule shows us the same moment.
- Never attach game data or proprietary BIOS files.
- Report a security issue as [SECURITY.md](SECURITY.md) says, not in a public
  issue.

## License

By contributing you agree that your contribution is licensed under the
[MIT License](LICENSE).

See [docs/BUILD.md](docs/BUILD.md) for setup and test commands, and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency licenses.
