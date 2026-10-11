# Project status

Last verified: **12 October 2026**. The project is in development; full campaign
validation is incomplete.

| Area | Current state |
| --- | --- |
| Platform and disc | Windows x64; Disruptor USA, SLUS-00224 only. Linux is a development target. |
| Gameplay | Boot, menus, FMVs, audio, and the first mission have been exercised on Windows. |
| Rendering | OpenGL supports 1x–8x resolution, widescreen, geometry correction, and perspective textures. Minor edge gaps and localized texture wobble can remain. |
| Controls | Modern keyboard/mouse controls, optional smooth mouse turning, and keyboard save naming are available. Vertical look and weapon aim remain experimental. |
| Settings | Controls, Enhancements, Experimental, Language, System, Debug, and Cheats tabs; supported preferences persist between runs. Launcher settings scroll and offer hover hints. |
| Interpolation and diagnostics | In-between frames start off. The Debug tab offers diagnostic views and screen recording; Ctrl+F12 records replay capsules. |
| Ultrawide corruption | Fixed primitive-buffer overflow. The user's 8x / 32:9 training retest stayed correct through 8,934 frames and closed normally. |
| Save states | Save states require a matching build and language. The alpha.3 code-generation changes make alpha.1 and alpha.2 states incompatible; use memory-card saves for progress across releases. |
| Automated checks | Alpha.4 Windows x64 Release build, code-generation audit, and all 77 root CTests passed. |
| Player package | Windows x64 alpha includes the launcher, OpenBIOS, setup guide, and dependency licenses; players supply their supported BIN/CUE. |
| Launcher and languages | Disc import and verification, saved settings, presets, and optional French/German/Japanese content from a second owned disc. USA remains the required game disc. |
| Intro and widescreen fixes | Optional intro skipping, complete widescreen map and selection lists, reduced movie-edge noise, and near-wall hole fixes. |
| Recent fixes and options | Widescreen front-end menus, loading-screen and pause-text fixes, borderless fullscreen and mouse-focus fixes, improved state resumption, gap filling, optional depth sorting, and experimental ambient occlusion. |

## Remaining limits

- Full campaign, later levels, and broad memory-card regression coverage remain
  unverified.
- The source build requires local generation from the verified game executable.
  Prebuilt release users only need their supported BIN/CUE disc image.
- Gameplay defaults to the retail cadence of about 30 unique world/camera
  frames per second. Experimental 60 FPS gameplay removes that floor, and
  in-between frames add presentation frames. Higher render resolution alone
  does not increase the game cadence.
- The widescreen sky and skyline backdrop draws extra tile columns to keep its
  proportions. Checked at 16:9, 21:9, and 32:9 in the first mission only.
- Vertical aim needs broader actor, room-edge, and level-transition testing.
- 5x–7x resolution settings have automated coverage but have not been
  individually exercised live.
- Live volume adjustment and its relaunch persistence need user validation.

See [build details](BUILD.md) and [disc requirements](DISC.md) before
building or reporting a problem.
