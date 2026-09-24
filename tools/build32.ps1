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

param([switch]$Clean)
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'env32.ps1')
$root = Split-Path $PSScriptRoot
$src = Join-Path $root 'src32'
$out = Join-Path $root 'out\x86'
if ($Clean -and (Test-Path -LiteralPath $out)) { Remove-Item -LiteralPath $out -Recurse -Force }
Set-Location $src
$log = Join-Path $env:KEEL_TOOLS '_dl\build-x86.log'
cmd.exe /c "cmake --preset x86 > `"$log`" 2>&1 && cmake --build --preset x86 >> `"$log`" 2>&1"
$rc = $LASTEXITCODE
if ($rc -ne 0) { Get-Content $log | Select-Object -Last 40 } else { Get-Content $log | Select-String 'warning|Linking' | ForEach-Object { $_.Line } }
"build32 exit $rc"
if ($rc -eq 0) {
    Get-ChildItem $out -Recurse -Include keelshim32.dll, keelldr32.exe |
        ForEach-Object { "  $($_.Name)  $([math]::Round($_.Length/1KB))KB" }
}
exit $rc
