# Builds the release and collects everything needed to run into dist\BusterBustsLoose
# (the ROM is not included - users supply their own copy). With -Zip it also makes
# the release zip (dist\BusterBustsLoose-<version>-win64.zip) holding only what a
# player needs: no settings, saves or screenshots.
param([switch]$Zip)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
cmake -S $root -B "$root\build" -G "Visual Studio 17 2022" -A x64 | Out-Null
cmake --build "$root\build" --config Release --target BusterBustsLoose
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$dist = Join-Path $root "dist\BusterBustsLoose"
# replace only the packaged files: settings (bbl.ini), saves and screenshots stay
foreach ($f in "BusterBustsLoose.exe", "SDL2.dll", "README.md", "readme.txt", "LICENSE.txt", "PUT_YOUR_ROM_HERE.txt", "licenses") {
  $p = Join-Path $dist $f
  if (Test-Path $p) { Remove-Item -Recurse -Force $p }
}
New-Item -ItemType Directory -Force $dist | Out-Null
New-Item -ItemType Directory -Force "$dist\licenses" | Out-Null

Copy-Item "$root\build\Release\BusterBustsLoose.exe" $dist
Copy-Item "$root\build\Release\SDL2.dll" $dist
Copy-Item "$root\README.md" $dist
Copy-Item "$root\readme.txt" $dist
Copy-Item "$root\LICENSE" "$dist\LICENSE.txt"
Copy-Item "$root\src\snes\LICENSE-LakeSnes.txt" "$dist\licenses\LakeSnes.txt"
Copy-Item "$root\third_party\SDL2-2.32.10\LICENSE.txt" "$dist\licenses\SDL2.txt"
Copy-Item "$root\licenses\CascadiaMono.txt" "$dist\licenses\CascadiaMono.txt"
Set-Content -Path "$dist\PUT_YOUR_ROM_HERE.txt" -Value @'
Copy your ROM of
    Tiny Toon Adventures - Buster Busts Loose! (USA).sfc
into this folder, then run BusterBustsLoose.exe.
'@
Write-Host "Packaged to $dist"

if ($Zip) {
  $version = (Select-String -Path "$root\src\version.h" -Pattern 'BBL_VERSION "([^"]+)"').Matches[0].Groups[1].Value
  $stage = Join-Path $root "dist\zip\BusterBustsLoose"
  if (Test-Path (Join-Path $root "dist\zip")) { Remove-Item -Recurse -Force (Join-Path $root "dist\zip") }
  New-Item -ItemType Directory -Force $stage | Out-Null
  foreach ($f in "BusterBustsLoose.exe", "SDL2.dll", "readme.txt", "LICENSE.txt", "PUT_YOUR_ROM_HERE.txt", "licenses") {
    Copy-Item -Recurse (Join-Path $dist $f) $stage
  }
  $zipPath = Join-Path $root "dist\BusterBustsLoose-$version-win64.zip"
  if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
  Compress-Archive -Path $stage -DestinationPath $zipPath
  Remove-Item -Recurse -Force (Join-Path $root "dist\zip")
  Write-Host "Release zip: $zipPath"
}
