# KeelShim - Windows 7 user-mode UI translation for the Windows 10 kernel
# Copyright (C) 2026 Kevin Dalli <projectkeel@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

param(
    [string]$Iso = 'C:\Keel-media\Win7SP1_x64.iso',
    [string]$Out = (Join-Path (Split-Path $PSScriptRoot) 'donor'),
    [int]$ImageIndex = 0,
    [switch]$ManifestOnly
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1') | Out-Null
$sevenZip = Get-Command 7z.exe -ErrorAction Stop | Select-Object -ExpandProperty Source

$raw = Join-Path $Out 'raw'
New-Item -ItemType Directory -Force $raw | Out-Null
$chosen = $null

if ($ManifestOnly) {
    $prev = Join-Path $Out 'manifest.json'
    if (Test-Path $prev) { $pm = Get-Content $prev -Raw | ConvertFrom-Json; $chosen = [pscustomobject]@{ Name = $pm.image; Version = $pm.imageVersion } }
    if (-not $chosen) { $chosen = [pscustomobject]@{ Name = 'unknown'; Version = 'unknown' } }
} else {
if (-not (Test-Path $Iso)) { throw "donor ISO not found $Iso  (supply your own licensed Windows 7 SP1 x64 media)" }
Write-Host "mounting $Iso"
$img = Mount-DiskImage -ImagePath $Iso -PassThru
try {
    $vol = ($img | Get-Volume)
    $drive = "$($vol.DriveLetter):"
    $wim = Join-Path $drive 'sources\install.wim'
    if (-not (Test-Path $wim)) { throw "install.wim not found on $drive" }

    # 7z reports WIM metadata only for some images, so prefer DISM and fall back to probing
    $images = @()
    $hdr = & $sevenZip l -slt $wim '-x!*\*' 2>&1 | ForEach-Object { "$_" }
    $commentLine = $hdr | Where-Object { $_ -like 'Comment = *' } | Select-Object -First 1
    if ($commentLine) {
        [xml]$meta = $commentLine.Substring('Comment = '.Length)
        $images = @($meta.WIM.IMAGE | ForEach-Object { [pscustomobject]@{ Index=[int]$_.INDEX; Name=$_.NAME; Edition=$_.WINDOWS.EDITIONID; Arch=$_.WINDOWS.ARCH; Version=("{0}.{1}.{2}.{3}" -f $_.WINDOWS.VERSION.MAJOR,$_.WINDOWS.VERSION.MINOR,$_.WINDOWS.VERSION.BUILD,$_.WINDOWS.VERSION.SPBUILD) } })
    }
    if (-not $images) {
        try { $images = @(Get-WindowsImage -ImagePath $wim -ErrorAction Stop | ForEach-Object {
                  [pscustomobject]@{ Index=$_.ImageIndex; Name=$_.ImageName; Edition=''; Arch='9'; Version="$($_.Version)" } }) } catch { }
    }
    if ($images) { $images | Format-Table -AutoSize | Out-String | Write-Host }
    if ($ImageIndex -eq 0 -and $images) {
        $pick = $images | Where-Object { $_.Arch -eq '9' -and $_.Edition -match 'Ultimate|Enterprise|Professional' } | Sort-Object { @('Ultimate','Enterprise','Professional').IndexOf($_.Edition) } | Select-Object -First 1
        if (-not $pick) { $pick = $images | Where-Object { $_.Arch -eq '9' } | Select-Object -Last 1 }
        $ImageIndex = $pick.Index
    }
    $chosen = $images | Where-Object { $_.Index -eq $ImageIndex }
    if (-not $chosen) { $chosen = [pscustomobject]@{ Name = 'unknown'; Version = 'unknown' } }
    Write-Host "using image #$ImageIndex $($chosen.Name) $($chosen.Version)"
    if ($chosen.Version -notlike '6.1.7601.*') { Write-Warning "expected 6.1.7601.x, got $($chosen.Version)" }

    $prefix = $null
    foreach ($p in @('') + (1..8 | ForEach-Object { "$_\" })) {
        if (& $sevenZip l -ba $wim "${p}Windows\System32\dwm.exe" 2>&1 | Where-Object { $_ -match 'dwm\.exe' }) { $prefix = $p; break }
    }
    if ($null -eq $prefix) { throw 'could not find Windows\System32\dwm.exe in install.wim' }

    $paths = @(
        "${prefix}Windows\System32\*",
        "${prefix}Windows\explorer.exe",
        "${prefix}Windows\regedit.exe",
        "${prefix}Windows\Resources\*",
        "${prefix}Windows\Cursors\*",
        "${prefix}Windows\Fonts\segoe*",
        "${prefix}Windows\Fonts\seguisym.ttf",
        "${prefix}Windows\Media\*",
        "${prefix}Windows\Web\Wallpaper\*",
        "${prefix}Windows\WinSxS\Manifests\*",
        "${prefix}Windows\WinSxS\amd64_microsoft.windows.common-controls_*\*",
        # the v6 comctl32 keeps every string and most dialogs in this MUI, WinSxS abbreviates its folder name
        "${prefix}Windows\WinSxS\amd64_microsoft.windows.c..-controls.resources_*_6.0.*_en-us_*\*",
        "${prefix}Windows\en-US\*",
        "${prefix}Windows\Branding\*"
    )
    Write-Host 'extracting (this takes several minutes)'
    & $sevenZip x -y "-o$raw" $wim @paths | Select-String -Pattern 'Everything is Ok|Error|Files: ' | ForEach-Object { Write-Host $_.Line }
    if ($LASTEXITCODE -ne 0) { throw "7z exited $LASTEXITCODE" }

    if ($prefix) {
        $src = Join-Path $raw "$ImageIndex\Windows"
        $dst = Join-Path $raw 'Windows'
        if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Recurse -Force }
        Move-Item -LiteralPath $src $dst
        Remove-Item -LiteralPath (Join-Path $raw "$ImageIndex") -Recurse -Force -ErrorAction SilentlyContinue
    }

    $raw32 = Join-Path $Out 'raw32'
    New-Item -ItemType Directory -Force $raw32 | Out-Null
    & $sevenZip e -y "-o$raw32" $wim "${prefix}Windows\SysWOW64\dwmapi.dll" "${prefix}Windows\SysWOW64\user32.dll" | Select-String -Pattern 'Everything is Ok|Error' | ForEach-Object { Write-Host "  SysWOW64 $($_.Line)" }
} finally {
    Dismount-DiskImage -ImagePath $Iso | Out-Null
}
}

Write-Host 'writing manifest.json'
$files = Get-ChildItem (Join-Path $raw 'Windows') -Recurse -File -Force
$entries = foreach ($f in $files) {
    $rel = $f.FullName.Substring($raw.Length + 1)
    $vi = $f.VersionInfo

    [ordered]@{
        path = $rel
        size = $f.Length
        sha256 = (Get-FileHash $f.FullName -Algorithm SHA256).Hash
        fileVersion = if ($vi.FileVersionRaw) { $vi.FileVersionRaw.ToString() } else { $null }
        fileVersionString = $vi.FileVersion
        productVersion = if ($vi.ProductVersionRaw) { $vi.ProductVersionRaw.ToString() } else { $null }
    }
}
$manifest = [ordered]@{
    donor = 'Windows 7 SP1 x64'
    iso = (Split-Path $Iso -Leaf)
    image = $chosen.Name
    imageVersion = $chosen.Version
    extracted = (Get-Date -Format s)
    fileCount = @($entries).Count
    files = $entries
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $Out 'manifest.json') -Encoding UTF8
Write-Host ("donor {0} files, manifest written to {1}" -f @($entries).Count, (Join-Path $Out 'manifest.json')) -ForegroundColor Green
