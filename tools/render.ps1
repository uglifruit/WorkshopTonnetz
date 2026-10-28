# render.ps1 - build tools/render and render a set of reference WAVs.
#
#   powershell -File tools/render.ps1                 the standard set (below)
#   powershell -File tools/render.ps1 my.wav 60 0.8   one render, args as render.cpp
#
# WAVs land in tools/out/ (gitignored). LEFT = Audio Out 1 (chord),
# RIGHT = Audio Out 2 (melody).

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root "tests\host_build.ps1")

$objs = Build-HostObjects -Root $root -OutDir (Join-Path $root "tools\build") -Extra (Get-ChildItem "$root\tools\render.cpp")
if ($null -eq $objs) { exit 1 }
$exe = Join-Path $root "tools\build\render.exe"
& python -m ziglang c++ @objs -o $exe
if ($LASTEXITCODE -ne 0) { Write-Host "LINK FAILED"; exit 1 }

$out = Join-Path $root "tools\out"
New-Item -ItemType Directory -Force $out | Out-Null

if ($args.Count -gt 0) {
    $a = @($args)
    $a[0] = Join-Path $out $a[0]
    & $exe @a
    exit $LASTEXITCODE
}

#          name                 secs wander glide antic density seed
$set = @(
    @("conservative.wav",       180, 0.05,  0.35, 0.5,  0.55,   1),
    @("middle.wav",             180, 0.40,  0.45, 0.6,  0.55,   2),
    @("adventurous.wav",        180, 0.95,  0.45, 0.6,  0.55,   3),
    @("morphing-drone.wav",     180, 0.50,  1.00, 0.5,  0.30,   4),
    @("sharp-changes.wav",      120, 0.40,  0.00, 0.8,  0.75,   5)
)
foreach ($s in $set) {
    & $exe (Join-Path $out $s[0]) $s[1] $s[2] $s[3] $s[4] $s[5] $s[6]
    Write-Host ""
}
