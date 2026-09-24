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

param([string]$Exe = 'explorer.exe', [string]$Cut3 = '', [switch]$DependencyOnly)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'env.ps1') | Out-Null
if (-not $Cut3) { $Cut3 = Join-Path (Split-Path -Parent $here) 'donor\cut3' }
$exePath = Join-Path $Cut3 $Exe

$depPattern = '<assemblyIdentity\b[^>]*?name="Microsoft\.Windows\.Common-Controls"[^>]*?(?:/>|>\s*</assemblyIdentity>)'
$depKeel    = '<assemblyIdentity type="win32" name="Keel.Common-Controls" version="6.0.7601.23403" processorArchitecture="amd64"/>'

if ($DependencyOnly) {

    $ids = @(& python (Join-Path $here 'census\manifest_ids.py') $exePath 2>$null | ForEach-Object { [int]$_ })
    if (-not $ids) { $ids = @(1, 2, 3) }
    $done = 0
    foreach ($id in $ids) {
        $cur = "$env:TEMP\keel-cur-$id.manifest"
        if (Test-Path $cur) { Remove-Item $cur -Force }
        & mt.exe -inputresource:"$exePath;#$id" -out:$cur 2>&1 | Out-Null
        if (-not (Test-Path $cur)) { continue }
        $x = Get-Content $cur -Raw
        if ($x -notmatch $depPattern) { Write-Host "  #$id in $Exe has no Common-Controls dependency (left alone)"; continue }
        $x = [regex]::Replace($x, $depPattern, $depKeel)
        $mf = "$env:TEMP\keel-dep-$id.manifest"
        [IO.File]::WriteAllText($mf, $x, (New-Object System.Text.UTF8Encoding($false)))
        & mt.exe -manifest $mf -outputresource:"$exePath;#$id" 2>&1 | Where-Object { $_ -match 'error' } | ForEach-Object { $_ }
        if ($LASTEXITCODE -ne 0) { throw "mt.exe failed writing #$id of $Exe" }
        $done++
        Write-Host "rewrote Common-Controls dependency -> Keel.Common-Controls in $Exe (#$id)"
    }
    if (-not $done) { Write-Host "  $Exe has no manifest resource carrying the Win10 dependency" }
    return
}

$cur = "$env:TEMP\keel-cur.manifest"
# without this, a failed read leaves the PREVIOUS binary's manifest here and it gets written into this one
Remove-Item $cur -Force -ErrorAction SilentlyContinue
& mt.exe -inputresource:"$exePath;#1" -out:$cur 2>&1 | Out-Null
if (-not (Test-Path $cur)) { throw "no embedded manifest in $Exe" }
$xml = Get-Content $cur -Raw

$xml = [regex]::Replace($xml, '\s*<!-- Keel-injected file redirections -->', '')
$xml = [regex]::Replace($xml, '\s*<file\s+name="[^"]+"\s*/>', '')
$xml = [regex]::Replace($xml, '\s*<file\s+name="[^"]+"\s*>\s*</file>', '')

$xml = [regex]::Replace($xml, $depPattern, $depKeel)
$excluded = @('comctl32.dll')

$already = @([regex]::Matches($xml, '<file\s+name="([^"]+)"') | ForEach-Object { $_.Groups[1].Value.ToLower() })
$dlls = Get-ChildItem $Cut3 -File | Where-Object { $_.Extension -ieq '.dll' -and $excluded -notcontains $_.Name.ToLower() -and $already -notcontains $_.Name.ToLower() } | Select-Object -ExpandProperty Name | Sort-Object
$files = "  <!-- Keel-injected file redirections -->`n" + (($dlls | ForEach-Object { "  <file name=`"$_`"/>" }) -join "`n") + "`n"

$merged = $xml -replace '</assembly>\s*$', "$files</assembly>`n"
$mf = "$env:TEMP\keel-merged.manifest"
[IO.File]::WriteAllText($mf, $merged, (New-Object System.Text.UTF8Encoding($false)))

& mt.exe -manifest $mf -outputresource:"$exePath;#1" 2>&1 | ForEach-Object { $_ } | Where-Object { $_ -match 'error|success' -or $_ -match '\S' } | Select-Object -Last 3
Write-Host "applied $($dlls.Count) <file> redirections to $Exe embedded manifest"
