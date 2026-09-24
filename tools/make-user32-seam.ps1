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
    [string[]]$Donors = @('dwmapi.dll', 'dwm.exe', 'dwmcore.dll', 'uDWM.dll', 'dwmredir.dll'),
    [string]$Shim = 'keel32', [string]$Cut = 'donor\cut3')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'env.ps1') | Out-Null
$root = Split-Path -Parent $here
$cut3 = Join-Path $root $Cut
$census = Join-Path $here 'census'
$work = Join-Path $env:KEEL_TOOLS '_dl\seam'
Remove-Item $work -Recurse -Force -EA SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null

$donorPaths = @($Donors | ForEach-Object { Join-Path $cut3 $_ })
python (Join-Path $census 'gen_user32_seam.py') (Get-KeelHostDll 'System32\user32.dll') $Shim $work @donorPaths
if ($LASTEXITCODE -ne 0) { throw 'gen_user32_seam failed' }

$keelstubOut = Join-Path $cut3 'keelstub.dll'
$keel32Out = Join-Path $cut3 "$Shim.dll"

$stubSrc = Join-Path $root 'src\keelstub'
$defExports = { param($p) (Get-Content $p) | Where-Object { $_ -match '^\s+\S' -and $_ -notmatch '^\s*(LIBRARY|EXPORTS)' } | ForEach-Object { $_.Trim() } }
$needed = & $defExports (Join-Path $work 'keelstub.def')
$have   = & $defExports (Join-Path $stubSrc 'keelstub.def')
$missing = @($needed | Where-Object { $_ -notin $have })
if ($missing.Count) { throw "src\keelstub\keelstub.def is missing exports the donors import: $($missing -join ', ')" }
$extra = @($have | Where-Object { $_ -notin $needed })
if ($extra.Count) { Write-Host "keelstub exports that no donor imports right now $($extra -join ', ')" -ForegroundColor DarkGray }
Copy-Item (Join-Path $stubSrc 'keelstub.c') $work -Force
Copy-Item (Join-Path $stubSrc 'keelstub.def') $work -Force

Push-Location $work

& cl.exe /nologo /c /Gs- keelstub.c 2>&1 | Select-String 'error' | ForEach-Object { $_.Line }
& link.exe /nologo /DLL /NOENTRY /DEF:keelstub.def keelstub.obj "/OUT:$keelstubOut" /MACHINE:X64 kernel32.lib user32.lib 2>&1 | Select-String 'error|LNK' | ForEach-Object { $_.Line }

& link.exe /nologo /DLL /NOENTRY "/DEF:$Shim.def" "/OUT:$keel32Out" /MACHINE:X64 2>&1 | Select-String 'error|LNK' | ForEach-Object { $_.Line }
Pop-Location
foreach ($n in 'keelstub', $Shim) { Remove-Item (Join-Path $cut3 "$n.lib"), (Join-Path $cut3 "$n.exp") -EA SilentlyContinue }
if (-not (Test-Path $keel32Out)) { throw 'keel32 build failed' }
if (-not (Test-Path $keelstubOut)) { throw 'keelstub build failed' }

foreach ($d in $donorPaths) {
    python (Join-Path $census 'patch_import.py') $d user32 $Shim
    if ($LASTEXITCODE -ne 0) { throw "patch_import failed for $d" }
}
Write-Host "built $Shim.dll + keelstub.dll and repointed $($Donors -join ', ') user32 -> $Shim" -ForegroundColor Green
