<p align="center">
  <img src="docs/images/hero.png" alt="Disruptor Recompiled and its launcher" width="100%">
</p>

<p align="center">
  A native PC recompilation of <b>Disruptor</b> (PlayStation, 1996). It plays from your own disc.
</p>

<p align="center">
  <a href="https://github.com/RecompTeam/DisruptorRecomp/releases"><img src="https://img.shields.io/github/v/release/RecompTeam/DisruptorRecomp?include_prereleases&label=release" alt="Release"></a>
  <a href="https://github.com/RecompTeam/DisruptorRecomp/releases"><img src="https://img.shields.io/github/downloads/RecompTeam/DisruptorRecomp/total" alt="Downloads"></a>
  <a href="https://github.com/RecompTeam/DisruptorRecomp/releases"><img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20x64-1a231b" alt="Windows and Linux x64"></a>
  <a href="https://github.com/RecompTeam/DisruptorRecomp/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow/status/RecompTeam/DisruptorRecomp/build.yml?label=build" alt="Build"></a>
  <a href="https://scorecard.dev/viewer/?uri=github.com/RecompTeam/DisruptorRecomp"><img src="https://img.shields.io/ossf-scorecard/github.com/RecompTeam/DisruptorRecomp?label=OpenSSF%20Scorecard" alt="OpenSSF Scorecard"></a>
  <a href="https://discord.gg/hWbFatkXpU"><img src="https://img.shields.io/badge/Discord-join-5865F2" alt="Discord"></a>
</p>

<p align="center">
  <a href="https://github.com/RecompTeam/DisruptorRecomp/releases/tag/v0.2.0-alpha.1"><b>Download</b></a>
</p>

## Quick start

1. Download from [Releases](https://github.com/RecompTeam/DisruptorRecomp/releases):
    - **Windows:** run [**DisruptorRecomp-win-Setup.exe**](https://github.com/RecompTeam/DisruptorRecomp/releases/download/v0.2.0-alpha.1/DisruptorRecomp-win-Setup.exe).
    - **Windows, without installing:** unpack [**DisruptorRecomp-win-Portable.zip**](https://github.com/RecompTeam/DisruptorRecomp/releases/download/v0.2.0-alpha.1/DisruptorRecomp-win-Portable.zip) and run `Disruptor Recompiled.exe`.
    - **Linux x64:** download [**DisruptorRecomp.AppImage**](https://github.com/RecompTeam/DisruptorRecomp/releases/download/v0.2.0-alpha.1/DisruptorRecomp.AppImage), make it executable and run it:\
      `chmod +x DisruptorRecomp.AppImage`
2. Drop your **Disruptor (USA, SLUS-00224)** disc image on the launcher it opens.
3. Press **Play**.

**No game data is included.**\
A French, German or Japanese disc of yours can give the game its language.

<p align="center">
  <a href="docs/COMPARISONS.md"><b>Before and after</b></a><br>
  What changed from the PlayStation version, side by side.
</p>

## Launcher

Drop the disc and press Play. The image is checked byte for byte.\
Updates install with one click. Saves and settings stay.

<table>
  <tr>
    <td width="50%" valign="top">
      <img src="docs/images/display.png" alt="The Display page" width="100%"><br>
      <b>Display</b><br>
      Window or fullscreen, aspect ratio, resolution scale and VSync.
    </td>
    <td width="50%" valign="top">
      <img src="docs/images/enhancements.png" alt="The Enhancements page" width="100%"><br>
      <b>Enhancements</b><br>
      60 FPS gameplay, in-between frames, exact geometry and perspective-correct textures.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <img src="docs/images/controls.png" alt="The Controls page" width="100%"><br>
      <b>Controls</b><br>
      Modern or Classic, weapon and psionic wheels, mouse aim and a gamepad beside the keyboard.
    </td>
    <td valign="top">
      <img src="docs/images/keys.png" alt="The key and gamepad tables" width="100%"><br>
      <b>Keys and buttons</b><br>
      Each game action can be moved to another key or gamepad button, apart from the game's fixed keys. Click it, press the new one.
    </td>
  </tr>
</table>

## Settings

| Setting | Default | What it changes |
| --- | --- | --- |
| Internal resolution scale | 4x | Up to 8x the original resolution |
| Aspect ratio | 4:3 | 16:9, 21:9, 32:9 or the window's shape |
| Exact-provenance geometry | On | Steadies the polygon edges that wobble on the PlayStation |
| Perspective-correct world textures | On | Textures keep their shape near the viewer |
| Gap filling | On | Closes hairline gaps between polygons |
| Depth-sorted world | On | Draws a near polygon over a far one where the game's own order gets it wrong |
| Smooth distance fade | On | Walls fade evenly instead of in twelve steps |
| Ambient occlusion | Off | Shaded corners and floors under things |
| 60 FPS gameplay | On | The game runs at up to 60 FPS instead of 30 |
| In-between frames | Off | Adds frames for smoother motion, up to 320 FPS |
| Smooth mouse turning | On | The view turns between the game's 256 heading steps |
| Control scheme | Modern | WASD and the mouse, or the original keys |
| Weapon and psionic wheels | Off | Choose a weapon or a psionic on a wheel instead of the game's list |
| Anti-aliasing (MSAA / SSAA) | Off | Smoother edges, experimental |

<details>
<summary><b>Build from source</b></summary>

Git, Python 3.11+, CMake 3.24+ and Visual Studio's **Desktop development with C++**, and your disc's files in `input/`
([DISC.md](docs/DISC.md)). From an x64 developer PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
.\run.ps1
```

[BUILD.md](docs/BUILD.md) has Linux, the generated code and the tests.
</details>

<p align="center">
  <a href="docs/PLAYING.md">Player's guide</a> ·
  <a href="docs/COMPARISONS.md">Before and after</a> ·
  <a href="docs/DISC.md">Supported disc</a> ·
  <a href="CHANGELOG.md">Changelog</a> ·
  <a href="docs/STATUS.md">Status</a> ·
  <a href="docs/BUILD.md">Build from source</a> ·
  <a href="CONTRIBUTING.md">Contributing</a>
</p>
