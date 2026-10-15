# Third-party notices

`framework/` contains patches to, and files written for, PSXRecomp revision
`854a658372b304c222a0d0cbbae16c66264b785e`. PSXRecomp is copyright Matthew
Stan and is provided under the PolyForm Noncommercial License 1.0.0. The
upstream license is reproduced in
[`licenses/PSXRecomp-LICENSE.txt`](licenses/PSXRecomp-LICENSE.txt).

The build downloads the pinned upstream framework and its dependencies. Their
own notices remain in that checkout and resulting local build. No third-party
binaries are stored in this repository.

The optional in-game settings menu builds against Dear ImGui
1.92.6, copyright Omar Cornut and Dear ImGui contributors, under the MIT
License. The source archive is integrity-pinned in `CMakeLists.txt`; its license
is reproduced in [`licenses/Dear-ImGui-LICENSE.txt`](licenses/Dear-ImGui-LICENSE.txt).

The Windows launcher built with MSVC links the C library of Velopack 1.2.161,
copyright Caelan Sayler and Velopack Ltd., under the MIT License. The archive is
integrity-pinned in `src/launcher/CMakeLists.txt`; its license is reproduced in
[`licenses/Velopack-LICENSE.txt`](licenses/Velopack-LICENSE.txt).

The Windows launcher draws its text with FreeType 2.14.3, under the FreeType
License. Portions of this software are copyright © 2026 The FreeType Project
(https://freetype.org). All rights reserved. The archive is integrity-pinned in
`src/launcher/CMakeLists.txt`. Its license is reproduced in
[`licenses/FreeType-LICENSE.txt`](licenses/FreeType-LICENSE.txt).

The launcher carries cut-down copies of the Inter typeface 4.1, copyright The
Inter Project Authors, under the SIL Open Font License 1.1, and of 30 Lucide
icons 1.49.0, copyright Lucide Icons and Contributors, under the ISC License.
They are in `src/launcher/assets/`; their licenses are reproduced in
[`licenses/Inter-LICENSE.txt`](licenses/Inter-LICENSE.txt) and
[`licenses/Lucide-LICENSE.txt`](licenses/Lucide-LICENSE.txt).
