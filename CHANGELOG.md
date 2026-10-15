# Changelog

What changed in each release. The launcher shows the section of its own version on its home page.

## v0.1.0-alpha.5

- **Number-key weapon selection.** With modern controls enabled, press **1** for Baton, **2** for 18mm Semi, **3** for 18mm Auto, **4** for Phase Rifle, **5** for Phase Repeater, **6** for Lockon Cannon, **7** for AM Blaster, **8** for AM Cyclone, **9** for Zodiac, and **0** for Plasma Lance. Both the number row and keypad work. Selection requires owning the weapon; menus and save-name entry keep their usual input.
- **Weapon and psionic wheels.** One **Weapon and psionic wheels** toggle in **Controls** enables both selectors in the launcher and in-game settings. It starts **off**. Hold the existing selector binding (**Q / R** with modern controls, or the mapped controller buttons), point with the mouse or left stick, and release to equip. Arrow keys and the D-pad cycle through owned items. Left-click or Enter confirms; Escape, right-click, or the controller cancel button cancels. Gameplay continues while the wheel is open. Turning the option off restores the original selection lists.
- **Correct wheel ammo counts.** Counts include both of the game's ammo pools and update live, including the shared pools used by pistols and AM weapons. Special weapons follow the game's behavior and omit a numeric ammo counter. Wheel selections are recorded in replay capsules.
- **Mouse capture and widescreen fixes.** Mouse aim captures the pointer automatically while the game window has focus. Held weapons keep their correct size and position in widescreen. In-between frames order geometry using its interpolated depth.
- **Clearer settings.** The launcher and game share setting descriptions and dependency rules. Hovering over an in-game setting shows its explanation, and options stay disabled when their required settings are off.
- **Bug reports.** **Report a problem** in the launcher collects logs and settings. Its **Troubleshooting** page offers optional detailed logging for reproduction runs. Reports identify Windows 11 correctly.
- **Save and input fixes.** Save states are captured and resumed at checked execution points. Typing a save name keeps letter and number keys from also pressing mapped controller buttons. Loading a state cancels an open wheel.

## v0.1.0-alpha.4

- **Widescreen menus and fixes.** Front-end menus fill a wide screen while the logo and text keep their shape. Loading screens, pause-menu text, and the ordering of world sprites have been corrected.
- **Keyboard save names.** Type letters, digits, and spaces on the game's save-name screen. Backspace and Delete edit the name; Enter moves to DONE.
- **Smooth mouse turning.** An optional setting in Controls turns the picture between the game's 256 heading steps. It requires captured mouse aim and exact geometry.
- **Renderer options.** Gap filling closes small gaps between polygons. Optional depth sorting changes how world polygons overlap, and experimental ambient occlusion adds adjustable shading in corners and under objects. These options require exact geometry.
- **Replay capsules and reporting tools.** Ctrl+F12 starts and finishes a capsule containing a save state, input, settings, and build details. The new Debug tab offers diagnostic views and a short screen recording for rendering reports. Capsules require the build that recorded them and are not recorded in network games; see the [replay instructions](https://github.com/micmea668/DisruptorRecomp/blob/v0.1.0-alpha.4/docs/BUILD.md#replay-capsules) for playback and its limits.
- **Launcher improvements.** Long settings pages scroll, hovering over an option shows its explanation, and experimental renderer options are available before starting the game. Play restores settings omitted from an older settings file. Paths containing non-ASCII characters are handled correctly.
- **Window and state fixes.** Borderless fullscreen stays borderless on Windows, mouse capture returns when the window regains focus, and loaded states resume with their saved CPU timing settings, including in headless mode.
- **Language and settings layout.** The in-game language controls have their own tab. Long explanatory notes wrap, and the 60 FPS and in-between-frame options have clearer labels.

## v0.1.0-alpha.3

- **Native Windows launcher.** Browse to or drop your supported USA disc image. The launcher copies it into place, creates its CUE, verifies the complete SHA-256, and starts the game. Your original dump is kept. Later launches verify the installed disc before booting.
- **Settings in the launcher.** Display, Enhancements, Controls, and Audio share the game's saved settings. Original and Enhanced presets, reset, language selection, and startup-log errors are available before launching.
- **More reliable settings saves on Windows.** Brief file locks are retried without replacing or damaging the previous settings when a save fails.
- **Optional French, German, and Japanese content.** Add a second disc image you own to choose its language. The USA SLUS-00224 disc remains required as the game disc. French and German discs supply movies, speech, menus, messages, and mission-reload hints; Japanese supplies movies, speech, and hints, with English menus as on that disc. Language disc images stay in their original location. Save-state slots are separate for each language.
- **Intro skipping.** An optional setting skips the boot logos and title movie. Escape can skip the current logo or movie. The default keeps the original intro.
- **Widescreen and rendering fixes.** Weapon/psionic selection lists and map frames remain whole in widescreen. Near-wall holes and noise around movie edges have been addressed.

## v0.1.0-alpha.2

- **Optional 60 FPS gameplay.** Enable it under **Settings → Enhancements**. It starts off, preserving the original gameplay cadence by default.
- **Optional in-between frames.** The frame interpolator is included again, with a saved enable switch and rate control. It starts off and requires exact geometry. Frame matching and sub-pixel sprite placement have been improved.
- **Improved near-wall geometry and shadows.** Guard-band corners keep their true projection. Choose **Vanilla** or **Improved** shadows in Enhancements when geometry correction is enabled.
- **Widescreen fixes.** The sky backdrop uses extra tile columns to keep its proportions, and enemy shadows retain the correct width while the player moves.
- **HUD size for widescreen.** Adjust health and ammunition displays from 50% to 100%; the weapon keeps its size.

## v0.1.0-alpha.1

First public **Windows x64 alpha** of DisruptorRecomp. This release includes the in-game settings menu, 1x–8x rendering, modern mouse/keyboard controls, geometry/perspective correction, and the fix for ultrawide texture corruption and freezes. Frame interpolation is disabled.
