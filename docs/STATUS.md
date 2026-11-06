# Project status

Last verified: **6 November 2026**, release 0.11.0. The project is in alpha.
The whole campaign can be played through.

| Area | Current state |
| --- | --- |
| Release | 0.11.0. Since 0.10.0 the game runs on PSXRecomp v0.3.4-alpha. That move touched almost every feature, so 0.10.x and 0.11.x may still break something that worked in 0.9.0. |
| Platform and disc | Windows x64 and Linux x64, an AppImage for glibc 2.35 and newer. Disruptor USA, SLUS-00224 only. Before each release the AppImage starts its launcher, and the game from power-on through the intro, under WSLg on Ubuntu 22.04. A level has not been played on Linux yet. |
| Gameplay | The whole campaign can be played through on Windows. 60 FPS gameplay is on at first. Doors, a ridden lift, enemies and shots move on every frame under it. |
| Rendering | OpenGL supports 1x–8x resolution and widescreen up to 32:9 or the window's shape, with 4x and 4:3 at first. Exact geometry, perspective textures, gap filling, a depth-sorted world and a smooth distance fade are on at first. Ambient occlusion, anti-aliasing (MSAA / SSAA) and sprites that face the camera are optional. Movies show all 320 columns, cleaned of their blocks and scaled smoothly under Movie filter. Minor edge gaps and localized texture wobble can remain. |
| Motion | 60 FPS gameplay, the render thread and smooth mouse turning are on at first. In-between frames start off and add frames up to 320 FPS. |
| Controls | Modern or Classic controls, a gamepad beside the keyboard with dual analog sticks, keys and gamepad buttons rebound in the launcher, and keyboard save naming. Vertical mouse look, with an Auto-aim option and a crosshair. Weapon and psionic wheels remain experimental. |
| Settings | One settings table for the launcher and the in-game menu. The lighting options stay closed in releases while they are tested. |
| Achievements | RetroAchievements sign-in in the launcher, with a choice of how unlocks show. |
| Languages | A French, German or Japanese disc of the player's own can give the game its language. USA remains the required game disc. |
| Save states | A save state loads in a build with the same generated code and memory layout, and in the language it was made in. 0.11.0 does not load the states of earlier versions. After an update the launcher tries each older state once and carries over those that load. Memory cards carry across every release, so keep progress there. |
| Automated checks | All CTests pass on Windows (MSVC), the source contracts among them, and on Ubuntu 22.04 (GCC 13). CI builds and tests the sources without the game on Linux (GCC) and Windows (MinGW-w64). |
| Release checks | Before a release goes out, a smoke test starts the packed Windows ZIP and the AppImage the way the launcher does. On Windows a recorded replay must stay in step. The update from the previous release is tried through the launcher. Only the files that passed are uploaded, after CI passed on their commit. |
| Player package | A Windows installer, a portable Windows ZIP and a Linux AppImage. Each holds the launcher, the game with its own enhancement package, OpenBIOS, the player's guide, the MIT license and dependency licenses. Each is signed with Sigstore. Players supply their supported BIN/CUE. |
| Launcher | Disc import and verification, updates through Velopack (by default a new release is looked for at start, and only what changed is downloaded when it can be), the player's guide on a Help page, and a problem report with an optional detailed log. |

What changed in each release is in the [changelog](../CHANGELOG.md).

## Remaining limits

- 0.10.x may still break features that worked in 0.9.0. Please
  [open an issue](https://github.com/RecompTeam/DisruptorRecomp/issues/new)
  for anything that works worse.
- A level has not been played on Linux yet.
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
- The widescreen sky and skyline are checked at 16:9, 21:9 and 32:9 in the
  first mission only.
- Vertical mouse look moves the horizon instead of turning the camera, about 31 degrees each way.
- 5x–7x resolution settings have automated coverage but have not been
  individually exercised live.
- Live volume adjustment and its relaunch persistence need user validation.

See [build details](BUILD.md) and [disc requirements](DISC.md) before
building or reporting a problem.
