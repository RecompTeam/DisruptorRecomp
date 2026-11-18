$ErrorActionPreference = 'Stop'
$version = '4.14.3'
$digest = 'c845c3d65be81954c75659145a522a793a27586bf940c5dcfe18af33fa1a67dd'
$name = "ccache-$version-windows-x86_64"
$archive = Join-Path $env:RUNNER_TEMP "$name.zip"
$tools = Join-Path $env:RUNNER_TEMP 'disruptor-tools'
Invoke-WebRequest "https://github.com/ccache/ccache/releases/download/v$version/$name.zip" -OutFile $archive
if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $digest) { throw 'ccache checksum mismatch' }
Expand-Archive -LiteralPath $archive -DestinationPath $tools
$bin = (Get-Item -LiteralPath (Join-Path $tools "$name/ccache.exe")).DirectoryName
[IO.File]::WriteAllText((Join-Path $bin 'python3'), "#!/bin/sh`n" + 'exec python.exe "$@"' + "`n")
if (!(Test-Path -LiteralPath 'C:/mingw64/bin/g++.exe')) { throw 'Runner MinGW-w64 compiler missing' }
Add-Content -LiteralPath $env:GITHUB_PATH -Value @($bin, 'C:\mingw64\bin')
