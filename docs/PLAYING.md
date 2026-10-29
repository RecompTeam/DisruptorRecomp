# Getting started

Disruptor Recompiled runs on Windows x64 and Linux x64 from your own **Disruptor (USA,
SLUS-00224)** disc image.

- Game assets are not included.
- You do not need to build the project or extract its executable.
- No PlayStation BIOS is needed: OpenBIOS is included.

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

To play without installing:

- Download **DisruptorRecomp-win-Portable.zip** instead.
- Unpack it into a folder of its own and run `Disruptor Recompiled.exe`.
- Your saves and settings stay in its `data` folder.
- It needs the
  [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe).

Windows 10 or 11 x64 and a graphics driver with OpenGL 3.3 are needed.

On Linux x64:

- Download **DisruptorRecomp.AppImage**.
- Make it executable: `chmod +x DisruptorRecomp.AppImage`
- Run it.
- It needs glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36 and later)
  and OpenGL 3.3.
- Your saves and settings are kept in `~/.local/share/DisruptorRecompData`.
- The launcher updates the AppImage in place.

### Add your disc

1. Open **Disruptor Recompiled** from the desktop or the Start menu.
2. Press **Choose file...** and pick your USA **MODE2/2352** CUE, BIN or CHD,
   or drop the image on the launcher's window.
3. The launcher copies the image into `input/discs/SLUS-00224/` in the
   [folder of your files](#where-your-files-are), writes its CUE and checks
   the SHA-256 of every byte. A CHD stays a CHD, and the bytes checked are the
   sectors in it. Your original files stay where they are.
4. Leave **Start the game after importing** on to play at once.

What the launcher accepts:

- A CUE with its BIN beside it, or the BIN alone. The file names do not
  matter.
- A raw `.iso` or `.img` whose bytes match the supported dump.
- A `.chd` that chdman made of the supported dump.
- Not a cooked 2048-byte ISO, and not a PAL or Japanese disc as the game
  disc. Those discs can give the game its language, see
  [Other languages](#other-languages).

An import needs about 610 MiB of free space, about 425 MiB for a CHD.

### Play

Press **Play** on the launcher's home page. The disc is checked before every
start.

In the game's menu, pick **New Game** with the arrow keys and **Enter**, or
**Practice Mode** for a training mission.

- If the game stops with an error, the launcher says so in its banner, beside
  **Play**.
- **Details** there shows the end of `startup.log` and everything the launcher
  did.
- **Help** in the side bar shows this guide. F1 opens it too.

### Older releases

Releases up to alpha.5 came as a ZIP. Install the new release with its
Setup.exe, then press **Bring saves...** in the side bar and choose the old
folder: saves, settings, keys and the imported disc come over.

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
| Select weapon directly | 1–9 / 0 or the keypad, when Weapon and psionic keys is on |
| Select psionic directly | The keys you give the powers, when Weapon and psionic keys is on |
| Map / pause | Tab / P |
| Settings | Backquote (`` ` ``). Escape closes |
| Fullscreen | Alt+Enter |
| Skip a logo or movie | Escape |
| Fast-forward | Hold B |
| Save / load state | Shift+F1–F11 / F1–F11 |
| Name a save | Type it: letters, digits, Space, Backspace and Delete. Enter moves to DONE |
| Start / end a replay capsule | Ctrl+F12 |
| Load the newest replay capsule | F12 |

The table shows the Modern scheme, which a new installation has.

**Weapon and psionic wheels** replace both selection lists. The setting is on
the Controls page, experimental and off at first.

- Hold the key or gamepad button of Choose weapon or Choose psionic. In the
  Modern scheme these are **Q** and **R** until you move them.
- Point with the mouse or the left stick and let go to equip.
- The arrows and the D-pad step through what you own. An arrow you bind to an
  action other than moving or turning does not step.
- A left click or Enter confirms.
- Escape, a right click or the gamepad's cancel button cancels.
- The psionic wheel opens once you have a psionic.
- The game keeps running while a wheel is open.

**Weapon and psionic keys** is on the Controls page too, off at first, in either
scheme. Each weapon and each psionic power takes two keys. The
weapons have these at first, the powers none:

| Key | Weapon |
| --- | --- |
| 1 | Baton |
| 2 | 18mm Semi |
| 3 | 18mm Auto |
| 4 | Phase Rifle |
| 5 | Phase Repeater |
| 6 | Lockon Cannon |
| 7 | AM Blaster |
| 8 | AM Cyclone |
| 9 | Zodiac |
| 0 | Plasma Lance |

- The second key is the same number on the keypad. Each press picks once.
- A key that changes the weapon or the power shows the weapon list or the
  psionics list for a second, as while its button is held. The key of the one
  already in hand shows no list.
- A baton you pick stays in hand. On its own the game takes a gun with
  ammunition instead.
- With the setting on, the Weapons and Psionics tables under it change them.
  Click a key, then press the new one. Delete takes a key away. Default weapon
  and psionic keys puts these back.
- A key an action has cannot go to a weapon or a power, and theirs cannot go to an
  action. The note says who has it, and Take it moves it: the one that had it gets
  the other's old key, or none. Two weapons or powers swap their keys.
- In the Modern scheme an arrow a weapon or a power has no longer walks.
- The fixed keys, Shift, Ctrl, Alt and the system key never pick. While Alt is
  held nothing is picked, so Alt+Enter stays the window's.
- Menus and the save-name screen keep their keys for their own input.
- A key that an action gets in another scheme does only that action. The table
  shows it faint.
- Blast's key still picks it once the game turns it into TeraBlast.
- **Psionic keys use the power**, under the setting and off at first, makes a
  power's key use the power too, as Psionic attack does. A power not in hand is
  used the moment the game has it ready, and the game reads its pictures from
  the disc four times as fast, so it is ready in about half the time. Holding
  the key keeps using it. While it is off the key only picks the power.

### Scheme and keys

**Control scheme** on the launcher's Controls page, and in the game's
settings, is Modern or Classic.

- **Modern:** WASD and the mouse. The left button fires, the right one uses
  psionics, the side buttons choose weapon and psionic: they are these actions'
  second keys. The Up and Down arrows also walk while no action has them.
- **Classic:** the original keys. The mouse only turns the view.

The **Keys** table under it puts any action on another key: click the key,
then press the new one. A key another action had moves over, and that action
takes the old key. Delete takes an action's key away. **Default keys** brings
back the scheme's own keys once you say yes. It is greyed out while the keys
are the scheme's own.

Each action takes two keys. The second may be a mouse button: click the second
key, then click it with the button. The left, the right and the two side
buttons can be given. The middle button stays the one that frees and holds the
mouse. A mouse button works while the game holds the mouse, in either scheme,
and only as a second key.

**Profiles** keep two sets of keys: Save to 1 or 2 keeps every key of the page,
the second keys and the weapons' and the powers' too, and Load 1 or 2 brings
one back. A profile saved before second keys existed leaves them as they are. Save asks
before it replaces a profile saved earlier. The scheme and the mouse settings
stay out of a profile. The profiles are `keys-1.ini` and `keys-2.ini` beside
`keybinds.ini`.

These keys do the same in both schemes and cannot be changed:

- **Escape** frees the mouse, closes the settings, skips logos and movies.
- **Backquote** (`` ` ``) opens the settings.
- **B** fast-forwards while held, except on the save-name screen.
- **F1 to F11** load a save state. With Shift they save one.
- **F12** loads the newest replay capsule. Ctrl+F12 records one.
- **Alt+Enter** switches between a window and fullscreen.

The keys are kept in `keybinds.ini` in the folder of your files, the second
keys in `settings.toml`. A change of scheme brings the other scheme's layout
only while the keys are a scheme's own: keys you set yourself stay.

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

**Stick layout** sets what the sticks do in play:

| Stick layout | Left stick | Right stick |
| --- | --- | --- |
| Dual analog, at first | Move and strafe | Turn |
| Classic | Move and turn, as the D-pad | Nothing |
| Southpaw | Turn | Move and strafe |

The original game read only the buttons, so the sticks here are an addition.
In menus the left stick works as the D-pad in every layout.

| Xbox | PlayStation | In the game |
| --- | --- | --- |
| D-pad | D-pad | Move and turn |
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

- A light push of the turning stick turns slowly, for fine aim. A full push
  turns as fast as the D-pad at **Turning speed** 100%, which goes from 25% to
  300%. **Smooth mouse turning** smooths the stick's turn too.
- **Turning dead zone** sets how far the turning stick moves before it turns.
- With **Vertical mouse look** on, the turning stick also looks up and down.
  **Invert vertical stick** turns that around.
- **Stick dead zone** sets how far the moving stick goes before it counts.
  Raise it if you walk without touching the stick.
- To change a button, connect the gamepad, click the button on the launcher's
  Controls page, then press the new one on the gamepad. Escape cancels.
  **Default buttons** brings back the table above once you say yes. The sticks
  and the D-pad keep their places.
- The buttons are kept in `input.ini`, in the folder of your files.

## Settings

### Where to change them

- In the launcher, under **Settings**: the pages **Display**,
  **Enhancements**, **Controls**, **Audio**, **Experimental** and
  **Troubleshooting**. **Experimental** holds new settings while they are tried, none in this version. **Achievements** has a page of its own under **Play**. **Controls** has four tabs: **Keys**, **Weapons**,
  **Mouse** and **Gamepad**.
- In the game, with Backquote: tabs of the same names. **Detailed logging**
  is on **Debug**. Keys and gamepad buttons are changed in the launcher
  only.

A change is saved to `settings.toml` in the folder of your files at once. A
change in the launcher applies the next time the game starts. The launcher's
pages are locked while the game runs.

The launcher shows what a setting does under its name, the game under the
pointer. One that needs another setting says which, and stays grey until that
one is on. A setting that is on can always be switched off.
**Reset this page...** at the foot of a launcher page returns the settings
on that page to what a new installation has.

A change that cannot be written is named in red under the page's title, and
the setting stays as it was. If `settings.toml` cannot be read, the launcher's
pages say so and show no settings. Mend the file, or press **Start over with
defaults**: the file is kept beside it as `settings.unreadable.toml`.

### The launcher without a mouse

- **Tab** and the arrow keys move through a page. **Space** or **Enter**
  presses what the cursor is on. **Escape** goes back to the side bar.
- On the side bar, **Right** or **Space** opens a page and goes into it.
- **Ctrl+Tab** and **Ctrl+Shift+Tab** step through the pages. **F1** opens
  Help.
- **Enter** starts the game on the home page while no control has the cursor.
- A gamepad moves with the D-pad, presses with **A**, goes back with **B** and
  steps through the pages with **LB** and **RB**. **Start** starts the game on
  the home page.
- On Help and under What's new, **PageUp** and **PageDown**, or the gamepad's
  triggers, scroll the text.

### Display

- **Display mode**, **VSync** and **Internal resolution scale**, 1x to 8x.
  The default is 4x at 4:3. Lower the scale if the game runs slowly.
- **Aspect ratio**, 4:3 or wider. The main menu and its screens fill a wide
  screen: the backdrop is stretched, the logo and the text keep their shape.
  Movies, the map and loading screens stay 4:3.
- **HUD size** for widescreen, from 50% to 100%. The weapon in hand keeps its
  size. Below 100% the game's text is drawn a little bolder and gets a dark
  edge, so a small line stays readable. At 100% it is drawn as the game draws it.
- Shadows keep the game's own shape at 4:3 and lie flat on the floor in
  widescreen.

### Enhancements

The page is in five groups, in the launcher and in the game alike.

| Group | Settings | At first |
| --- | --- | --- |
| Frame rate | **60 FPS gameplay** | On |
| Frame rate | **In-between frames** and their rate | Off |
| Picture | **Exact-provenance geometry**, **Perspective-correct world textures**, **Gap filling**, **Smooth distance fade**, **Depth-sorted world** | On |
| Picture | **Fog shape** and **Draw distance** | Original |
| Picture | **Anti-aliasing (MSAA / SSAA)** | Off |
| Shading | **Ambient occlusion**, its reach and strength | Off |
| Movies | **Movie filter** | Smooth |
| Startup | **Skip the logos and the title movie** | Off |

- In-between frames, Ambient occlusion and the other Picture settings but
  Draw distance need exact geometry. Fog shape needs Smooth distance fade too.
- **Gap filling** closes small gaps between polygons.
- **Smooth distance fade** fades walls and floors evenly into the distance,
  where the game steps through twelve shades. Distant walls and floors fade in
  instead of appearing in blocks.
- **Fog shape** under it offers only the game's own fog, Original, in this
  release. The other shapes come in a later release. **Draw distance** offers
  Original and 1.25x, a quarter further.
- **Depth-sorted world** draws world polygons by their depth where the game's
  own order puts a far one over a near one.
- **Anti-aliasing (MSAA / SSAA)** smooths edges.
  - MSAA at 2x, 4x, 8x or 16x smooths the edges of polygons and of the HUD's
    rectangles.
  - SSAA at the same counts also smooths textures and cut-out outlines, the
    HUD's among them, and costs the graphics card that many times the shading.
    It needs OpenGL 4.0 or its sample shading extension. Without it the card
    draws MSAA of that count.
  - A card with fewer samples uses its most.
  - Video memory grows with the count and the internal resolution.
- **Ambient occlusion** shades corners and the floor under things.
- **Movie filter** sets how the game's movies are drawn at the size of your
  screen. Smooth takes away the square blocks the PlayStation's video
  compression leaves and the grain in flat areas. Original shows every pixel
  as a square, as before.
- **Skip the logos and the title movie** skips only the boot intro. The other
  movies play. Escape skips the logo or movie on screen while the settings
  menu is closed.

### Audio

- **Master volume** sets the loudness of everything the game plays.
- **Bass** sets how much deep bass all of the game's sound keeps: effects,
  music, voices and movies. Original plays it as the game does. Reduced
  softens the deepest rumble, under 80 Hz. TV speaker plays it as a
  television's own speakers did, with little under 160 Hz. The psionics and
  explosions carry most of their sound down there, and headphones and
  subwoofers play it in full.
- **Mute in the background** silences the game while its window is not in
  front. It is off at first.
- **Mute all audio** silences the game and keeps the volume.

## Other languages

The game can take its language from a second disc image you own: the
French, German or Japanese release. The US disc stays the game disc and is
still needed.

### Add a language disc

- **In the launcher:** press **Add disc...** under **Language**, or drop the
  image on the window. A CUE, BIN or CHD works. The launcher checks that it
  fits your US disc and uses it where it is. It is not copied, so keep it there.
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

## RetroAchievements

The game counts the [RetroAchievements](https://retroachievements.org) set
of Disruptor: 52 achievements for finishing levels, finding hidden areas and
clearing a whole map.

- Unlocks count as casual. RetroAchievements has not approved our client for
  hardcore yet.
- Any copy of the disc counts as the USA one, a language disc too.

### Sign in

1. Open the launcher's **Achievements** page.
2. Type your RetroAchievements name and password and press **Sign in**.
3. Start the game. It signs in by itself and shows how many you have.

The password goes to retroachievements.org once and is not kept. The game
keeps the token the site gives back instead, in `retroachievements.toml` in
the folder of your files. On Windows the token is sealed for your Windows
account. A problem report never carries it. **Sign out** on the same page,
or in the game's menu, forgets it.

### In the game

The **Achievements** tab of the game's menu, opened with Backquote, shows:

- whether you are signed in, with a link to your profile
- each achievement with its picture, points and description
- how far a counting one has come, as the kills and pickups of a level

An unlock appears in a corner of the screen with its picture and a chime.

| Setting | Default | What it changes |
| --- | --- | --- |
| Count achievements | On | Off plays without them, the account stays signed in |
| Show notifications | On | The popup of each unlock |
| Notification time | 12 seconds | 3, 5, 8, 12, 15 or 30 seconds |
| Notification corner | Top right | Any corner of the screen |
| Progress indicator | Off | A short note when a count moves |
| Unlock sound | On | The chime, at the game's volume |
| Sound file | The chime | A WAV of your own in place of the chime |
| Encore | Off | Achievements you have can trigger again |
| Unofficial achievements | On | Also the set's three unofficial ones, which the site does not record |

Encore and unofficial achievements take effect at the next start of the
game. The pictures are fetched once and kept in `achievements/badges/`.

**Sound file** under Unlock sound takes a WAV of your own: plain PCM or
32-bit float, mono or stereo, up to 10 seconds and 8 MB. The launcher
copies it to `achievements/unlock.wav` in the folder of your files, so the
original can move or go. It plays for every unlock and for a mastered set,
from the game's next start. **Default** brings the chime back. A file that
does not play leaves the chime, and the game's log says why.

### Save states and achievements

A save state keeps how far each achievement had counted, in a file beside
it. Loading the state puts the counts back, so a 100% achievement still
counts the kills before the save. The file fits that one state only. A state
made with RetroAchievements off has none, and loading it starts every count
over.

Save states work as before. In casual play they do not stop an unlock.

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
download and install it.

- **What's new** beside it, **Update options** and the version at the foot of
  the side bar open the **Updates** window.
- That window shows the release notes and this copy's version.
- **Check at start** there looks for a newer release each time the launcher
  opens. Switch it off to stop that.
- A portable copy is updated the same way, in its own folder, and only when
  you press the button.
- A copy unzipped from a ZIP of alpha.5 or older is updated by installing the
  new release with its Setup.exe.

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

## Reporting a problem

### Make a report

Press **Report a problem** on the launcher's **Troubleshooting** page. It
gathers into one ZIP:

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
3. Press **Report a problem** on the same page.

The log then also says every ten seconds how the frames ran and how the
sound queue stood, and records each setting changed in the game's menu,
controllers connected and removed, and what the graphics driver complains of.
It names the sound driver and device, and during a movie it writes once a
second where the sound and the picture are and how late the sound leaves. The switch is in the game's menu
too, under **Settings → Debug**. There the driver's part starts with the
next launch.

## Recordings and capsules

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
  launcher writes its CUE. A CHD has to hold the same one MODE2/2352 track,
  as chdman makes it of the dump's CUE.
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

This is an early version. The whole campaign can be played through. Small
geometry or texture faults can remain.
