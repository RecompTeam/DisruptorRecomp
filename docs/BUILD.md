# Building from source

## Requirements

- The verified Disruptor USA executable and BIN/CUE files in `input/`; see
  [DISC.md](DISC.md).
- Git, Python 3.11+, CMake 3.24+, and a C++20 toolchain.
- Windows x64: Visual Studio with **Desktop development with C++**, or
  MSYS2 MinGW-w64. Ninja is recommended.
- Linux x64: GCC 13 or newer and the development libraries SDL builds
  against (the `build` workflow's list). The launcher builds there too.
  Windows is the platform play is tested on.

The first build needs network access to fetch the pinned PSXRecomp revision and
dependencies. OpenBIOS is built locally; no proprietary PlayStation BIOS is
required.

## Windows

Run from an x64 developer PowerShell at the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
.\run.ps1
```

The build verifies the executable, applies the framework overlay, generates and
audits the translated code, builds the runtime, and runs the tests. The runtime
is produced in `build/`, or `build/Release/` for a multi-configuration generator.
Set `DISRUPTOR_BUILD_DIRECTORY` to build into and run from another folder of the
repository instead. `build.sh` and `run.sh` read it too. Set
`DISRUPTOR_COMPILER_LAUNCHER` to a compiler cache such as `ccache` and `build.sh`
compiles through it.

Windows builds also produce `DisruptorLauncher.exe` beside the runtime and
stage its configuration, help, and initial settings there. The launcher draws
with Dear ImGui through SDL3's renderer, Direct3D 11 or SDL's software renderer.
Under MSVC it is compiled with a static
runtime, with an SDL3 build of its own that holds only the window, its events
and dialogs, so setup can open before the game runtime's VC++ redistributable
is installed. There it also links Velopack's C library, which configuring
downloads, to update a copy the updater installed. A MinGW launcher has no
updater. It imports existing disc dumps; physical drive reading is outside the
current scope. Release packaging includes it automatically through
`tools/package_release.py` and still excludes game data.

## Releases

A release is a Velopack `Setup.exe` and a portable ZIP, packed from a Release
configuration whose `PSX_GAME_VERSION` is the release's version:

```powershell
python tools/package_release.py --version v0.2.0-alpha.1 --build build --zstd-license <zstd LICENSE>
```

- `CHANGELOG.md` needs the release's `## v0.2.0-alpha.1` section first. It
  becomes the package's notes, which the launcher shows.
- The GitHub release's text is `release/notes/v0.2.0-alpha.1.md` when it is
  written, else that section. The packager adds the Install and Verify
  sections below it.
- The .NET SDK runs the pinned Velopack packer, the version of the C library
  the launcher links.
- The previous release's package is fetched from GitHub when there is one,
  and the update then also gets a delta package. Without it, or with
  `--no-delta`, an update downloads the full package.
- `build/releases/<version>/windows/packages` holds
  `DisruptorRecomp-win-Setup.exe` and `DisruptorRecomp-win-Portable.zip` for
  players, each with its SHA-256, and the packages the launcher updates from.
  Setup installs the Visual C++ x64 Redistributable when it is missing, the ZIP
  does not. `--publish` creates the
  GitHub pre-release with them.
- `--publish`, or `--sign` alone, signs those files with Sigstore first. A
  browser opens: sign in with the organisation's Google account,
  `recomp.team@gmail.com`. Each file gets a `.sigstore.json` bundle and a line
  of SLSA provenance in `DisruptorRecomp-<channel>.intoto.jsonl`, and the
  script stops when a bundle names another account. It signs only a checkout
  without uncommitted changes.

A Linux release is an AppImage packed from a Linux Release build of the same
commit. It is built on Ubuntu 22.04, so that it runs on glibc 2.35 and newer:
GCC 13 from the `ubuntu-toolchain-r/test` PPA, Kitware's CMake and Python 3.11,
newer than the ones 22.04 ships. The C++ runtime is linked statically and OpenGL through
`libGL.so.1`, so glibc and a GL driver are all it asks of a player's system.
From Windows, with that build reachable through WSL:

```powershell
python tools/package_release.py --platform linux --version v0.2.0-alpha.1 `
    --build \\wsl.localhost\<distro>\<checkout>\build --zstd-license <zstd LICENSE>
```

The Linux build folder must hold a `SOURCE_COMMIT` file with the commit it was
built from, and the script stops unless that is the checkout's. It makes
`DisruptorRecomp.AppImage` and the packages the launcher updates from in
`build/releases/<version>/linux/packages`. With `--publish` they go to the release the Windows run made, which must
come first and from the same commit, and the release text gets the Linux
install lines.

The launcher and the game find the player's files by one rule,
`src/game/settings/user_folder.cpp`. A copy the updater installed keeps them in
`%LOCALAPPDATA%\DisruptorRecompData`, a portable copy of the updater in `data`
beside its `current` folder, the AppImage in `$XDG_DATA_HOME` or
`~/.local/share` under `DisruptorRecompData`, and any other copy, a build
folder included, beside the executable. A whole path in `PSX_USER_DIR` names another folder.
The framework's `main.cpp` asks `psx_game_user_folder` for it before anything
else and writes settings, bindings, memory cards, caches, mods and the notes of
a run there. Program files are still found beside the executable. The shipped
`settings.toml` and `keybinds.ini` are copied into a new folder of the player's
files.

For launcher-only iteration after configuration:

```powershell
cmake --build build --config Release --target disruptor-launcher disruptor-disc-import-test
ctest --test-dir build -C Release -R disruptor_disc_import --output-on-failure
```

`DisruptorLauncher.exe --verify IMAGE` checks an image without copying it.
`--import IMAGE --root FOLDER` performs the normal import without starting the
game; `--check --root FOLDER` verifies installed data and required build files.
`--root` names one folder for the program and the player's files alike.
These diagnostic modes return zero only on success and report to redirected
stdout/stderr. The GUI always resolves the build folder from its executable,
so shortcuts work even when opened from a different working directory.

Supported revisions are defined in `src/launcher/disc_import.cpp`. Future PAL
and Japanese entries must include the full raw-disc size/hash and a matching
game configuration and compiled runtime support. Do not enable a region by
serial alone; the current game hooks and translated code are USA-specific.

Optional launch switches can be combined:

```powershell
.\run.ps1 -ModernControls -Widescreen -GeometryCorrection -PerspectiveTextures
.\run.ps1 -ModernControls -VerticalLook
```

`-MouseAim` enables horizontal mouse aim without the modern action bindings.
Use the in-game settings menu for render scale, other aspect ratios, fullscreen,
audio, and control preferences.

## Linux

```sh
chmod +x build.sh run.sh tools/regen.sh
./build.sh
./run.sh --modern-controls --widescreen --geometry-correction --perspective-textures
```

`--mouse-aim` and `--vertical-look` are also available.

## Without the game

A machine without the game can still check a change:

```sh
DISRUPTOR_WITHOUT_GAME=1 ./build.sh
```

This compiles every source except the translated game code, leaves the runtime
unlinked, and runs the tests that read no retail data. It needs Ninja. The
`build` workflow runs it on Linux and under MSYS2 MinGW-w64 for every pull
request.

## In-between frames

The presentation-only frame interpolator is built by default, but starts off
until enabled in **Settings → Enhancements**. Configure with
`-DDISRUPTOR_FRAME_INTERPOLATION=OFF` to leave it out.

## Generated game code

The build scripts generate the translated resident game code locally from your
verified executable. That retail-derived code is excluded from this source
repository. The Windows alpha reads game assets and runtime-loaded code from
the player's disc; it does not require loose game executables or captured files.

If a private regeneration produces `generated/overlays_static.c`, CMake can
also link that optional static overlay. The current alpha does not use it.
Keep generated game translations, disc files, captures, and binaries outside
Git; publish runtime binaries as release assets rather than source commits.

## Incremental build and tests

After the initial setup, use the same compiler environment:

```sh
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The project's changes to the framework are patches against the revision in
`framework/PIN`. `framework/FILES.txt` lists every changed file, a `+` marking
one the project adds. To change the framework, edit the file in the `psxrecomp`
checkout, build and test, then save the edit as a patch:

```sh
python tools/framework.py refresh
```

After pulling changes to `framework/`, bring the checkout up to date:

```sh
python tools/framework.py apply
```

`apply` refuses to overwrite an edit that `refresh` has not saved, and
`python tools/framework.py check` tells whether the checkout holds exactly what
`framework/` describes. Runtime preferences live in
`settings.toml` in the folder of the player's files, beside the executable in a
build folder; game files, generated code, and build directories are ignored by
Git.

The framework reaches the game through one header, `runtime/include/game_host.h`,
which `src/game/host.cpp` answers. The instructions the game hooks are listed in
`game.toml` under `[[recompiler.instruction_hook]]` for code that runs from RAM.
The code generator holds its own copy for native code, and a contract keeps the
two equal. The game's section of `settings.toml` belongs to the game as well:
its fields are in `src/game/settings/game_user_settings.h`, and
`src/game/settings/saved.cpp` reads and writes them for the framework's reader.

A save state carries a key made of the code generator's sources, listed in
`psxrecomp/runtime/codegen_hash_sources.cmake`. Any edit to one of them, a
comment included, makes every earlier save state and capsule unloadable. Those
files change only together with the pin.

## Layout

| Folder | Holds |
| --- | --- |
| `src/game/` | The game's own module by area: `input`, `render`, `frame`, `language`, `session`, `settings`, `ui` |
| `src/launcher/` | The Windows launcher |
| `framework/` | The pinned framework revision, the project's patches to it and the files it adds |
| `tests/unit/` | Unit tests, one executable each |
| `tests/contracts/` | Source contracts, run by Python |
| `tests/support/` | Stubs and fakes the unit tests build against |
| `cmake/` | Build helpers: test registration, launcher staging |
| `config/` | Key presets |
| `tools/` | Regeneration, the overlay, packaging, capsule replay |

`game.toml`, `game-widescreen.toml` and `mouse-aim.ini` stay at the root: the
game is started from there and reads them by those names.

A unit test is registered in `CMakeLists.txt` with
`disruptor_unit_test(<test> tests/unit/<file>)` and a contract with
`disruptor_contract(<name>)` for `tests/contracts/test_<name>_contract.py`.

## Replay capsules

Ctrl+F12 in the game starts a recording and Ctrl+F12 again ends it. The game
writes a folder under `capsules/` beside `settings.toml`: a save state, the
input of every frame after it, the settings and the build it was made with.
A recording ends by itself after ten minutes, or when a save state is loaded.
The word REC shows in a corner for a second when a recording starts, and REC
saved when it ends.

```sh
python tools/replay_capsule.py capsules/1791553413 --pictures out
```

This plays the capsule back without a window or sound, writes each frame as a
PNG and exits with 0 when the replay stayed in step with the recording. A
replay is the same run every time. It is not the recorded run bit for bit: a
loaded state takes its interrupts a few instructions apart, so some frames
differ from the recording in a few bytes of the game's memory, from one in ten
to seven in ten in the runs measured. A replay counts as in step when it has the
recording's memory at some frame of its last 60.

A capsule holds the pad buttons, the heading the mouse wrote, direct weapon
selections and the pitch of
the vertical look. It does not hold analog sticks, cheats or anything changed
in the settings menu while it was recorded. It plays in the build that made
it and with the settings it was made with, and it is not recorded in a network
game. The pictures are of the software frame, which a wide aspect squashes:
the stretched and in-between frames need a window.
