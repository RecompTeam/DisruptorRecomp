# Getting started

This Windows x64 alpha requires your own **Disruptor (USA, SLUS-00224)** disc
image. Game assets are not included. You do not need to build the project,
extract its executable, or supply a PlayStation BIOS to use the release.

Join the [community Discord](https://discord.gg/aeTQjaQUr) to discuss the project
and share feedback.

## Setup

1. Download the Windows x64 ZIP from the
   [GitHub releases](https://github.com/micmea668/DisruptorRecomp/releases).
   Choose the Windows ZIP asset, rather than GitHub's source-code archives.
2. Use **Extract All** and open the extracted folder. Keep its files together
   in a writable location, such as your Games folder.
3. On Windows 10 or 11 x64, install the latest
   [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe)
   if it is not already installed. The GPU driver must support OpenGL 3.3.
4. Double-click **DisruptorLauncher.exe**, select **Browse...** in the
   **Disc** row, and locate your USA **MODE2/2352** CUE or BIN. Dropping the
   image on the launcher window does the same. Keep a selected CUE beside
   the BIN it references. Filenames do not need to match the package's names.
   A raw image named `.iso` or `.img` is also accepted if its complete bytes
   match the supported dump. A cooked 2048-byte ISO cannot be used.
5. The launcher copies the image into `input/discs/SLUS-00224/`, creates the
   CUE, and checks the complete SHA-256 of the copied data. Your originals are
   kept. Leave **Start the game after importing** checked to boot immediately.
   On later runs, select **Play game**; the disc is verified before every boot.
   **Play Disruptor.cmd** opens the same launcher.
6. At the game menu, use the arrow keys and **Enter** to select **New Game**,
   or choose **Practice Mode** to try a training mission.

The launcher's **Display**, **Enhancements**, **Controls**, **Audio** and
**Experimental** tabs hold the settings of the in-game menu. A change is saved
to `settings.toml`
at once and applies the next time the game starts. **Original** and
**Enhanced** set a group of them in one step, and **Reset settings** returns
them to those of a new installation. The tabs are locked while the game runs.
If the game stops with an error, the launcher shows the end of `startup.log`.
Long settings pages scroll, and hovering over an option shows its explanation.

OpenBIOS is included. A PAL or Japanese disc cannot be the game disc, but it
can give the game its language: see [Other languages](#other-languages). Keep
the raw BIN/CUE format; do not convert the disc to a cooked ISO.

Older releases without `DisruptorLauncher.exe` use manual setup: copy the BIN
and CUE into `input/`, name the CUE `Disruptor (USA).cue`, and run
`Play Disruptor.cmd`. New launcher builds also recognize and import this old
layout automatically. Allow about 610 MiB of additional free space for an
import. A repair preserves previous installed files in an `input/discs/.previous-*`
folder; these backups can be removed manually once the repaired copy works.

## Controls

| Action | Default key or button |
| --- | --- |
| Move / strafe | W / S / A / D |
| Turn | Mouse, after capture; arrow keys also work |
| Release / capture mouse | Escape or middle-click releases; a left or middle click captures again |
| Fire / menu confirm | Left mouse / Enter |
| Psionic attack | Right mouse / F |
| Jump / menu cancel | Space |
| Use | E |
| Choose weapon / psionic | Q / R |
| Map / pause | Tab / P |
| Settings | Backquote (`` ` ``); Escape closes |
| Fullscreen | Alt+Enter |
| Skip a logo or movie | Escape |
| Fast-forward | Hold ] |
| Save / load state | Shift+F1–F11 / F1–F11 |
| Name a save | Type it: letters, digits, Space, Backspace and Delete. Enter moves to DONE |
| Start / end a replay capsule | Ctrl+F12 |
| Load the newest replay capsule | F12 |

Modern mouse/keyboard controls, geometry correction, and perspective textures
are enabled initially. Vertical mouse look is experimental and starts off;
enable it in **Settings → Controls** if desired.
**Smooth mouse turning** in the same tab turns the picture between the game's
256 heading steps. It requires captured mouse aim and exact geometry.
A setting shows its description under the pointer, the same one as in the
launcher. A setting that needs another one stays greyed until that one is on.
In-between frames that are on can still be switched off.

**Settings → Enhancements** offers 1x–8x resolution, widescreen options,
HUD size for widescreen, and 60 FPS gameplay and in-between frames. Both
frame options start off; in-between frames need exact geometry.
Shadows keep the game's own shape at 4:3 and lie flat on the floor in
widescreen. HUD size ranges from 50% to 100% in widescreen and leaves the
weapon size unchanged.
**Gap filling** closes small polygon gaps; **Depth-sorted world** offers an
alternative to the game's polygon order. Both require exact geometry.
**Smooth distance fade** fades walls and floors into the distance evenly,
where the game steps through twelve shades. It requires exact geometry and
starts off.
**Ambient occlusion** shades corners and the floor under things, with
adjustable reach and strength. It requires exact geometry and starts off.
**Settings → Experimental** holds three anti-aliasing settings that start
off and can be combined.
**Anti-aliasing (MSAA / SSAA)** is one list. 2x, 4x, 8x or 16x MSAA draws
the world with that many samples a pixel, which smooths the edges of
polygons and of the HUD's rectangles. The same counts of SSAA also shade
every sample, which smooths textures and cut-out outlines too, the HUD's
among them. The graphics card then shades that many times as much. SSAA
needs OpenGL 4.0 or its sample shading extension, without it the card draws
MSAA of that count. The list requires exact geometry, and video memory grows
with the number and the internal resolution. A card that has fewer samples
uses its most.
**Downscale averaging** averages an internal resolution above the screen's
down to it. **Edge smoothing** smooths the edges left in the world's picture,
sprite outlines among them. With exact geometry the textured pictures of the
HUD and of menus stay as they are. Movies are never filtered.

**Skip the logos and the title movie** in Enhancements skips the boot intro
automatically. It starts off; the other movies still play normally. Escape
can skip the logo or movie currently on screen when the settings menu is closed.

The default is 4x at 4:3; lower the scale if performance is poor. The main
menu and its screens fill a wide screen: the backdrop is stretched, and the
logo and the text keep their shape. Movies, the map and loading screens remain
at 4:3. Controls, display, and audio preferences are saved in `settings.toml`
beside the executable.

## Other languages

The game can take its language from a second disc image that you own. In the
launcher, press **Add disc...** in the **Language** row, or drop the image on
the window, and pick the image of the French, German or Japanese release. The
launcher checks that it fits your US disc and then uses it where it is. It is
not copied, so keep it there. The **Language** list keeps every disc you have
added: choose one of them or **English** before **Play game**. **Remove**
takes a disc off the list and leaves the file alone. The US disc is still
required and stays the game disc.

The same choice is in the game: open **Settings → Language**, press **Choose a
disc image**, then press **Restart now**. **English** there removes the
second disc again.

The French and German discs give their movies, speech, menus, messages and
the hints shown while a mission reloads. The Japanese disc gives its movies,
speech and hints. Its menus are in English on that disc as well. A few long
French and German menu lines are shortened to fit the screen.

Save states belong to the language they were made in. With a language disc
the twelve slots are kept in a folder of their own, `saves/German` for the
German disc, so the slots start empty after a change of language and the
earlier ones are back when you return to it. A state holds what the game has
read of its disc, and the discs are not cut alike. Memory card saves do not
depend on the language.

## Saves and updates

Use the game's memory-card save system for long-term progress. Memory cards and
save states are stored in `saves/`; back up that folder before installing a new
release. Save states are specific to their build and may not load after an
update. **Save states from v0.1.0-alpha.1 and alpha.2 are incompatible with
v0.1.0-alpha.3.**
Use a memory-card save or start a new game, then create new save states.
The original password system is also available in the game.

To avoid importing again, copy `input/discs/` into the new extracted build.
The launcher verifies it on startup. Keep `settings.toml` too if you want to
retain your controls and display preferences.

God Mode resets off on launch. Granting all weapons and psionics marks the game
as cheated, including subsequent game saves.

## Reporting a problem

Press **Report a problem** in the launcher. It gathers the game's log of the
last two runs, the mouse log, your settings, the notes the game leaves of how
its last run ended and of a freeze, and a note of the system into one
ZIP under `reports/` beside the executable and shows it in Explorer. Where
Windows cannot make the ZIP, it is a folder of the same name: attach its
files. The name of your Windows user folder is cut out of paths, and
your disc image, BIOS and saves are never
included. Attach the ZIP to a
[bug report](https://github.com/micmea668/DisruptorRecomp/issues) with the
level and the steps that lead to the problem. For a fault in the picture add
a replay capsule or a screen recording, described below.

For a problem you can repeat, first switch on **Detailed logging** on the
launcher's **Troubleshooting** page, play until it shows, close the game and
then make the report. The log then also says every ten seconds how the
frames ran, each setting changed in the game's menu, controllers connected
and removed, and what the graphics driver complains of. The switch is in the
game's menu too, under **Settings → Debug**, where the driver's part starts
with the next launch.

## Reports and replay capsules

**Settings → Debug** offers diagnostic views and **Record the screen** for
rendering bug reports. Close the settings menu to begin the short recording;
its pictures and timing data are written under `shown/` beside the executable.
Diagnostic views reset to Off at the next start. The **Texture coordinates**
view paints each textured pixel with its place in its texture, so a recording
shows how a texture sits and moves on its polygons. The world's
semi-transparent pixels are left out and show what is behind them, while
those of the HUD and the weapon in hand are painted. It needs exact geometry
and switches ambient occlusion and the anti-aliasing filters off while on.

Press **Ctrl+F12** to start a replay capsule and press it again to finish.
**F12** alone loads the newest capsule: the game stands where that
recording began. While a capsule records, F12 alone ends it. The save
states have F1 to F11.
Every capsule is kept. **Settings → Debug** lists them, the newest on top,
with the last picture, time and length of each, and can load one, open their
folder or delete one. Hold the mouse over a picture to see that capsule play.
The capsule is saved under `capsules/` beside the executable. It contains a
save state, inputs, settings, and build details so a run can be replayed for
investigation. Capsules are specific to their build and are not recorded in
network games. See the [replay instructions](https://github.com/micmea668/DisruptorRecomp/blob/v0.1.0-alpha.4/docs/BUILD.md#replay-capsules)
for the replay tool and its limits.

## Troubleshooting

- **Missing MSVCP140 or VCRUNTIME140 DLL:** install the latest x64 Visual C++
  Redistributable linked above, then relaunch. See Microsoft's
  [runtime download guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/).
- **Disc not found:** open the launcher and browse to your original image.
  If selecting a CUE, keep its referenced BIN in the location named by its
  `FILE` line. Import from an extracted, writable build folder.
- **Import rejected:** the launcher requires the complete, unmodified USA
  raw dump. Cooked ISO files, other regions, missing CUE tracks, offset tracks,
  and multi-track layouts are rejected. Selecting a correct raw BIN directly
  also works; the launcher generates its CUE.
- **Import cancelled or interrupted:** your source files remain intact.
  Browse to the image again to retry. Incomplete data is never used to boot.
- **Wrong disc or boot failure:** only the supported USA revision works. Its
  BIN is 636,350,064 bytes and has SHA-256
  `3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4`.
- **Slow rendering:** lower resolution to 2x or 1x in Enhancements and update
  your graphics driver. Try turning off 60 FPS gameplay and in-between
  frames if they are enabled.
- **Mouse does not turn:** the game holds the mouse while its window has
  focus and mouse aim or modern controls are on. After Escape or a
  middle-click, left-click in the window to capture it again.
- **Other problems:** see [Reporting a problem](#reporting-a-problem). Do not
  attach your game image, game assets, or BIOS files.

This is an early alpha. The first mission and training-level path have been
tested, including the recent 8x / 32:9 corruption fix. Full campaign coverage
remains incomplete, and minor geometry or texture artifacts can remain.
