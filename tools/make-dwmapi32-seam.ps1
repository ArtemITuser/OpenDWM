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
    [string]$Win7  = 'donor\raw32\dwmapi.dll',
    [string]$Win10 = 'donor\raw32\dwmapi-win10-x86.dll',
    [string]$HostUser32 = '')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
. (Join-Path $here 'env32.ps1') | Out-Null
$census = Join-Path $here 'census'
$out = Join-Path $root 'out\x86\dwmapi-seam'
$work = Join-Path $env:KEEL_TOOLS '_dl\dwmapi32-seam'
Remove-Item $work -Recurse -Force -EA SilentlyContinue
New-Item -ItemType Directory -Force $work, $out | Out-Null
$w7  = Join-Path $root $Win7
$w10 = Join-Path $root $Win10
if (-not (Test-Path $w10)) { $w10 = Get-KeelHostDll 'SysWOW64\dwmapi.dll' }
if (-not $HostUser32) { $HostUser32 = Get-KeelHostDll 'SysWOW64\user32.dll' }
foreach ($p in $w7, $w10, $HostUser32) { if (-not (Test-Path $p)) { throw "missing input $p" } }

python (Join-Path $census 'gen_dwmapi32_seam.py') $w7 $w10 $HostUser32 $work
if ($LASTEXITCODE -ne 0) { throw 'gen_dwmapi32_seam failed' }

Push-Location $work
try {

    & cl.exe /nologo /c /Gs- keelstub.c 2>&1 | Select-String 'error' | ForEach-Object { $_.Line }
    & link.exe /nologo /DLL /NOENTRY /DEF:keelstub.def keelstub.obj "/OUT:$out\keelstub.dll" /MACHINE:X86 kernel32.lib 2>&1 | Select-String 'error|LNK' | ForEach-Object { $_.Line }

    & link.exe /nologo /DLL /NOENTRY /DEF:keel32.def "/OUT:$out\keel32.dll" /MACHINE:X86 2>&1 | Select-String 'error|LNK' | ForEach-Object { $_.Line }
    & link.exe /nologo /DLL /NOENTRY /DEF:dwmapi.def "/OUT:$out\dwmapi.dll" /MACHINE:X86 2>&1 | Select-String 'error|LNK' | ForEach-Object { $_.Line }
} finally { Pop-Location }
foreach ($n in 'keelstub', 'keel32', 'dwmapi') { Remove-Item (Join-Path $out "$n.lib"), (Join-Path $out "$n.exp") -EA SilentlyContinue }
foreach ($n in 'keelstub', 'keel32', 'dwmapi') { if (-not (Test-Path (Join-Path $out "$n.dll"))) { throw "$n.dll did not build" } }

Copy-Item $w7 (Join-Path $out 'dwmapi7.dll') -Force
python (Join-Path $census 'patch_import.py') (Join-Path $out 'dwmapi7.dll') user32 keel32
if ($LASTEXITCODE -ne 0) { throw 'patch_import failed for dwmapi7' }
Copy-Item $w10 (Join-Path $out 'dwmapi10.dll') -Force
Get-ChildItem $out | Select-Object Name, Length | Format-Table -AutoSize | Out-String | Write-Host
Write-Host "x86 dwmapi seam built in $out" -ForegroundColor Green
