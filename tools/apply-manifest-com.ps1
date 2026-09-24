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

param([string]$Exe = 'explorer.exe', [string]$Cut3 = '')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'env.ps1') | Out-Null
$root = Split-Path -Parent $here
if (-not $Cut3) { $Cut3 = Join-Path $root 'donor\cut3' }
$exePath = Join-Path $Cut3 $Exe
$hive = Join-Path $root 'donor\raw\Windows\System32\config\SOFTWARE'
if (-not (Test-Path $hive)) { throw "donor SOFTWARE hive not found $hive" }

$cur = "$env:TEMP\keel-cur.manifest"
Remove-Item $cur -Force -EA SilentlyContinue
& mt.exe -inputresource:"$exePath;#1" -out:$cur 2>&1 | Out-Null
if (-not (Test-Path $cur)) { throw "no embedded manifest in $Exe" }

$mf = "$env:TEMP\keel-com.manifest"
python (Join-Path $here 'census\gen_com_manifest.py') $hive $Cut3 $cur $mf
if ($LASTEXITCODE -ne 0) { throw 'gen_com_manifest failed' }

& mt.exe -manifest $mf -outputresource:"$exePath;#1" 2>&1 | ForEach-Object { $_ } | Where-Object { $_ -match '\S' } | Select-Object -Last 3
Write-Host "applied reg-free COM manifest to $Exe" -ForegroundColor Green
