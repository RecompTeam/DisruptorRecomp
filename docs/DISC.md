# Supported disc revision

Only **Disruptor (USA), SLUS-00224, NTSC-U** is supported. Supply your own disc
files; matching the serial alone is insufficient.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `SLUS_002.24` | 401,408 bytes | `48e8c3143b7f5de10340c9d4a9bac8cb7e97c15eda7a0897d3cf337ad96cb2c4` |
| `Disruptor (USA).bin` | 636,350,064 bytes | `3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4` |

The disc has one **MODE2/2352** data track starting at `00:00:00`, with 270,557
raw sectors. Keep the BIN/CUE format: converting to a cooked 2048-byte ISO loses
information needed for PlayStation audio and video.

Windows builds include `DisruptorLauncher.exe`, which accepts this exact raw
image as BIN, CUE, ISO, or IMG, verifies its full SHA-256, and installs a
normalized BIN/CUE in `input/discs/SLUS-00224/`. The extension alone does not
make an image compatible: cooked ISOs are rejected. Only one MODE2/2352 track
at INDEX 01 `00:00:00` is supported. No loose executable extraction is needed
for players using the launcher.

For source builds, place the executable, `Disruptor (USA).cue`, and its referenced BIN in `input/`.
The CUE's `FILE` entry must match the BIN filename. Verify the supplied files in
PowerShell with:

```powershell
Get-FileHash .\input\SLUS_002.24 -Algorithm SHA256
Get-FileHash '.\input\Disruptor (USA).bin' -Algorithm SHA256
```

The build creates a local code-only analysis image from the verified executable
without modifying the original. Disc files and all generated retail code stay
outside version control.
