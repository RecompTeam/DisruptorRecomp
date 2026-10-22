# Project status

Last verified: **16 October 2026**. The project is in development. The whole
campaign can be played through.

| Area | Current state |
| --- | --- |
| Platform and disc | Windows x64 and Linux x64 (an AppImage for glibc 2.35 and newer); Disruptor USA, SLUS-00224 only. On Linux the build and its tests pass. The AppImage's launcher imports the disc and starts the game, tested under WSLg on Ubuntu 22.04 up to the save-name screen. A level has not been played there yet. |
| Gameplay | Boot, menus, FMVs, audio, and the first mission have been exercised on Windows. 60 FPS gameplay is on at first. Doors, a ridden lift, enemies and shots move on every frame under it. |
| Rendering | OpenGL supports 1x–8x resolution and widescreen up to 32:9, with 4x and 4:3 at first. Exact geometry, perspective textures, gap filling, a depth-sorted world and a smooth distance fade are on at first. Ambient occlusion and anti-aliasing (MSAA / SSAA) are optional. Movies show all 320 columns, cleaned of their blocks and scaled smoothly under Movie filter (OpenGL, Vulkan scales them bilinear). Minor edge gaps and localized texture wobble can remain. |
| Controls | Modern or Classic controls, smooth mouse turning (on at first), a gamepad beside the keyboard, keys and gamepad buttons rebound in the launcher, and keyboard save naming. Vertical look and weapon aim remain experimental. |
| Settings | One settings table for the launcher (Display, Enhancements, Controls, Audio, Experimental, Troubleshooting) and the in-game menu (Display, Enhancements, Controls, Experimental, Language, System, Debug, Cheats). Each launcher settings page except Troubleshooting resets on its own. |
| Interpolation and diagnostics | In-between frames start off. The Debug tab offers diagnostic views and screen recording; Ctrl+F12 records replay capsules and F12 loads the newest. |
| Ultrawide corruption | Fixed primitive-buffer overflow. The user's 8x / 32:9 training retest stayed correct through 8,934 frames and closed normally. |
| Save states | A save state loads in a build with the same generated code and in the language it was made in. After an update the launcher tries each state of the earlier build once and carries over those that load. The alpha.3 code-generation changes make alpha.1 and alpha.2 states incompatible; use memory-card saves for progress across releases. |
| Automated checks | All 110 root CTests pass on Windows (MSVC), the source contracts among them. The Linux builds (GCC 15, and GCC 13 on Ubuntu 22.04) passed all 108 before the game config test was added. |
| Player package | A Windows installer, a portable Windows ZIP and a Linux AppImage. Each holds the launcher, OpenBIOS, the player's guide, the MIT license and dependency licenses; players supply their supported BIN/CUE. |
| Launcher | Disc import and verification, updates through Velopack (by default a new release is looked for at start, and only what changed is downloaded when it can be), the player's guide on a Help page, a problem report with an optional detailed log, and optional French/German/Japanese content from a second owned disc. USA remains the required game disc. |
| Intro and widescreen fixes | Optional intro skipping, complete widescreen map and selection lists, reduced movie-edge noise, and near-wall hole fixes. |
| Recent fixes and options | Widescreen front-end menus, loading-screen and pause-text fixes, borderless fullscreen and mouse-focus fixes, improved state resumption, bodies whole over the floor they lie on, walls beside the player drawn whole, world textures no longer jumping a texel, the sky and sprites turning with smooth mouse turning, and movies at their full width. |

## Remaining limits

- Broad memory-card regression coverage remains unverified.
- The source build requires local generation from the verified game executable.
  Prebuilt release users only need their supported BIN/CUE disc image.
- 60 FPS gameplay is on at first. Off, the game keeps the retail cadence of
  about 30 unique world/camera frames per second. In-between frames add
  presentation frames. Higher render resolution alone does not increase the
  game cadence.
- The game steps doors, lifts, enemies and shots on every second tick. Under
  60 FPS gameplay a door is drawn half a step ahead on the frames in between.
  A lift, an enemy and a shot are drawn half a step behind on the frames they
  step, a lift with the view of a player it carries. So all of them move on
  every frame. The other things the game updates at that rate still step at 30.
- Enemies and bodies sort a little nearer than the game sorts them, together
  with the items lying over them, so the floor under them does not cut them.
  A wall or a shut door standing in front of one keeps it behind, also where
  the game itself drew a pickup lying under a shut door over the door.
- The widescreen sky and skyline backdrop draws extra tile columns to keep its
  proportions. Checked at 16:9, 21:9, and 32:9 in the first mission only.
- Vertical aim needs broader actor, room-edge, and level-transition testing.
- 5x–7x resolution settings have automated coverage but have not been
  individually exercised live.
- Live volume adjustment and its relaunch persistence need user validation.

See [build details](BUILD.md) and [disc requirements](DISC.md) before
building or reporting a problem.
