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

param([string[]]$Donors = @('shell32.dll'), [string]$Shim = 'keelkr32', [string]$Stub = 'keelaux', [string]$Cut = 'donor\cut3',
      [string]$Primary = '', [string]$Fallback = '')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'env.ps1') | Out-Null
$root = Split-Path -Parent $here
$cut3 = Join-Path $root $Cut
$census = Join-Path $here 'census'
$work = Join-Path $env:KEEL_TOOLS '_dl\krseam'
Remove-Item $work -Recurse -Force -EA SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null

if (-not $Primary)  { $Primary  = Get-KeelHostDll 'System32\kernel32.dll' }
if (-not $Fallback) { $Fallback = Get-KeelHostDll 'System32\kernelbase.dll' }

$donorPaths = @($Donors | ForEach-Object { Join-Path $cut3 $_ })
python (Join-Path $census 'gen_dep_seam.py') $Shim $Stub $work kernel32 $Primary $Fallback @donorPaths
if ($LASTEXITCODE -ne 0) { throw 'gen_dep_seam failed' }

$shimOut = Join-Path $cut3 "$Shim.dll"
Push-Location $work

$stubHasCode = (Get-Content "$Stub.c" | Where-Object { $_ -match '__stdcall' }).Count -gt 0
if ($stubHasCode) {
    $stubOut = Join-Path $cut3 "$Stub.dll"
    & cl.exe /nologo /c /Gs- "$Stub.c" 2>&1 | Select-String 'error' | ForEach-Object { $_.Line }
    & link.exe /nologo /DLL /NOENTRY "/DEF:$Stub.def" "$Stub.obj" "/OUT:$stubOut" /MACHINE:X64 2>&1 | Select-String 'error|LNK' | ForEach-Object { $_.Line }
}
& link.exe /nologo /DLL /NOENTRY "/DEF:$Shim.def" "/OUT:$shimOut" /MACHINE:X64 2>&1 | Select-String 'error|LNK' | ForEach-Object { $_.Line }
Pop-Location
Remove-Item (Join-Path $cut3 "$Shim.lib"), (Join-Path $cut3 "$Shim.exp") -EA SilentlyContinue
if (-not (Test-Path $shimOut)) { throw "$Shim build failed" }

foreach ($d in $donorPaths) {
    python (Join-Path $census 'patch_import.py') $d kernel32 $Shim
    if ($LASTEXITCODE -ne 0) { throw "patch_import failed for $d" }
}
Write-Host "built $Shim.dll and repointed $($Donors -join ', ') kernel32 -> $Shim" -ForegroundColor Green
