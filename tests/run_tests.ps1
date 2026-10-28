# run_tests.ps1 - build and run the host test suite.
#
#   powershell -File tests/run_tests.ps1            all tests
#   powershell -File tests/run_tests.ps1 tonal      only tests whose name contains "tonal"
#
# Compiles the REAL engine and DSP sources (src/engine, src/dsp) with zig c++
# (python -m pip install --user ziglang) - no Windows SDK needed.
#
# Card sources get the firmware's float warnings (-Wdouble-promotion
# -Wfloat-conversion) so a stray double is caught here before it reaches the
# RP2040; test sources do not, since they printf floats freely.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "host_build.ps1")

$objs = Build-HostObjects -Root $root -OutDir (Join-Path $root "tests\build") -Extra (Get-ChildItem "$root\tests\*.cpp")
if ($null -eq $objs) { exit 1 }

$exe = Join-Path $root "tests\build\tonnetz_tests.exe"
& python -m ziglang c++ @objs -o $exe
if ($LASTEXITCODE -ne 0) { Write-Host "LINK FAILED"; exit 1 }

& $exe @args
exit $LASTEXITCODE
