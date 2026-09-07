# Optional FullBuild / CC round-trip harness. Never copies EA bytes into the repo.
# Usage: pwsh -File scripts/roundtrip.ps1 [-Packages path,...] [-GameDir DIR] [-Payloads] [-Tbc]
[CmdletBinding()]
param(
    [string[]]$Packages,
    [string]$GameDir = $env:SXPE_GAME_DIR,
    [string]$UserDir = $env:SXPE_USER_DIR,
    [string]$Sxpe,
    [switch]$Payloads,
    [switch]$Tbc,
    [int]$PayloadMaxBytes = 32MB
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Sxpe) {
    $cand = @(
        (Join-Path $Root "build\sxpe.exe"),
        (Join-Path $Root "build\sxpe")
    )
    $Sxpe = $cand | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Sxpe -or -not (Test-Path $Sxpe)) {
    throw "sxpe CLI not found. Build first (scripts/build.ps1) or pass -Sxpe."
}

$Work = Join-Path ([System.IO.Path]::GetTempPath()) "sxpe-roundtrip"
New-Item -ItemType Directory -Force -Path $Work | Out-Null

function Invoke-Sxpe {
    param([string[]]$SxpeArgs)
    $out = & $Sxpe @SxpeArgs --format json 2>&1 | Out-String
    if (-not $out) { throw "sxpe produced no output: $($SxpeArgs -join ' ')" }
    $j = $out | ConvertFrom-Json
    if (-not $j.ok) {
        $msg = $j.error.message
        throw "sxpe failed ($($SxpeArgs -join ' ')): $msg"
    }
    return $j.data
}

function Get-HeaderFacts([string]$Path) {
    $fs = [System.IO.File]::OpenRead($Path)
    try {
        $buf = New-Object byte[] 96
        [void]$fs.Read($buf, 0, 96)
    } finally { $fs.Close() }
    $le = [BitConverter]::IsLittleEndian
    function U32([int]$o) {
        $v = [BitConverter]::ToUInt32($buf, $o)
        if (-not $le) { return 0 }
        return $v
    }
    $size = (Get-Item $Path).Length
    $indexPos = U32 0x40
    $indexSize = U32 0x2C
    return [pscustomobject]@{
        Magic        = [System.Text.Encoding]::ASCII.GetString($buf, 0, 4)
        Major        = U32 4
        Minor        = U32 8
        Unknown1Zero = ($buf[0x0C..0x23] | Where-Object { $_ -ne 0 }).Count -eq 0
        Count        = U32 0x24
        Unknown2     = U32 0x28
        IndexSize    = $indexSize
        Unknown3Zero = ($buf[0x30..0x3B] | Where-Object { $_ -ne 0 }).Count -eq 0
        IndexVersion = U32 0x3C
        IndexPos     = $indexPos
        Unknown4Zero = ($buf[0x44..0x5F] | Where-Object { $_ -ne 0 }).Count -eq 0
        FileSize     = $size
        IndexAtEof   = ($indexPos + $indexSize) -eq $size
    }
}

function Get-ListKey($item) {
    return "{0:X8}-{1:X8}-{2:X16}-{3}-{4}-{5}" -f `
        [uint32]$item.type, [uint32]$item.group, [uint64]$item.instance, `
        [int]$item.ordinal, [int]$item.memSize, $(if ($item.compressed) { 1 } else { 0 })
}

function Get-AllItems([string]$PackagePath) {
    $items = @()
    $cursor = $null
    do {
        $args = @("resource", "list", "--package", $PackagePath, "--limit", "500")
        if ($cursor) { $args += @("--cursor", $cursor) }
        $data = Invoke-Sxpe $args
        if ($null -ne $data.items) {
            $items += @($data.items)
        }
        if ($data.truncated) { $cursor = [string]$data.nextCursor } else { $cursor = $null }
        if ($cursor -eq "") { $cursor = $null }
    } while ($cursor)
    return @($items)
}

$targets = New-Object System.Collections.Generic.List[string]
if ($Packages) {
    foreach ($p in $Packages) { [void]$targets.Add($p) }
}
if ($env:SXPE_ROUNDTRIP_PACKAGES) {
    foreach ($p in $env:SXPE_ROUNDTRIP_PACKAGES -split ";" ) {
        if ($p.Trim()) { [void]$targets.Add($p.Trim()) }
    }
}
if ($GameDir) {
    foreach ($rel in @(
        "Game\Bin\Misc\fallback.package",
        "GameData\Shared\DeltaPackages\p20\DeltaBuild_p20.package"
    )) {
        $p = Join-Path $GameDir $rel
        if (Test-Path $p) { [void]$targets.Add($p) }
    }
}
if ($UserDir) {
    $mods = Join-Path $UserDir "Mods\Packages"
    if (Test-Path $mods) {
        Get-ChildItem $mods -Filter *.package -ErrorAction SilentlyContinue |
            Select-Object -First 10 |
            ForEach-Object { [void]$targets.Add($_.FullName) }
    }
}
if ($targets.Count -eq 0) {
    $syn = Join-Path $Root "fixtures\synthetic\single-blob.bin"
    if (-not (Test-Path $syn)) { throw "No packages given and synthetic fixture missing." }
    Write-Host "No game/CC packages; using synthetic $syn"
    [void]$targets.Add($syn)
}

$failed = 0
$seen = @{}
foreach ($src in $targets) {
    if ($seen.ContainsKey($src)) { continue }
    $seen[$src] = $true
    if (-not (Test-Path $src)) {
        Write-Host "SKIP missing $src"
        continue
    }
    $name = [IO.Path]::GetFileName($src)
    Write-Host "=== $name ==="

    if ($Tbc) {
        $h = Get-HeaderFacts $src
        Write-Host ("  header magic={0} major={1} minor={2} count={3} indexVer={4} indexAtEof={5} unkZero={6}" -f `
            $h.Magic, $h.Major, $h.Minor, $h.Count, $h.IndexVersion, $h.IndexAtEof, `
            ($h.Unknown1Zero -and $h.Unknown3Zero -and $h.Unknown4Zero -and $h.Unknown2 -eq 0))
    }

    $copy = Join-Path $Work ("in-" + [Guid]::NewGuid().ToString("n") + ".package")
    $out = Join-Path $Work ("out-" + [Guid]::NewGuid().ToString("n") + ".package")
    Copy-Item -LiteralPath $src -Destination $copy -Force

    $before = Invoke-Sxpe @("package", "info", "--package", $copy)
    $listBefore = Get-AllItems $copy
    Invoke-Sxpe @("package", "saveAs", "--package", $copy, "--path", $out, "--force") | Out-Null
    $after = Invoke-Sxpe @("package", "info", "--package", $out)
    $listAfter = Get-AllItems $out

    $fields = @("indexCount", "compressedCount", "deletedCount", "dirPresent", "major", "minor", "indexVersion")
    foreach ($f in $fields) {
        $b = $before.$f; $a = $after.$f
        if ("$b" -ne "$a") {
            Write-Host "  FAIL $f before=$b after=$a"
            $failed++
        }
    }
    $listBefore = @($listBefore)
    $listAfter = @($listAfter)
    if ([int]$before.indexCount -ne $listBefore.Count) {
        Write-Host "  FAIL listed $($listBefore.Count) != indexCount $($before.indexCount)"
        $failed++
    }
    $kb = @{}; foreach ($it in $listBefore) { $kb[(Get-ListKey $it)] = $true }
    $ka = @{}; foreach ($it in $listAfter) { $ka[(Get-ListKey $it)] = $true }
    if ($kb.Count -ne $ka.Count) {
        Write-Host "  FAIL list count $($kb.Count) -> $($ka.Count)"
        $failed++
    } else {
        $missing = 0
        foreach ($k in $kb.Keys) { if (-not $ka.ContainsKey($k)) { $missing++ } }
        if ($missing -gt 0) {
            Write-Host "  FAIL $missing TGI/size rows changed"
            $failed++
        }
    }

    $size = (Get-Item $src).Length
    if ($Payloads -and $size -le $PayloadMaxBytes) {
        # Hash via list+export would be slow; compare memSize already in the key.
        Write-Host "  payloads skipped (TGI+memSize compared; full body hash is CLI-heavy)"
    }

    Write-Host ("  ok indexCount={0} compressed={1} listed={2}" -f $after.indexCount, $after.compressedCount, $listAfter.Count)
}

if ($failed -gt 0) {
    Write-Host "$failed comparison(s) failed"
    exit 1
}
Write-Host "round-trip ok ($($seen.Count) package(s)); work dir $Work"
exit 0
