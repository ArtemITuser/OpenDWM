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

param([string[]]$Donors = @('shell32.dll', 'shlwapi.dll'), [string]$Shim = 'kntdl', [string]$Stub = 'keelaux', [string]$Cut = 'donor\cut3')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'env.ps1') | Out-Null
$root = Split-Path -Parent $here
$cut3 = Join-Path $root $Cut
$census = Join-Path $here 'census'
$work = Join-Path $env:KEEL_TOOLS '_dl\ntseam'
Remove-Item $work -Recurse -Force -EA SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null

$donorPaths = $Donors | ForEach-Object { Join-Path $cut3 $_ }
python (Join-Path $census 'gen_ntdll_seam.py') (Get-KeelHostDll 'System32\ntdll.dll') $Shim $Stub $work @donorPaths
if ($LASTEXITCODE -ne 0) { throw 'gen_ntdll_seam failed' }

$stubOut = Join-Path $cut3 "$Stub.dll"
$shimOut = Join-Path $cut3 "$Shim.dll"
Push-Location $work

& cl.exe /nologo /c /Gs- "$Stub.c" 2>&1 | Select-String 'error' | ForEach-Object { $_.Line }
& link.exe /nologo /DLL /NOENTRY "/DEF:$Stub.def" "$Stub.obj" "/OUT:$stubOut" /MACHINE:X64 2>&1 | Select-String 'error|LNK' | ForEach-Object { $_.Line }

& link.exe /nologo /DLL /NOENTRY "/DEF:$Shim.def" "/OUT:$shimOut" /MACHINE:X64 2>&1 | Select-String 'error|LNK' | ForEach-Object { $_.Line }
Pop-Location
foreach ($n in $Stub, $Shim) { Remove-Item (Join-Path $cut3 "$n.lib"), (Join-Path $cut3 "$n.exp") -EA SilentlyContinue }
if (-not (Test-Path $stubOut)) { throw "$Stub build failed" }
if (-not (Test-Path $shimOut)) { throw "$Shim build failed" }

foreach ($d in $donorPaths) {
    python (Join-Path $census 'patch_import.py') $d ntdll $Shim
    if ($LASTEXITCODE -ne 0) { throw "patch_import failed for $d" }
}
Write-Host "built $Shim.dll + $Stub.dll and repointed $($Donors -join ', ') ntdll -> $Shim" -ForegroundColor Green
