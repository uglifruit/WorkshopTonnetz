# host_build.ps1 - shared host compile step for tests/ and tools/.
#
# Dot-source it, then:
#   $objs = Build-HostObjects -Root $root -OutDir <dir> -Extra <extra .cpp files>
# Returns the object file list, or $null on a compile error. Only files newer
# than their object are recompiled.

function Build-HostObjects {
    param([string]$Root, [string]$OutDir, $Extra)

    New-Item -ItemType Directory -Force $OutDir | Out-Null
    $common = @("-std=c++17", "-O2", "-DTONNETZ_HOST", "-I$Root\src", "-I$Root\tests", "-Wall", "-Wextra")
    $strict = $common + @("-Wdouble-promotion", "-Wfloat-conversion")

    $card = @()
    $card += Get-ChildItem "$Root\src\*.cpp" | Where-Object { $_.Name -ne "main.cpp" }
    $card += Get-ChildItem "$Root\src\engine\*.cpp"
    $card += Get-ChildItem "$Root\src\dsp\*.cpp" -ErrorAction SilentlyContinue

    $headers = Get-ChildItem "$Root\src" -Recurse -Include *.h
    $newestHeader = ($headers | Measure-Object -Property LastWriteTime -Maximum).Maximum

    $objs = @()
    $failed = $false
    foreach ($set in @(@{ Files = $card; Flags = $strict; Tag = "src" }, @{ Files = $Extra; Flags = $common; Tag = "ext" })) {
        foreach ($f in $set.Files) {
            $obj = Join-Path $OutDir ("{0}_{1}.o" -f $set.Tag, $f.BaseName)
            $stale = -not (Test-Path $obj)
            if (-not $stale) {
                $t = (Get-Item $obj).LastWriteTime
                $stale = ($f.LastWriteTime -gt $t) -or ($newestHeader -gt $t) -or ((Get-Item "$Root\tests\test.h").LastWriteTime -gt $t)
            }
            if ($stale) {
                & python -m ziglang c++ @($set.Flags) -c $f.FullName -o $obj
                if ($LASTEXITCODE -ne 0) { Write-Host "COMPILE FAILED: $($f.Name)"; $failed = $true }
            }
            $objs += $obj
        }
    }
    if ($failed) { return $null }
    return $objs
}
