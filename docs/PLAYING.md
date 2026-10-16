# Getting started

This Windows x64 alpha runs from your own **Disruptor (USA, SLUS-00224)** disc
image. Game assets are not included. You do not need to build the project,
extract its executable or supply a PlayStation BIOS: OpenBIOS is included.

Join the [community Discord](https://discord.gg/hWbFatkXpU) to talk about the
project and share feedback.

## Setup

### Install

1. Download **DisruptorRecomp-win-Setup.exe** from the
   [GitHub releases](https://github.com/RecompTeam/DisruptorRecomp/releases).
2. Run it. Windows may warn of an unknown publisher: press **More info**, then
   **Run anyway**.
3. Setup installs the game for your Windows user, puts shortcuts on the desktop
   and in the Start menu, and opens the launcher. It installs the Microsoft
   Visual C++ x64 Redistributable too when it is missing.

To play without installing, download **DisruptorRecomp-win-Portable.zip**
instead, unpack it into a folder of its own and run
`Disruptor Recompiled.exe`. Your saves and settings stay in its `data` folder.
It needs the
[Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe).

Windows 10 or 11 x64 and a graphics driver with OpenGL 3.3 are needed.

On Linux x64, download **DisruptorRecomp.AppImage**, make it executable with
`chmod +x DisruptorRecomp.AppImage` and run it. It needs glibc 2.35 or newer
(Ubuntu 22.04, Debian 12, Fedora 36 and later) and OpenGL 3.3. Your saves and
settings are kept in `~/.local/share/DisruptorRecompData`. The launcher updates
the AppImage in place.

### Add your disc

1. Open **Disruptor Recompiled** from the desktop or the Start menu.
2. Press **Choose file...** and pick your USA **MODE2/2352** CUE or BIN, or
   drop the image on the launcher's window.
3. The launcher copies the image into `input/discs/SLUS-00224/` in the
   [folder of your files](#where-your-files-are), writes its CUE and checks
   the SHA-256 of every byte. Your original files stay where they are.
4. Leave **Start the game after importing** on to play at once.

What the launcher accepts:

- A CUE with its BIN beside it, or the BIN alone. The file names do not
  matter.
- A raw `.iso` or `.img` whose bytes match the supported dump.
- Not a cooked 2048-byte ISO, and not a PAL or Japanese disc as the game
  disc. Those discs can give the game its language, see
  [Other languages](#other-languages).

An import needs about 610 MiB of free space.

### Play

Press **Play** on the launcher's home page. The disc is checked before every
start. In the game's menu, use the arrow keys and **Enter** to pick
**New Game**, or **Practice Mode** for a training mission.

If the game stops with an error, the launcher says so under its banner.
**Details** shows the end of `startup.log` and everything the launcher did.
**Help** in the side bar shows this guide, and F1 opens it.

### Older releases

Releases up to alpha.5 came as a ZIP. Install the new release with its
Setup.exe, then press **Bring saves...** in the side bar and choose the old
folder: saves, settings, keys and the imported disc come over.

Releases without `DisruptorLauncher.exe` are set up by hand: copy the BIN and
CUE into `input/`, name the CUE `Disruptor (USA).cue` and run
`Play Disruptor.cmd`. The launcher finds such a folder and imports it by
itself.

A repair keeps the files it replaced in an `input/discs/.previous-*` folder.
Delete it once the repaired copy works.

## Controls

| Action | Default key or button |
| --- | --- |
| Move / strafe | W / S / A / D |
| Turn | Mouse. The Left and Right arrows too |
| Free / hold the mouse | The game holds it while its window has focus. Escape or a middle-click frees it, a left click holds it again |
| Fire / menu confirm | Left mouse / Enter |
| Psionic attack | Right mouse / F |
| Jump / menu cancel | Space |
| Use | E |
| Choose weapon / psionic | Q / R |
| Map / pause | Tab / P |
| Settings | Backquote (`` ` ``). Escape closes |
| Fullscreen | Alt+Enter |
| Skip a logo or movie | Escape |
| Fast-forward | Hold ] |
| Save / load state | Shift+F1–F11 / F1–F11 |
| Name a save | Type it: letters, digits, Space, Backspace and Delete. Enter moves to DONE |
| Start / end a replay capsule | Ctrl+F12 |
| Load the newest replay capsule | F12 |

The table shows the Modern scheme, which a new installation has.

### Scheme and keys

**Control scheme** on the launcher's Controls page, and in the game's
settings, is Modern or Classic.

- **Modern:** WASD and the mouse. The left button fires, the right one uses
  psionics, the side buttons choose weapon and psionic. The Up and Down arrows
  also walk while no action has them.
- **Classic:** the original keys. The mouse only turns the view.

The **Keys** table under it puts any action on another key: click the key,
then press the new one. A key another action had moves over, and that action
takes the old key. **Default keys** brings back the scheme's own keys.

These keys do the same in both schemes and cannot be changed:

- **Escape** frees the mouse, closes the settings, skips logos and movies.
- **Backquote** (`` ` ``) opens the settings.
- **B** fast-forwards while held, except on the save-name screen.
- **F1 to F11** load a save state. With Shift they save one.
- **F12** loads the newest replay capsule. Ctrl+F12 records one.
- **Alt+Enter** switches between a window and fullscreen.

The keys are kept in `keybinds.ini` in the folder of your files. A change of
scheme brings the other scheme's layout only while the keys are a scheme's
own: keys you set yourself stay.

### Mouse

- **Vertical mouse look** is experimental and starts off. Switch it on on the
  Controls page. It tilts at the horizontal speed unless **Separate vertical
  speed** is on.
- Speeds are shown as a multiple of the default speed, 1.0x.
- **Smooth mouse turning** is on at first. It turns the picture between the
  game's 256 heading steps and needs captured mouse aim and exact geometry.

### Gamepad

A connected gamepad plays beside the keyboard and mouse. The launcher's
Controls page and the game's Controls tab name the one connected.

| Xbox | PlayStation | In the game |
| --- | --- | --- |
| Left stick or D-pad | Left stick or D-pad | Move and turn |
| A | Cross | Fire, confirm |
| B | Circle | Jump, cancel |
| X | Square | Psionic attack |
| Y | Triangle | Use |
| LB | L1 | Choose weapon |
| RB | R1 | Choose psionic |
| LT | L2 | Strafe left |
| RT | R2 | Strafe right |
| View | Create | Map |
| Menu | Options | Pause |

- **Stick dead zone** sets how far the stick moves before it counts. Raise it
  if you walk or turn without touching the stick.
- To change a button, connect the gamepad, click the button on the launcher's
  Controls page, then press the new one on the gamepad. Escape cancels.
  **Default buttons** brings back the table above. The left stick and the
  D-pad always move and turn.
- The buttons are kept in `input.ini`, in the folder of your files.

## Settings

### Where to change them

- In the launcher, under **Settings**: the pages **Display**,
  **Enhancements**, **Controls**, **Audio**, **Experimental** and
  **Troubleshooting**.
- In the game, with Backquote: tabs of the same names. The sound is on its
  **System** tab and **Detailed logging** on **Debug**. Keys and gamepad
  buttons are changed in the launcher only.

A change is saved to `settings.toml` in the folder of your files at once. A
change in the launcher applies the next time the game starts. The launcher's
pages are locked while the game runs.

The launcher shows what a setting does under its name, the game under the
pointer. One that needs another setting says which, and stays grey until that
one is on. A setting that is on can always be switched off.
**Reset this page...** at the foot of a launcher page returns the settings
on that page to what a new installation has.

### Display

- **Display mode**, **VSync** and **Internal resolution scale**, 1x to 8x.
  The default is 4x at 4:3. Lower the scale if the game runs slowly.
- **Aspect ratio**, 4:3 or wider. The main menu and its screens fill a wide
  screen: the backdrop is stretched, the logo and the text keep their shape.
  Movies, the map and loading screens stay 4:3.
- **HUD size** for widescreen, from 50% to 100%. The weapon in hand keeps its
  size.
- Shadows keep the game's own shape at 4:3 and lie flat on the floor in
  widescreen.

### Enhancements

The page is in four groups, in the launcher and in the game alike.

| Group | Settings | At first |
| --- | --- | --- |
| Frame rate | **60 FPS gameplay** | On |
| Frame rate | **In-between frames** and their rate | Off |
| Picture | **Exact-provenance geometry**, **Perspective-correct world textures**, **Gap filling**, **Smooth distance fade**, **Depth-sorted world** | On |
| Shading | **Ambient occlusion**, its reach and strength | Off |
| Startup | **Skip the logos and the title movie** | Off |

- In-between frames, the last three of Picture and Ambient occlusion need
  exact geometry.
- **Gap filling** closes small gaps between polygons.
- **Smooth distance fade** fades walls and floors evenly into the distance,
  where the game steps through twelve shades.
- **Depth-sorted world** draws world polygons by their depth where the game's
  own order puts a far one over a near one.
- **Ambient occlusion** shades corners and the floor under things.
- **Skip the logos and the title movie** skips only the boot intro. The other
  movies play. Escape skips the logo or movie on screen while the settings
  menu is closed.

### Experimental

**Anti-aliasing (MSAA / SSAA)** starts off and needs exact geometry.

- MSAA at 2x, 4x, 8x or 16x smooths the edges of polygons and of the HUD's
  rectangles.
- SSAA at the same counts also smooths textures and cut-out outlines, the
  HUD's among them, and costs the graphics card that many times the shading.
  It needs OpenGL 4.0 or its sample shading extension. Without it the card
  draws MSAA of that count.
- A card with fewer samples uses its most.
- Video memory grows with the count and the internal resolution.

## Other languages

The game can take its language from a second disc image you own: the
French, German or Japanese release. The US disc stays the game disc and is
still needed.

### Add a language disc

- **In the launcher:** press **Add disc...** under **Language**, or drop the
  image on the window. The launcher checks that it fits your US disc and uses
  it where it is. It is not copied, so keep it there.
- **In the game:** open **Settings → Language**, press **Choose a disc
  image**, then press **Restart now**.

The **Language** list keeps every disc you added. Choose one of them or
**English** before **Play**. The **×** beside the list takes a disc off the
list and leaves its file alone. **English** in the game's menu removes the
second disc too.

### What each disc gives

- **French and German:** movies, speech, menus, messages and the hints shown
  while a mission reloads. A few long menu lines are shortened to fit.
- **Japanese:** movies, speech and hints. Its menus are in English on that
  disc too.

### Save states and languages

Save states belong to the language they were made in, because the discs are
not cut alike. With a language disc the twelve slots live in a folder of
their own, `saves/German` for the German disc. The slots are empty after a
change of language and come back when you return to it. Memory card saves do
not depend on the language.

## Where your files are

Your saves, settings, keys and gamepad buttons, replay capsules, reports and
the imported disc are kept apart from the program, wherever an update
replaces it:

| Copy | Folder of your files |
| --- | --- |
| Installed by Setup | `%LOCALAPPDATA%\DisruptorRecompData` |
| A ZIP of alpha.5 or earlier, or a build folder | the folder of the executable |

- **Saves folder** in the launcher's side bar opens `saves/` there.
  **About** names the folder.
- An update or an uninstall leaves the folder of an installed copy alone.
  Delete it by hand to remove your saves for good.
- Setting `PSX_USER_DIR` to a whole path makes the launcher and the game use
  that folder instead.

### Bring files from an earlier copy

**Bring saves...** in the side bar brings the files of an earlier copy, such
as an unzipped older release, into this one.

1. Choose the folder of that copy.
2. The launcher lists what it found: saves, settings, keys and gamepad
   buttons, capsules, mods and the imported disc.
3. It copies them once you agree. The earlier copy is left as it was.

A file here that one of them replaces is kept in `backups/replaced-<time>/`
first. The disc is not copied when this copy has one already. Save states of
the earlier build are then tried, see [Saves and updates](#saves-and-updates).
This also saves a second import after unzipping a new release.

## Saves and updates

### Updates

The launcher's home page says whether a newer release is out and offers to
download and install it. **What's new** beside it, or **Update options**, and
the version at the foot of the side bar open the **Updates** window: the
release notes, this copy's version and **Check at start**, which looks for a
newer release each time the launcher opens. A portable copy is updated the same
way, in its own folder, and only when you press the button. A copy unzipped
from a ZIP of alpha.5 or older is updated by installing the new release with
its Setup.exe.

Below them the home page shows what is new: in this version, or in the next one
once it is out. The notes come from [CHANGELOG.md](../CHANGELOG.md).

Before the updater installs a new version, the launcher copies `saves/` to
`backups/saves-<time>-<version>/` beside it. It keeps the newest five copies.

### Saves

- Use the game's memory card saves for long-term progress. They do not
  depend on the build. The game's password system works too.
- Memory cards and save states are stored in `saves/` in the folder of your
  files.
- **Save states from v0.1.0-alpha.1 and alpha.2 are incompatible with
  v0.1.0-alpha.3.** Load a memory card save or start a new game, then make
  new save states.

### Save states of an earlier build

A save state carries the mark of the build that made it, and the game loads
only its own. When the launcher finds states of an earlier build, it tries
each one on this build in a hidden, silent run of a second.

- A state that runs is given this build's mark.
- A state that does not run stays as it was and is not tried again.
- The originals are kept in `saves/old-states/` either way.

### Cheats

God Mode is off at every start. Granting all weapons and psionics marks the
game as cheated, the saves made after it included.

## Reporting a problem

### Make a report

Press **Report a problem** in the launcher. It gathers into one ZIP:

- the game's log of the last two runs and the mouse log,
- your settings,
- the notes the game leaves of how its last run ended and of a freeze,
- a note of the system.

The ZIP goes to `reports/` in the folder of your files and Explorer shows it.
Where Windows cannot make a ZIP, it is a folder of the same name: attach its
files. The name of your Windows user folder is cut out of paths, and your
disc image, BIOS and saves are never included.

Attach the ZIP to a
[bug report](https://github.com/RecompTeam/DisruptorRecomp/issues) with the
level and the steps that lead to the problem. For a fault in the picture, add
a replay capsule or a screen recording, see
[Recordings and capsules](#recordings-and-capsules).

### A problem you can repeat

1. Switch on **Detailed logging** on the launcher's **Troubleshooting** page.
2. Play until the problem shows, then close the game.
3. Make the report.

The log then also says every ten seconds how the frames ran, and records
each setting changed in the game's menu, controllers connected and removed,
and what the graphics driver complains of. The switch is in the game's menu
too, under **Settings → Debug**. There the driver's part starts with the
next launch.

## Recordings and capsules

### Screen recording and views

**Settings → Debug** in the game offers diagnostic views and **Record the
screen**, for reports of a fault in the picture.

- Close the settings menu to start the short recording. Its pictures and
  timing data go to `shown/` in the folder of your files.
- Diagnostic views are back to Off at the next start.
- The **Texture coordinates** view paints each textured pixel with its place
  in its texture, so a recording shows how a texture sits and moves on its
  polygons. The world's semi-transparent pixels are left out and show what is
  behind them. Those of the HUD and the weapon in hand are painted. The view
  needs exact geometry and turns ambient occlusion and the anti-aliasing
  filters off while it is on.

### Replay capsules

A capsule records a stretch of play so that it can be replayed for
investigation. It holds a save state, the inputs, the settings and the
build's details.

- **Ctrl+F12** starts a capsule and ends it. F12 alone also ends one that is
  recording.
- **F12** loads the newest capsule: the game stands where that recording
  began. The save states have F1 to F11.
- Every capsule is kept in `capsules/` in the folder of your files.
- **Settings → Debug** lists them, the newest on top, with the last picture,
  time and length of each. It can load one, open their folder or delete one.
  Hold the mouse over a picture to see that capsule play.
- A capsule replays only on the build that made it. Network games record
  none.

The [replay instructions](https://github.com/RecompTeam/DisruptorRecomp/blob/v0.1.0-alpha.4/docs/BUILD.md#replay-capsules)
describe the replay tool and its limits.

## Troubleshooting

- **Missing MSVCP140 or VCRUNTIME140 DLL:** install the latest
  [x64 Visual C++ Redistributable](https://aka.ms/vc14/vc_redist.x64.exe),
  then start again. Setup installs it, a copy from an older ZIP does not. See
  Microsoft's
  [runtime download guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/).
- **Disc not found:** open the launcher and choose your original image again.
  A CUE needs its BIN where its `FILE` line says. Import from an extracted,
  writable folder.
- **Import rejected:** only the complete, unmodified USA raw dump is taken.
  Cooked ISO files, other regions, missing CUE tracks, offset tracks and
  multi-track layouts are refused. A correct raw BIN alone works too: the
  launcher writes its CUE.
- **Import cancelled or interrupted:** your source files are untouched.
  Choose the image again. Incomplete data is never used to start the game.
- **Wrong disc or the game does not start:** only the supported USA revision
  works. Its BIN is 636,350,064 bytes with the SHA-256
  `3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4`.
- **Slow rendering:** lower the resolution to 2x or 1x on the Display page and
  update your graphics driver. Turn off 60 FPS gameplay and in-between frames
  if they are on.
- **Mouse does not turn:** the game holds the mouse while its window has
  focus and **Horizontal mouse aim** is on or the scheme is Modern. After
  Escape or a middle-click, left-click in the window to hold it again.
- **Other problems:** see [Reporting a problem](#reporting-a-problem). Do not
  attach your game image, game assets or BIOS files.

This is an early alpha. The first mission and the training-level path have
been tested, the 8x / 32:9 corruption fix included. The whole campaign is not
covered yet, and small geometry or texture faults can remain.
