# Changelog

What changed in each release. The launcher shows the section of its own version on its home page.

## v0.5.0

Released on 2026-10-21.

### Changes

- Translate the game's code anew. The launcher carries save states of 0.4.x over
- Stop loading capsules of 0.4.x
- Run every setting the settings file does not name as the launcher shows it, also without the launcher
- Ship no settings file
- Call Fog shape's one choice Original instead of Even
- Run the game's processor eight times faster inside a frame with 60 FPS gameplay, instead of four

### Enhancements

- Offer Draw distance 1.25x again
- Take about 20% less processor time a frame for the emulation

### Bug fixes

- Show the far fogged shapes again under Smooth distance fade, up to where the game stops drawing
- Hold 60 FPS and higher in a big fight far out on an open level with 60 FPS gameplay

## v0.4.0

Released on 2026-10-19.

### Changes

- Move Fog shape and Draw distance to the Enhancements page and offer only Even and Original for now
- Keep about 45 ms of sound before the system instead of 180
- Drop the comparisons page, each release's notes carry its before and after pairs

### Enhancements

- Fade distant walls and floors in instead of drawing them in blocks, under Smooth distance fade
- Write the sound output and the movies' timing to the detailed log

### Bug fixes

- Slow down less when many enemies are on screen
- Start a movie's sound on the frame the French or German version starts it, with such a disc
- Let briefings play to the end with a French, German or Japanese disc
- Draw no dark lines along the held weapon with multisampling

## v0.3.1

Released on 2026-10-17.

### Changes

- Show the launcher's last message in the banner beside Play
- Drop Play Disruptor.cmd from the portable ZIP, DisruptorLauncher.exe starts the game
- Drop the -alpha label from version numbers

### Enhancements

- Add a Report a problem button to the launcher's Troubleshooting page

### Bug fixes

- Keep the Linux AppImage's memory cards in ~/.local/share/DisruptorRecompData/saves, so the game can save
- Draw the textures of mission 1 right with a French or German disc, and of every level that disc has pictures of its own for
- Draw the drones' shadows right with a French or German disc
- Put the pause menu's quit question and its answers where the French or German version puts them

## v0.3.0-alpha.1

Released on 2026-10-17.

### Changes

- Pick weapons with the number keys only when Number keys pick weapons is on, off at first, in either scheme
- Keep a baton picked by a number key or on the wheel in hand, where the game would take a gun with ammunition
- Mark the number keys and the weapon and psionic wheels experimental

### Bug fixes

- Keep the psionic wheel shut until the first psionic is found
- Stop the mouse turning the view while the game is paused or one of its lists is open
- Pick the weapon of a number key pressed during a weapon change once the change ends, instead of opening the one before again
- Keep a pickup or a body under a shut door behind the door
- Keep a drone under the stairs behind the floor in front of it
- Keep the sky in a window while the view turns with in-between frames
- Stop a dark rim hanging in the air beside a column in in-between frames

## v0.2.0-alpha.1

Released on 2026-10-16.

### Changes

- Move fast-forward from ] to B, which does nothing on the save-name screen
- Turn on 60 FPS gameplay and smooth mouse turning for a new installation, while a settings file that names them keeps its choice
- Turn on the smooth distance fade and the depth-sorted world for a new installation
- Drop the shadow setting, as shadows now follow the picture's shape by themselves

### Enhancements

- Install the game with `DisruptorRecomp-win-Setup.exe`, or play from `DisruptorRecomp-win-Portable.zip` without installing
- Update from the launcher: by default it looks for a new release at every start, downloads only what changed when it can and installs it with Restart and update
- Keep saves, settings and save states outside the program folder, so an update keeps them
- Copy the saves of an older copy with Bring saves
- Carry the save states of an earlier build over after a trial run of each
- Add `DisruptorRecomp.AppImage` for Linux x64 with glibc 2.35 or newer, updated in place
- Show art from your own disc, the disc's state and this version's notes on the launcher's home page
- Show the player's guide on the launcher's Help page
- Reset one settings page at a time
- Choose Modern or Classic controls
- Move each game action to another key or gamepad button on the launcher's Controls page
- Play with a gamepad beside the keyboard and mouse, with an adjustable stick dead zone
- Add experimental anti-aliasing: MSAA for polygon edges, SSAA for textures and cut-out outlines too
- Move doors and a ridden lift on every frame under 60 FPS gameplay
- Let in-between frames reach 320 FPS on any display
- List replay capsules with their pictures in the Debug tab, record one with Ctrl+F12 and load the newest with F12
- Add a texture coordinates view to the Debug tab
- Give the game's window the launcher's icon

### Bug fixes

- Turn the sky backdrop with the world under smooth mouse turning
- Turn bodies and pickups with the world under smooth mouse turning
- Keep a far corner from being thrown across the screen under smooth mouse turning
- Draw a wall beside the player whole, without holes or smears
- Keep bodies whole over the floor they lie on
- Stop world textures jumping a texel as the camera moves
- Show all 320 columns of movies and logos
- Keep a level's end wide in widescreen while the game fades its last frame
- Keep sprites drawn to the edges of a wide picture
- Let gap filling fill only empty space
- Keep a state loaded mid-run from freezing the game
- Keep a number key bound to an action from also picking a weapon
- Open a wheel only on the key its choice is bound to
- Step through a wheel with the Up and Down arrows in the Modern scheme while they are bound to no action

## v0.1.0-alpha.5

Released on 2026-10-13.

### Enhancements

- Choose a weapon with the number keys under modern controls, on the number row or the keypad
- Add weapon and psionic wheels, off by default: hold the selector, point with the mouse or the left stick and release to equip
- Count both of the game's ammo pools on the wheels, live, with no number for special weapons, as in the game
- Record wheel selections in replay capsules
- Capture the mouse while the game window has focus, with no click
- Share the setting descriptions and dependency rules between the launcher and the game, with a hint over each in-game setting
- Gather logs and settings for a bug report with Report a problem in the launcher
- Add an optional detailed log on the Troubleshooting page

### Bug fixes

- Keep the held weapon at its size and place in widescreen
- Order in-between frames by the depth their corners have there
- Capture and resume save states at checked points of the game's code
- Keep letter and number keys typed into a save's name from pressing controller buttons
- Close an open wheel when a state is loaded
- Name Windows 11 in a report

## v0.1.0-alpha.4

Released on 2026-10-12.

### Enhancements

- Fill a wide screen with the front-end menus, the logo and text keeping their shape
- Type a save's name on the keyboard
- Add smooth mouse turning, off by default, which turns the picture between the game's 256 heading steps and needs captured mouse aim and exact geometry
- Add gap filling, an optional depth-sorted world and experimental ambient occlusion, all needing exact geometry
- Record a replay capsule with Ctrl+F12: a save state, the input that follows it, the settings and the build's details
- Replay a capsule only in the build that recorded it, and record none in a network game
- Add a Debug tab with diagnostic views and a short screen recording
- Scroll long settings pages in the launcher, with a hint over each option
- Set the experimental renderer options in the launcher before the game starts
- Restore settings an older settings file lacks when Play starts
- Give the language its own tab in the game's settings

### Bug fixes

- Draw loading screens, the pause menu's text and the order of world sprites correctly in widescreen
- Keep borderless fullscreen borderless on Windows
- Take the mouse back when the window regains focus
- Resume loaded states with their saved CPU timing, also without a window
- Handle paths with letters outside ASCII
- Wrap long notes in the game's settings
- Label the 60 FPS and in-between frame options more clearly

## v0.1.0-alpha.3

Released on 2026-10-10.

### Enhancements

- Add a Windows launcher that copies your USA disc image into place, writes its CUE, verifies its whole SHA-256 and starts the game
- Keep your original disc image where it is
- Verify the installed disc at every later start
- Change Display, Enhancements, Controls and Audio settings in the launcher before playing
- Add Original and Enhanced presets, a reset, the language choice and the start-up log's errors to the launcher
- Add French, German and Japanese content from a second disc image of yours, while the USA disc SLUS-00224 stays the game disc
- Take movies, speech, menus, messages and mission hints from a French or German disc
- Take movies, speech and hints from a Japanese disc, with English menus as on that disc
- Leave a language disc image where it is
- Keep separate save state slots for each language
- Add a skip of the boot logos and the title movie, off by default
- Skip a logo or a movie with Escape

### Bug fixes

- Retry a settings save through a brief file lock without damaging the file
- Keep the weapon and psionic lists and the map whole in widescreen
- Close holes along near walls in widescreen
- Fix the noise around movies with the OpenGL renderer

## v0.1.0-alpha.2

Released on 2026-10-08.

### Enhancements

- Add optional 60 FPS gameplay, off by default
- Add optional in-between frames with their rate, off by default and needing exact geometry
- Add a HUD size for widescreen, from 50% to 100%, which leaves the weapon at its size
- Add a choice of original or improved shadows when geometry correction is on

### Bug fixes

- Keep the true projection of near-wall corners
- Match in-between frames better and place sprites between whole pixels in them
- Keep the widescreen sky's proportions with extra tile columns
- Keep enemy shadows at their width while the player moves

## v0.1.0-alpha.1

Released on 2026-10-06.

### Enhancements

- Release the first Windows x64 alpha with the in-game settings menu, 1x to 8x rendering, modern mouse and keyboard controls and geometry and perspective correction
- Leave frame interpolation out of this release

### Bug fixes

- Stop ultrawide texture corruption and freezes
