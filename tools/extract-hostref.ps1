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

[CmdletBinding()]
param(
    [string]$Iso = 'C:\Keel-media\LTSC2021_x64.iso',
    [string]$Out = (Join-Path (Split-Path $PSScriptRoot) 'host-ref'),
    [int]$ImageIndex = 0
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1') | Out-Null
$sevenZip = Get-Command 7z.exe -ErrorAction Stop | Select-Object -ExpandProperty Source

# every binary a generated seam resolves a forwarder against
$sys32 = @('user32.dll', 'ntdll.dll', 'kernel32.dll', 'kernelbase.dll', 'advapi32.dll', 'sechost.dll',
           'rpcrt4.dll', 'gdi32.dll', 'combase.dll', 'ole32.dll', 'shcore.dll', 'win32u.dll', 'dwmapi.dll')
$wow64 = @('dwmapi.dll', 'user32.dll')

$raw = Join-Path $Out 'raw'
$want = Join-Path $raw 'Windows\System32\user32.dll'
if (Test-Path $want) { Write-Host "host-ref already present at $raw"; return }
if (-not (Test-Path $Iso)) { throw "Windows 10 ISO not found $Iso  (needed so the seams are built against the target, not this machine)" }

New-Item -ItemType Directory -Force $raw | Out-Null
Write-Host "mounting $Iso"
$img = Mount-DiskImage -ImagePath $Iso -PassThru
try {
    $drive = $null
    for ($i = 0; $i -lt 30 -and -not $drive; $i++) { Start-Sleep 1; $drive = (Get-DiskImage -ImagePath $Iso | Get-Volume).DriveLetter }
    if (-not $drive) { throw 'the mounted ISO never got a drive letter' }
    $wim = "${drive}:\sources\install.wim"
    if (-not (Test-Path $wim)) { throw "install.wim not found on ${drive}:" }

    # 7z only reports WIM metadata for some images, so ask DISM and fall back to probing the path prefix
    $chosen = $null
    try {
        $images = @(Get-WindowsImage -ImagePath $wim -ErrorAction Stop)
        if ($ImageIndex -eq 0) { $ImageIndex = ($images | Select-Object -First 1).ImageIndex }
        $i = $images | Where-Object { $_.ImageIndex -eq $ImageIndex } | Select-Object -First 1
        if ($i) { $chosen = [pscustomobject]@{ Name = $i.ImageName; Version = "$($i.Version)" } }
    } catch { }
    if (-not $chosen) { $chosen = [pscustomobject]@{ Name = 'unknown'; Version = 'unknown' } }
    Write-Host "using image #$(if ($ImageIndex) { $ImageIndex } else { 1 }) $($chosen.Name) $($chosen.Version)"

    $prefix = $null
    foreach ($p in @('') + (1..8 | ForEach-Object { "$_\" })) {
        $probe = & $sevenZip l -ba $wim "${p}Windows\System32\user32.dll" 2>&1
        if ($probe | Where-Object { $_ -match 'user32\.dll' }) { $prefix = $p; break }
    }
    if ($null -eq $prefix) { throw 'could not find Windows\System32\user32.dll in install.wim' }

    foreach ($pair in @(@{ Dir = 'System32'; Files = $sys32 }, @{ Dir = 'SysWOW64'; Files = $wow64 })) {
        $dst = Join-Path $raw "Windows\$($pair.Dir)"
        New-Item -ItemType Directory -Force $dst | Out-Null
        $args = @($pair.Files | ForEach-Object { "${prefix}Windows\$($pair.Dir)\$_" })
        & $sevenZip e -y "-o$dst" $wim @args | Select-String 'Everything is Ok|Error' | ForEach-Object { Write-Host "  $($pair.Dir) $($_.Line)" }
        if ($LASTEXITCODE -ne 0) { throw "7z exited $LASTEXITCODE extracting $($pair.Dir)" }
    }

    $info = [ordered]@{ iso = (Split-Path $Iso -Leaf); image = $chosen.Name; imageVersion = $chosen.Version
                        extracted = (Get-Date -Format s) }
    $info | ConvertTo-Json | Set-Content (Join-Path $Out 'manifest.json') -Encoding UTF8
}
finally { Dismount-DiskImage -ImagePath $Iso | Out-Null }

$n = (Get-ChildItem $raw -Recurse -File | Measure-Object).Count
Write-Host "host-ref $n files from the Windows 10 image -> $raw" -ForegroundColor Green
